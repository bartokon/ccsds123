#include "Ccsds123Codec.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "ccsds123/modules.hpp"

namespace ccsds123 {

namespace {

#pragma pack(push, 1)
struct HeaderLayout {
  std::array<char, 4> magic{};
  std::uint16_t version{};
  std::uint16_t nx{};
  std::uint16_t ny{};
  std::uint16_t nz{};
  std::uint16_t d{};
  std::uint16_t p{};
  std::uint16_t local_sum{};
  std::uint32_t payload_bits{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
};
#pragma pack(pop)

constexpr std::array<char, 4> kMagic{'C', '1', '2', '3'};
constexpr std::uint16_t kVersion = 2;
constexpr int kOmega = 19;
constexpr int kRbits = 64;
constexpr int kVmin = -6;
constexpr int kVmax = 9;
constexpr int kTincLog = 4;

struct BitWriter {
  explicit BitWriter(Bitstream &sink) : sink_(sink) {}

  void write_bit(bool bit) {
    current_ = static_cast<std::uint8_t>((current_ << 1) | (bit ? 1U : 0U));
    ++filled_;
    ++bits_written_;
    if (filled_ == 8U) {
      flush_byte();
    }
  }

  void write_bits(std::uint32_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
      const bool bit = ((value >> (count - 1U - i)) & 1U) != 0U;
      write_bit(bit);
    }
  }

  void write_unary(std::uint32_t value) {
    for (std::uint32_t i = 0; i < value; ++i) {
      write_bit(false);
    }
    write_bit(true);
  }

  void finish() {
    if (filled_ > 0U) {
      current_ <<= static_cast<unsigned>(8U - filled_);
      flush_byte();
    }
  }

  [[nodiscard]] std::size_t bits_written() const noexcept { return bits_written_; }

private:
  void flush_byte() {
    sink_.push_back(current_);
    current_ = 0U;
    filled_ = 0U;
  }

  Bitstream &sink_;
  std::uint8_t current_{0U};
  unsigned filled_{0U};
  std::size_t bits_written_{0U};
};

struct BitReader {
  BitReader(std::span<const std::uint8_t> bytes, std::size_t payload_bits) : bytes_(bytes), payload_bits_(payload_bits) {}

  bool read_bit(bool &bit) {
    if (consumed_bits_ >= payload_bits_) {
      return false;
    }
    if (available_bits_ == 0U) {
      if (offset_ >= bytes_.size()) {
        return false;
      }
      current_ = bytes_[offset_++];
      available_bits_ = 8U;
    }
    bit = (current_ & 0x80U) != 0U;
    current_ <<= 1U;
    --available_bits_;
    ++consumed_bits_;
    return true;
  }

  bool read_unary(std::uint32_t &value) {
    value = 0U;
    bool bit = false;
    while (true) {
      if (!read_bit(bit)) {
        return false;
      }
      if (bit) {
        return true;
      }
      ++value;
    }
  }

  bool read_bits(unsigned count, std::uint32_t &value) {
    value = 0U;
    bool bit = false;
    for (unsigned i = 0; i < count; ++i) {
      if (!read_bit(bit)) {
        return false;
      }
      value = (value << 1U) | static_cast<std::uint32_t>(bit);
    }
    return true;
  }

private:
  std::span<const std::uint8_t> bytes_{};
  std::size_t payload_bits_{0U};
  std::size_t consumed_bits_{0U};
  std::size_t offset_{0U};
  std::uint8_t current_{0U};
  unsigned available_bits_{0U};
};

struct AdaptiveState {
  std::uint32_t count{1U};
  std::uint32_t sum{4U};
};

unsigned select_k(const AdaptiveState &state) {
  unsigned k = 0U;
  while ((state.count << k) < state.sum && k < 15U) {
    ++k;
  }
  return k;
}

void update_state(AdaptiveState &state, std::uint32_t mapped, const SampleAdaptiveCoderParams &params) {
  state.sum += mapped;
  ++state.count;
  if (state.count == params.reset_threshold) {
    state.count = (state.count + 1U) >> 1U;
    state.sum = (state.sum + 1U) >> 1U;
  }
}

struct BandState {
  std::vector<int32_t> prev_row{};
  std::vector<int32_t> curr_row{};
  std::vector<int32_t> weights{};
};

modules::LocalSamples gather_samples(const BandState &band, int nx, int x, int y) {
  modules::LocalSamples samples{};
  samples.cur = band.curr_row[static_cast<std::size_t>(x)];
  samples.west = (x > 0) ? band.curr_row[static_cast<std::size_t>(x - 1)] : 0;
  samples.north = (y > 0) ? band.prev_row[static_cast<std::size_t>(x)] : 0;
  samples.north_west = (x > 0 && y > 0) ? band.prev_row[static_cast<std::size_t>(x - 1)] : 0;
  samples.north_east = (y > 0 && x + 1 < nx) ? band.prev_row[static_cast<std::size_t>(x + 1)] : samples.north;
  return samples;
}

void swap_rows(BandState &band) {
  band.prev_row = band.curr_row;
  std::fill(band.curr_row.begin(), band.curr_row.end(), 0);
}

void validate_params(const Params &params) {
  if (params.NX <= 0 || params.NY <= 0 || params.NZ <= 0) {
    throw std::invalid_argument("Image dimensions must be positive");
  }
  if (params.D <= 0 || params.D > 16) {
    throw std::invalid_argument("Bit depth must be within (0, 16]");
  }
  if (params.P != 0) {
    throw std::invalid_argument("Predictor order P > 0 not yet supported");
  }
  if (params.local_sum != Params::LocalSumMode::NeighborNarrow) {
    throw std::invalid_argument("Only neighbor-narrow local sums are implemented");
  }
  if (params.theta != 0) {
    throw std::invalid_argument("Theta must be zero for the lossless configuration");
  }
}

HeaderLayout make_header(const Params &params, std::size_t payload_bits) {
  HeaderLayout header{};
  header.magic = kMagic;
  header.version = kVersion;
  header.nx = static_cast<std::uint16_t>(params.NX);
  header.ny = static_cast<std::uint16_t>(params.NY);
  header.nz = static_cast<std::uint16_t>(params.NZ);
  header.d = static_cast<std::uint16_t>(params.D);
  header.p = static_cast<std::uint16_t>(params.P);
  header.local_sum = static_cast<std::uint16_t>(params.local_sum == Params::LocalSumMode::NeighborNarrow);
  header.payload_bits = static_cast<std::uint32_t>(payload_bits);
  return header;
}

HeaderLayout parse_header(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < sizeof(HeaderLayout)) {
    throw std::runtime_error("Container too small");
  }
  HeaderLayout header{};
  std::memcpy(&header, bytes.data(), sizeof(HeaderLayout));
  if (header.magic != kMagic) {
    throw std::runtime_error("Invalid container magic");
  }
  if (header.version != kVersion) {
    throw std::runtime_error("Unsupported container version");
  }
  return header;
}

std::vector<BandState> create_band_states(const Params &params, bool reduced) {
  const std::size_t width = static_cast<std::size_t>(params.NX);
  const int components = params.P + (reduced ? 0 : 3);
  std::vector<BandState> bands(static_cast<std::size_t>(params.NZ));
  for (auto &band : bands) {
    band.prev_row.assign(width, 0);
    band.curr_row.assign(width, 0);
    band.weights = modules::init_weights(reduced, kOmega, components);
  }
  return bands;
}

modules::PredictorInputs make_predictor_inputs(const modules::CtrlSignals &ctrl, int depth, int prev_band_sample,
                                               int32_t local_sum, int64_t numerator) {
  modules::PredictorInputs inputs{};
  inputs.ctrl = ctrl;
  inputs.depth = depth;
  inputs.omega = kOmega;
  inputs.rbits = kRbits;
  inputs.prev_band_sample = prev_band_sample;
  inputs.local_sum = local_sum;
  inputs.numerator = numerator;
  return inputs;
}

void encode_payload(std::span<const std::uint16_t> input, BitWriter &writer, const Params &params,
                    std::size_t &payload_bits) {
  const bool reduced = false;
  auto bands = create_band_states(params, reduced);
  modules::ControlState control({params.NX, params.NY, params.NZ, kVmin, kVmax, kTincLog});
  AdaptiveState coder_state{params.coder.initial_count, params.coder.initial_sum};
  const int band_stride = params.NX * params.NY;

  std::vector<int32_t> diffs(static_cast<std::size_t>(params.P + 3), 0);

  const int total = params.NX * params.NY * params.NZ;
  const int32_t offset = 1 << (params.D - 1);
  for (int s = 0; s < total; ++s) {
    const auto ctrl_out = control.step();
    const modules::CtrlSignals &ctrl = ctrl_out.ctrl;
    const int z = ctrl_out.z;
    const int pixel = s / params.NZ;
    const int x = pixel % params.NX;
    const int y = pixel / params.NX;
    auto &band = bands[static_cast<std::size_t>(z)];
    const std::size_t index = static_cast<std::size_t>(z) * static_cast<std::size_t>(band_stride) +
                              static_cast<std::size_t>(pixel);
    const int32_t sample_centered = static_cast<int32_t>(input[index]) - offset;
    auto neighborhood = gather_samples(band, params.NX, x, y);
    neighborhood.cur = sample_centered;
    const auto local = modules::local_diff(ctrl, neighborhood, false);
    diffs[0] = local.d_n;
    diffs[1] = local.d_w;
    diffs[2] = local.d_nw;
    const int64_t dot = modules::dot_product(diffs, band.weights);
    const auto pred_inputs = make_predictor_inputs(ctrl, params.D, -1, local.local_sum, dot);
    const auto pred = modules::predictor(pred_inputs);
    const modules::ResidualMapperInputs mapper_inputs{ctrl, params.D, sample_centered, pred.scaled_pred};
    const auto mapped = modules::residual_map(mapper_inputs);
    const unsigned k = select_k(coder_state);
    const std::uint32_t q = mapped.delta >> k;
    const std::uint32_t remainder_mask = (k == 0U) ? 0U : ((1U << k) - 1U);
    const std::uint32_t r = mapped.delta & remainder_mask;
    writer.write_unary(q);
    writer.write_bits(r, k);
    update_state(coder_state, mapped.delta, params.coder);
    const modules::WeightUpdateInputs wu_inputs{ctrl, params.D, kOmega, kVmin, kVmax, pred.scaled_pred, sample_centered,
                                                diffs};
    modules::weight_update(band.weights, wu_inputs, reduced);
    band.curr_row[static_cast<std::size_t>(x)] = sample_centered;
    if (x == params.NX - 1) {
      swap_rows(band);
    }
  }

  writer.finish();
  payload_bits = writer.bits_written();
}

std::uint32_t read_mapped(BitReader &reader, AdaptiveState &state, const SampleAdaptiveCoderParams &params) {
  std::uint32_t q = 0U;
  if (!reader.read_unary(q)) {
    throw std::runtime_error("Unexpected end of bitstream while reading unary prefix");
  }
  const unsigned k = select_k(state);
  std::uint32_t r = 0U;
  if (k > 0U && !reader.read_bits(k, r)) {
    throw std::runtime_error("Unexpected end of bitstream while reading remainder");
  }
  const std::uint32_t mapped = (q << k) | r;
  update_state(state, mapped, params);
  return mapped;
}

void decode_payload(std::span<const std::uint8_t> payload, std::span<std::uint16_t> output, const Params &params,
                    std::size_t payload_bits) {
  const bool reduced = false;
  auto bands = create_band_states(params, reduced);
  modules::ControlState control({params.NX, params.NY, params.NZ, kVmin, kVmax, kTincLog});
  AdaptiveState coder_state{params.coder.initial_count, params.coder.initial_sum};
  const int band_stride = params.NX * params.NY;

  BitReader reader(payload, payload_bits);
  std::vector<int32_t> diffs(static_cast<std::size_t>(params.P + 3), 0);

  const int total = params.NX * params.NY * params.NZ;
  const int32_t offset = 1 << (params.D - 1);
  const int32_t max_val = (1 << params.D) - 1;
  for (int s = 0; s < total; ++s) {
    const auto ctrl_out = control.step();
    const modules::CtrlSignals &ctrl = ctrl_out.ctrl;
    const int z = ctrl_out.z;
    const int pixel = s / params.NZ;
    const int x = pixel % params.NX;
    const int y = pixel / params.NX;
    auto &band = bands[static_cast<std::size_t>(z)];
    auto neighborhood = gather_samples(band, params.NX, x, y);
    neighborhood.cur = 0;
    const auto local = modules::local_diff(ctrl, neighborhood, false);
    diffs[0] = local.d_n;
    diffs[1] = local.d_w;
    diffs[2] = local.d_nw;
    const int64_t dot = modules::dot_product(diffs, band.weights);
    const auto pred_inputs = make_predictor_inputs(ctrl, params.D, -1, local.local_sum, dot);
    const auto pred = modules::predictor(pred_inputs);
    const std::uint32_t mapped = read_mapped(reader, coder_state, params.coder);
    const int32_t residual = modules::residual_unmap(mapped, pred.scaled_pred, params.D);
    const int32_t predicted = pred.predicted;
    const int32_t sample_centered = predicted + residual;
    const int32_t sample = std::clamp(sample_centered + offset, 0, max_val);
    const std::size_t index = static_cast<std::size_t>(z) * static_cast<std::size_t>(band_stride) +
                              static_cast<std::size_t>(pixel);
    output[index] = static_cast<std::uint16_t>(sample);
    const modules::WeightUpdateInputs wu_inputs{ctrl, params.D, kOmega, kVmin, kVmax, pred.scaled_pred, sample_centered, diffs};
    modules::weight_update(band.weights, wu_inputs, reduced);
    band.curr_row[static_cast<std::size_t>(x)] = sample_centered;
    if (x == params.NX - 1) {
      swap_rows(band);
    }
  }
}

} // namespace

void encode(const ImageU16 &input, Bitstream &out, const Params &params) {
  encode(std::span<const std::uint16_t>(input.data(), input.size()), out, params);
}

void encode(std::span<const std::uint16_t> input, Bitstream &out, const Params &params) {
  validate_params(params);
  const std::size_t expected = static_cast<std::size_t>(params.NX) * params.NY * params.NZ;
  if (input.size() != expected) {
    throw std::invalid_argument("Input size does not match dimensions");
  }

  Bitstream payload;
  BitWriter writer(payload);
  std::size_t payload_bits = 0;
  encode_payload(input, writer, params, payload_bits);
  const HeaderLayout header = make_header(params, payload_bits);
  out.clear();
  const auto payload_bytes = payload.bytes();
  out.resize(sizeof(HeaderLayout) + payload_bytes.size());
  std::memcpy(out.bytes().data(), &header, sizeof(HeaderLayout));
  std::memcpy(out.bytes().data() + sizeof(HeaderLayout), payload_bytes.data(), payload_bytes.size());
}

void decode(const Bitstream &in, ImageU16 &output, const Params &params) {
  output.resize(static_cast<std::size_t>(params.NX) * params.NY * params.NZ);
  decode(in, std::span<std::uint16_t>(output.data(), output.size()), params);
}

void decode(const Bitstream &in, std::span<std::uint16_t> output, const Params &params) {
  if (in.size() < sizeof(HeaderLayout)) {
    throw std::invalid_argument("Bitstream too small for header");
  }
  const HeaderLayout header = parse_header(in.bytes());
  Params effective = params;
  effective.NX = header.nx;
  effective.NY = header.ny;
  effective.NZ = header.nz;
  effective.D = header.d;
  effective.P = header.p;
  effective.local_sum = header.local_sum ? Params::LocalSumMode::NeighborNarrow : Params::LocalSumMode::NeighborWide;
  validate_params(effective);
  if (output.size() != static_cast<std::size_t>(effective.NX) * effective.NY * effective.NZ) {
    throw std::invalid_argument("Output span size does not match header dimensions");
  }
  const auto payload = in.bytes().subspan(sizeof(HeaderLayout));
  decode_payload(payload, output, effective, header.payload_bits);
}

ContainerSummary read_summary(std::span<const std::uint8_t> container_bytes) {
  const HeaderLayout header = parse_header(container_bytes);
  ContainerSummary summary{};
  summary.params.NX = header.nx;
  summary.params.NY = header.ny;
  summary.params.NZ = header.nz;
  summary.params.D = header.d;
  summary.params.P = header.p;
  summary.params.local_sum = header.local_sum ? Params::LocalSumMode::NeighborNarrow : Params::LocalSumMode::NeighborWide;
  summary.payload_bits = header.payload_bits;
  return summary;
}

} // namespace ccsds123
