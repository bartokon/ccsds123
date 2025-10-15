#include "Ccsds123Codec.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <vector>

#include "ccsds123/modules.hpp"

namespace ccsds123 {

namespace {

#pragma pack(push, 1)
struct HeaderLayoutV2 {
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

struct HeaderLayoutV3 {
  std::array<char, 4> magic{};
  std::uint16_t version{};
  std::uint16_t nx{};
  std::uint16_t ny{};
  std::uint16_t nz{};
  std::uint16_t d{};
  std::uint16_t p{};
  std::uint16_t local_sum{};
  std::uint16_t flags{};
  std::int16_t v_min{};
  std::int16_t v_max{};
  std::int16_t omega{};
  std::int16_t register_bits{};
  std::int16_t tinc_log{};
  std::uint16_t u_max{};
  std::uint16_t counter_size{};
  std::uint16_t initial_count_exponent{};
  std::uint16_t kz_prime{};
  std::uint32_t payload_bits{};
  std::uint32_t reserved0{};
};
#pragma pack(pop)

constexpr std::array<char, 4> kMagic{'C', '1', '2', '3'};
constexpr std::uint16_t kVersionV2 = 2;
constexpr std::uint16_t kVersionV3 = 3;

constexpr std::uint16_t kFlagReduced = 0x0001;
constexpr std::uint16_t kFlagColumnOriented = 0x0002;

struct BitWriter {
  explicit BitWriter(Bitstream &sink) : sink_(sink) {}

  void write_bit(bool bit) {
    current_ = static_cast<std::uint8_t>((current_ << 1U) | (bit ? 1U : 0U));
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
  BitReader(std::span<const std::uint8_t> bytes, std::size_t payload_bits)
      : bytes_(bytes), payload_bits_(payload_bits) {}

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

struct BandState {
  std::vector<int32_t> prev_row{};
  std::vector<int32_t> curr_row{};
  std::vector<int32_t> weights{};
};

struct HeaderInfo {
  Params params;
  std::size_t payload_bits{};
  std::uint16_t version{};
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
  if (params.reduced) {
    throw std::invalid_argument("Reduced mode is not supported in the scalar pipeline");
  }
  if (params.local_sum != Params::LocalSumMode::NeighborNarrow) {
    throw std::invalid_argument("Only neighbor-narrow local sums are implemented");
  }
  if (params.theta != 0) {
    throw std::invalid_argument("Theta must be zero for the lossless configuration");
  }
  if (params.omega <= 0 || params.omega > 31) {
    throw std::invalid_argument("OMEGA must be within (0, 31]");
  }
  if (params.register_bits <= 0 || params.register_bits > 64) {
    throw std::invalid_argument("Register size must be within (0, 64]");
  }
  if (params.v_min > params.v_max) {
    throw std::invalid_argument("V_MIN must not exceed V_MAX");
  }
  if (params.coder.u_max <= 0 || params.coder.u_max > 32) {
    throw std::invalid_argument("UMAX must be within (0, 32]");
  }
  if (params.coder.counter_size <= 0 || params.coder.counter_size > 16) {
    throw std::invalid_argument("Counter size must be within (0, 16]");
  }
  if (params.coder.initial_count_exponent < 0 || params.coder.initial_count_exponent > 16) {
    throw std::invalid_argument("Initial count exponent must be within [0, 16]");
  }
  if (params.coder.kz_prime < 0 || params.coder.kz_prime > 16) {
    throw std::invalid_argument("KZ' must be within [0, 16]");
  }
}

std::uint32_t mask_bits(unsigned bits) {
  if (bits == 0U) {
    return 0U;
  }
  if (bits >= 32U) {
    return 0xFFFFFFFFu;
  }
  return (1U << bits) - 1U;
}

std::vector<BandState> create_band_states(const Params &params) {
  const std::size_t width = static_cast<std::size_t>(params.NX);
  const int components = params.P + (params.reduced ? 0 : 3);
  std::vector<BandState> bands(static_cast<std::size_t>(params.NZ));
  for (auto &band : bands) {
    band.prev_row.assign(width, 0);
    band.curr_row.assign(width, 0);
    band.weights = modules::init_weights(params.reduced, params.omega, components);
  }
  return bands;
}

modules::PredictorInputs make_predictor_inputs(const Params &params, const modules::CtrlSignals &ctrl, int prev_band_sample,
                                               int32_t local_sum, int64_t numerator) {
  modules::PredictorInputs inputs{};
  inputs.ctrl = ctrl;
  inputs.depth = params.D;
  inputs.omega = params.omega;
  inputs.rbits = params.register_bits;
  inputs.prev_band_sample = prev_band_sample;
  inputs.local_sum = local_sum;
  inputs.numerator = numerator;
  return inputs;
}

std::uint32_t compute_rhs_part(std::uint32_t counter) { return (49U * counter) >> 7U; }

std::uint32_t compute_initial_accumulator(const Params &params) {
  const std::uint64_t lhs = 3ULL << static_cast<unsigned>(params.coder.kz_prime + 6);
  const std::uint64_t numerator = (lhs - 49ULL) << static_cast<unsigned>(params.coder.initial_count_exponent);
  return static_cast<std::uint32_t>(numerator >> 7U);
}

class SampleAdaptiveGolombEncoder {
public:
  explicit SampleAdaptiveGolombEncoder(const Params &params)
      : params_(params), accumulators_(static_cast<std::size_t>(params.NZ), 0U),
        initial_accumulator_(compute_initial_accumulator(params)),
        max_counter_((params.coder.counter_size >= 32)
                         ? std::numeric_limits<std::uint32_t>::max()
                         : ((1U << params.coder.counter_size) - 1U)) {}

  void encode_sample(const modules::CtrlSignals &ctrl, int z, std::uint32_t delta, BitWriter &writer) {
    const std::uint32_t counter_pre = counter_;
    const std::uint32_t accumulator = accumulators_[static_cast<std::size_t>(z)];
    const std::uint32_t rhs = accumulator + compute_rhs_part(counter_pre);
    const unsigned k = select_k(counter_pre, rhs, params_.D);
    const bool first_sample = ctrl.first_line && ctrl.first_in_line;

    if (first_sample) {
      writer.write_bits(delta & mask_bits(static_cast<unsigned>(params_.D)), static_cast<unsigned>(params_.D));
    } else {
      emit_code(delta, k, writer);
    }

    update_accumulator(z, counter_pre, delta, first_sample);
    update_counter(ctrl, z, counter_pre);
  }

private:
  unsigned select_k(std::uint32_t counter, std::uint32_t rhs, int depth) const {
    if (depth <= 1) {
      return 0U;
    }
    if (counter == 0U) {
      return static_cast<unsigned>(depth - 1 > 0 ? depth - 2 : 0);
    }
    if ((static_cast<std::uint64_t>(counter) << 1U) > rhs) {
      return 0U;
    }
    const int max_k = std::max(0, depth - 2);
    unsigned selected = 0U;
    for (int i = 1; i <= max_k; ++i) {
      const std::uint64_t lhs = static_cast<std::uint64_t>(counter) << i;
      if (lhs <= rhs) {
        selected = static_cast<unsigned>(i);
      }
    }
    return selected;
  }

  void emit_code(std::uint32_t delta, unsigned k, BitWriter &writer) const {
    const std::uint32_t value = delta & mask_bits(static_cast<unsigned>(params_.D));
    const std::uint32_t u = (k >= 32U) ? 0U : (value >> k);
    if (u >= static_cast<std::uint32_t>(params_.coder.u_max)) {
      for (int i = 0; i < params_.coder.u_max; ++i) {
        writer.write_bit(false);
      }
      writer.write_bits(value, static_cast<unsigned>(params_.D));
    } else {
      for (std::uint32_t i = 0; i < u; ++i) {
        writer.write_bit(false);
      }
      writer.write_bit(true);
      if (k > 0U) {
        const std::uint32_t remainder = value & mask_bits(k);
        writer.write_bits(remainder, k);
      }
    }
  }

  void update_accumulator(int z, std::uint32_t counter_pre, std::uint32_t delta, bool first_sample) {
    auto &acc = accumulators_[static_cast<std::size_t>(z)];
    if (first_sample) {
      acc = initial_accumulator_;
      return;
    }
    const std::uint64_t sum = static_cast<std::uint64_t>(acc) + delta;
    if (counter_pre < max_counter_) {
      acc = static_cast<std::uint32_t>(std::min<std::uint64_t>(sum, std::numeric_limits<std::uint32_t>::max()));
    } else {
      acc = static_cast<std::uint32_t>((sum + 1ULL) >> 1U);
    }
  }

  void update_counter(const modules::CtrlSignals &ctrl, int z, std::uint32_t counter_pre) {
    if (ctrl.first_line && ctrl.first_in_line) {
      counter_ = 1U << static_cast<unsigned>(params_.coder.initial_count_exponent);
      return;
    }
    if (z >= params_.NZ - 1) {
      if (counter_pre < max_counter_) {
        counter_ = counter_pre + 1U;
      } else {
        counter_ = (counter_pre + 1U) >> 1U;
      }
    } else {
      counter_ = counter_pre;
    }
  }

  const Params &params_;
  std::vector<std::uint32_t> accumulators_;
  std::uint32_t counter_{0U};
  std::uint32_t initial_accumulator_{0U};
  std::uint32_t max_counter_{0U};
};

class SampleAdaptiveGolombDecoder {
public:
  SampleAdaptiveGolombDecoder(const Params &params, BitReader &reader)
      : params_(params), reader_(reader), accumulators_(static_cast<std::size_t>(params.NZ), 0U),
        initial_accumulator_(compute_initial_accumulator(params)),
        max_counter_((params.coder.counter_size >= 32)
                         ? std::numeric_limits<std::uint32_t>::max()
                         : ((1U << params.coder.counter_size) - 1U)) {}

  std::uint32_t decode_sample(const modules::CtrlSignals &ctrl, int z) {
    const std::uint32_t counter_pre = counter_;
    const std::uint32_t accumulator = accumulators_[static_cast<std::size_t>(z)];
    const std::uint32_t rhs = accumulator + compute_rhs_part(counter_pre);
    const unsigned k = select_k(counter_pre, rhs, params_.D);
    const bool first_sample = ctrl.first_line && ctrl.first_in_line;

    std::uint32_t delta = 0U;
    if (first_sample) {
      delta = read_bits_checked(static_cast<unsigned>(params_.D));
    } else {
      const std::uint32_t u = read_unary_limited();
      if (u >= static_cast<std::uint32_t>(params_.coder.u_max)) {
        delta = read_bits_checked(static_cast<unsigned>(params_.D));
      } else {
        const std::uint32_t remainder = (k == 0U) ? 0U : read_bits_checked(k);
        delta = (u << k) | remainder;
      }
    }

    update_accumulator(z, counter_pre, delta, first_sample);
    update_counter(ctrl, z, counter_pre);
    return delta;
  }

private:
  unsigned select_k(std::uint32_t counter, std::uint32_t rhs, int depth) const {
    if (depth <= 1) {
      return 0U;
    }
    if (counter == 0U) {
      return static_cast<unsigned>(depth - 1 > 0 ? depth - 2 : 0);
    }
    if ((static_cast<std::uint64_t>(counter) << 1U) > rhs) {
      return 0U;
    }
    const int max_k = std::max(0, depth - 2);
    unsigned selected = 0U;
    for (int i = 1; i <= max_k; ++i) {
      const std::uint64_t lhs = static_cast<std::uint64_t>(counter) << i;
      if (lhs <= rhs) {
        selected = static_cast<unsigned>(i);
      }
    }
    return selected;
  }

  std::uint32_t read_bits_checked(unsigned count) {
    if (count == 0U) {
      return 0U;
    }
    std::uint32_t value = 0U;
    if (!reader_.read_bits(count, value)) {
      throw std::runtime_error("Unexpected end of bitstream while reading mapped residual");
    }
    return value;
  }

  std::uint32_t read_unary_limited() {
    std::uint32_t zeros = 0U;
    while (zeros < static_cast<std::uint32_t>(params_.coder.u_max)) {
      bool bit = false;
      if (!reader_.read_bit(bit)) {
        throw std::runtime_error("Unexpected end of bitstream while reading unary prefix");
      }
      if (bit) {
        return zeros;
      }
      ++zeros;
    }
    return zeros;
  }

  void update_accumulator(int z, std::uint32_t counter_pre, std::uint32_t delta, bool first_sample) {
    auto &acc = accumulators_[static_cast<std::size_t>(z)];
    if (first_sample) {
      acc = initial_accumulator_;
      return;
    }
    const std::uint64_t sum = static_cast<std::uint64_t>(acc) + delta;
    if (counter_pre < max_counter_) {
      acc = static_cast<std::uint32_t>(std::min<std::uint64_t>(sum, std::numeric_limits<std::uint32_t>::max()));
    } else {
      acc = static_cast<std::uint32_t>((sum + 1ULL) >> 1U);
    }
  }

  void update_counter(const modules::CtrlSignals &ctrl, int z, std::uint32_t counter_pre) {
    if (ctrl.first_line && ctrl.first_in_line) {
      counter_ = 1U << static_cast<unsigned>(params_.coder.initial_count_exponent);
      return;
    }
    if (z >= params_.NZ - 1) {
      if (counter_pre < max_counter_) {
        counter_ = counter_pre + 1U;
      } else {
        counter_ = (counter_pre + 1U) >> 1U;
      }
    } else {
      counter_ = counter_pre;
    }
  }

  const Params &params_;
  BitReader &reader_;
  std::vector<std::uint32_t> accumulators_;
  std::uint32_t counter_{0U};
  std::uint32_t initial_accumulator_{0U};
  std::uint32_t max_counter_{0U};
};

HeaderLayoutV3 make_header(const Params &params, std::size_t payload_bits) {
  HeaderLayoutV3 header{};
  header.magic = kMagic;
  header.version = kVersionV3;
  header.nx = static_cast<std::uint16_t>(params.NX);
  header.ny = static_cast<std::uint16_t>(params.NY);
  header.nz = static_cast<std::uint16_t>(params.NZ);
  header.d = static_cast<std::uint16_t>(params.D);
  header.p = static_cast<std::uint16_t>(params.P);
  header.local_sum = static_cast<std::uint16_t>(params.local_sum);
  header.flags = (params.reduced ? kFlagReduced : 0U) | (params.column_oriented ? kFlagColumnOriented : 0U);
  header.v_min = static_cast<std::int16_t>(params.v_min);
  header.v_max = static_cast<std::int16_t>(params.v_max);
  header.omega = static_cast<std::int16_t>(params.omega);
  header.register_bits = static_cast<std::int16_t>(params.register_bits);
  header.tinc_log = static_cast<std::int16_t>(params.tinc_log);
  header.u_max = static_cast<std::uint16_t>(params.coder.u_max);
  header.counter_size = static_cast<std::uint16_t>(params.coder.counter_size);
  header.initial_count_exponent = static_cast<std::uint16_t>(params.coder.initial_count_exponent);
  header.kz_prime = static_cast<std::uint16_t>(params.coder.kz_prime);
  header.payload_bits = static_cast<std::uint32_t>(payload_bits);
  return header;
}

HeaderInfo parse_header(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < sizeof(HeaderLayoutV2)) {
    throw std::runtime_error("Container too small");
  }
  HeaderLayoutV2 base{};
  std::memcpy(&base, bytes.data(), sizeof(HeaderLayoutV2));
  if (base.magic != kMagic) {
    throw std::runtime_error("Invalid container magic");
  }

  HeaderInfo info{};
  info.version = base.version;

  if (base.version == kVersionV2) {
    info.params.NX = base.nx;
    info.params.NY = base.ny;
    info.params.NZ = base.nz;
    info.params.D = base.d;
    info.params.P = base.p;
    info.params.local_sum = base.local_sum ? Params::LocalSumMode::NeighborNarrow : Params::LocalSumMode::NeighborWide;
    info.params.v_min = -6;
    info.params.v_max = 9;
    info.params.omega = 19;
    info.params.register_bits = 64;
    info.params.tinc_log = 4;
    info.params.coder.u_max = 9;
    info.params.coder.counter_size = 8;
    info.params.coder.initial_count_exponent = 6;
    info.params.coder.kz_prime = 8;
    info.payload_bits = base.payload_bits;
    return info;
  }

  if (base.version != kVersionV3) {
    throw std::runtime_error("Unsupported container version");
  }
  if (bytes.size() < sizeof(HeaderLayoutV3)) {
    throw std::runtime_error("Container too small for version 3 header");
  }
  HeaderLayoutV3 header{};
  std::memcpy(&header, bytes.data(), sizeof(HeaderLayoutV3));

  info.params.NX = header.nx;
  info.params.NY = header.ny;
  info.params.NZ = header.nz;
  info.params.D = header.d;
  info.params.P = header.p;
  info.params.local_sum = static_cast<Params::LocalSumMode>(header.local_sum);
  info.params.reduced = (header.flags & kFlagReduced) != 0U;
  info.params.column_oriented = (header.flags & kFlagColumnOriented) != 0U;
  info.params.v_min = header.v_min;
  info.params.v_max = header.v_max;
  info.params.omega = header.omega;
  info.params.register_bits = header.register_bits;
  info.params.tinc_log = header.tinc_log;
  info.params.coder.u_max = header.u_max;
  info.params.coder.counter_size = header.counter_size;
  info.params.coder.initial_count_exponent = header.initial_count_exponent;
  info.params.coder.kz_prime = header.kz_prime;
  info.payload_bits = header.payload_bits;
  return info;
}

void encode_payload(std::span<const std::uint16_t> input, BitWriter &writer, const Params &params,
                    std::size_t &payload_bits) {
  auto bands = create_band_states(params);
  modules::ControlState control({params.NX, params.NY, params.NZ, params.v_min, params.v_max, params.tinc_log});
  SampleAdaptiveGolombEncoder coder(params);
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
    const auto pred_inputs = make_predictor_inputs(params, ctrl, -1, local.local_sum, dot);
    const auto pred = modules::predictor(pred_inputs);
    const modules::ResidualMapperInputs mapper_inputs{ctrl, params.D, sample_centered, pred.scaled_pred};
    const auto mapped = modules::residual_map(mapper_inputs);
    coder.encode_sample(ctrl, z, mapped.delta, writer);
    const modules::WeightUpdateInputs wu_inputs{ctrl, params.D, params.omega, params.v_min, params.v_max, pred.scaled_pred,
                                                sample_centered, diffs};
    modules::weight_update(band.weights, wu_inputs, params.reduced);
    band.curr_row[static_cast<std::size_t>(x)] = sample_centered;
    if (x == params.NX - 1) {
      swap_rows(band);
    }
  }

  writer.finish();
  payload_bits = writer.bits_written();
}

void decode_payload(std::span<const std::uint8_t> payload, std::span<std::uint16_t> output, const Params &params,
                    std::size_t payload_bits) {
  auto bands = create_band_states(params);
  modules::ControlState control({params.NX, params.NY, params.NZ, params.v_min, params.v_max, params.tinc_log});
  BitReader reader(payload, payload_bits);
  SampleAdaptiveGolombDecoder coder(params, reader);
  const int band_stride = params.NX * params.NY;
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
    const auto pred_inputs = make_predictor_inputs(params, ctrl, -1, local.local_sum, dot);
    const auto pred = modules::predictor(pred_inputs);
    const std::uint32_t mapped = coder.decode_sample(ctrl, z);
    const int32_t residual = modules::residual_unmap(mapped, pred.scaled_pred, params.D);
    const int32_t predicted = pred.predicted;
    const int32_t sample_centered = predicted + residual;
    const int32_t sample = std::clamp(sample_centered + offset, 0, max_val);
    const std::size_t index = static_cast<std::size_t>(z) * static_cast<std::size_t>(band_stride) +
                              static_cast<std::size_t>(pixel);
    output[index] = static_cast<std::uint16_t>(sample);
    const modules::WeightUpdateInputs wu_inputs{ctrl, params.D, params.omega, params.v_min, params.v_max, pred.scaled_pred,
                                                sample_centered, diffs};
    modules::weight_update(band.weights, wu_inputs, params.reduced);
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
  const HeaderLayoutV3 header = make_header(params, payload_bits);
  out.clear();
  const auto payload_bytes = payload.bytes();
  out.resize(sizeof(HeaderLayoutV3) + payload_bytes.size());
  std::memcpy(out.bytes().data(), &header, sizeof(HeaderLayoutV3));
  std::memcpy(out.bytes().data() + sizeof(HeaderLayoutV3), payload_bytes.data(), payload_bytes.size());
}

void decode(const Bitstream &in, ImageU16 &output, const Params &params) {
  output.resize(static_cast<std::size_t>(params.NX) * params.NY * params.NZ);
  decode(in, std::span<std::uint16_t>(output.data(), output.size()), params);
}

void decode(const Bitstream &in, std::span<std::uint16_t> output, const Params &params) {
  if (in.size() < sizeof(HeaderLayoutV2)) {
    throw std::invalid_argument("Bitstream too small for header");
  }
  const HeaderInfo info = parse_header(in.bytes());
  Params effective = params;
  effective.NX = info.params.NX;
  effective.NY = info.params.NY;
  effective.NZ = info.params.NZ;
  effective.D = info.params.D;
  effective.P = info.params.P;
  effective.local_sum = info.params.local_sum;
  effective.reduced = info.params.reduced;
  effective.column_oriented = info.params.column_oriented;
  effective.v_min = info.params.v_min;
  effective.v_max = info.params.v_max;
  effective.omega = info.params.omega;
  effective.register_bits = info.params.register_bits;
  effective.tinc_log = info.params.tinc_log;
  effective.coder = info.params.coder;
  validate_params(effective);
  const std::size_t expected = static_cast<std::size_t>(effective.NX) * effective.NY * effective.NZ;
  if (output.size() != expected) {
    throw std::invalid_argument("Output span size does not match header dimensions");
  }
  const auto payload = in.bytes().subspan(info.version == kVersionV3 ? sizeof(HeaderLayoutV3) : sizeof(HeaderLayoutV2));
  decode_payload(payload, output, effective, info.payload_bits);
}

ContainerSummary read_summary(std::span<const std::uint8_t> container_bytes) {
  const HeaderInfo info = parse_header(container_bytes);
  ContainerSummary summary{};
  summary.params = info.params;
  summary.payload_bits = info.payload_bits;
  return summary;
}

} // namespace ccsds123
