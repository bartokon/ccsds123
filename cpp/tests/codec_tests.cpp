#define CATCH_CONFIG_MAIN
#include "external/catch2/catch.hpp"

#include "Ccsds123Codec.hpp"
#include "ccsds123/modules.hpp"

#include <algorithm>
#include <array>
#include <random>
#include <tuple>
#include <vector>

using namespace ccsds123;

namespace {

Params make_default_params(int nx, int ny, int nz, int d) {
  Params params;
  params.NX = nx;
  params.NY = ny;
  params.NZ = nz;
  params.D = d;
  params.P = 0;
  params.local_sum = Params::LocalSumMode::NeighborNarrow;
  params.theta = 0;
  return params;
}

ImageU16 make_gradient_rgb(int nx, int ny) {
  const int nz = 3;
  ImageU16 img(static_cast<std::size_t>(nx) * ny * nz);
  for (int z = 0; z < nz; ++z) {
    for (int y = 0; y < ny; ++y) {
      for (int x = 0; x < nx; ++x) {
        const std::size_t idx = static_cast<std::size_t>(z) * nx * ny + y * nx + x;
        img[idx] = static_cast<std::uint16_t>((x * 5 + y * 3 + z * 11) & 0xFFU);
      }
    }
  }
  return img;
}

ImageU16 make_constant_image(int nx, int ny, int nz, std::uint16_t value) {
  ImageU16 img(static_cast<std::size_t>(nx) * ny * nz, value);
  return img;
}

modules::CtrlSignals make_ctrl(bool first_line, bool first_in_line, bool last_in_line) {
  modules::CtrlSignals ctrl{};
  ctrl.first_line = first_line;
  ctrl.first_in_line = first_in_line;
  ctrl.last_in_line = last_in_line;
  ctrl.last = false;
  ctrl.scale_exponent = 0;
  return ctrl;
}

} // namespace

TEST_CASE("roundtrip_lossless_rgb_small") {
  const int nx = 8;
  const int ny = 8;
  const int nz = 3;
  const int d = 8;
  const auto img = make_gradient_rgb(nx, ny);
  auto params = make_default_params(nx, ny, nz, d);
  Bitstream bitstream;
  encode(img, bitstream, params);
  ImageU16 recon;
  decode(bitstream, recon, params);
  REQUIRE(recon == img);
}

TEST_CASE("roundtrip_random_seed") {
  const int nx = 16;
  const int ny = 16;
  const int nz = 3;
  const int d = 10;
  ImageU16 img(static_cast<std::size_t>(nx) * ny * nz);
  std::mt19937 rng(42);
  std::uniform_int_distribution<std::uint16_t> dist(0, (1u << d) - 1u);
  for (auto &value : img) {
    value = dist(rng);
  }
  auto params = make_default_params(nx, ny, nz, d);
  Bitstream bitstream;
  encode(img, bitstream, params);
  ImageU16 recon;
  decode(bitstream, recon, params);
  REQUIRE(recon == img);
}

TEST_CASE("roundtrip_constant_image_external_buffer") {
  const int nx = 12;
  const int ny = 6;
  const int nz = 2;
  const int d = 12;
  const auto img = make_constant_image(nx, ny, nz, 0x155U);
  auto params = make_default_params(nx, ny, nz, d);

  std::array<std::uint8_t, 4096> storage{};
  Bitstream bitstream(storage.data(), storage.size());
  encode(img, bitstream, params);

  const auto summary = read_summary(bitstream.bytes());
  REQUIRE(summary.params.NX == nx);
  REQUIRE(summary.params.NY == ny);
  REQUIRE(summary.params.NZ == nz);
  REQUIRE(summary.params.D == d);
  REQUIRE(summary.payload_bits > 0);
  const std::size_t total_samples = static_cast<std::size_t>(nx) * ny * nz;
  REQUIRE(bitstream.size() <= storage.size());
  const std::size_t payload_bytes = (summary.payload_bits + 7U) / 8U;
  REQUIRE(bitstream.size() >= payload_bytes);

  ImageU16 recon(total_samples);
  decode(bitstream, std::span<std::uint16_t>(recon.data(), recon.size()), params);
  REQUIRE(recon == img);
}

TEST_CASE("local_diff_matches_vhdl_cases") {
  modules::LocalSamples samples{};
  samples.cur = 120;
  samples.west = 118;
  samples.north = 122;
  samples.north_west = 121;
  samples.north_east = 123;

  const auto middle_ctrl = make_ctrl(false, false, false);
  const auto middle = modules::local_diff(middle_ctrl, samples, false);
  REQUIRE(middle.local_sum == 118 + 121 + 122 + 123);
  REQUIRE(middle.d_c == 4 * 120 - middle.local_sum);
  REQUIRE(middle.d_n == 4 * 122 - middle.local_sum);
  REQUIRE(middle.d_w == 4 * 118 - middle.local_sum);
  REQUIRE(middle.d_nw == 4 * 121 - middle.local_sum);

  const auto first_col_ctrl = make_ctrl(false, true, false);
  const auto first_col = modules::local_diff(first_col_ctrl, samples, false);
  REQUIRE(first_col.local_sum == 2 * samples.north + 2 * samples.north_east);
  REQUIRE(first_col.d_w == 4 * samples.north - first_col.local_sum);

  const auto first_line_ctrl = make_ctrl(true, false, false);
  const auto first_line = modules::local_diff(first_line_ctrl, samples, false);
  REQUIRE(first_line.local_sum == 4 * samples.west);
  REQUIRE(first_line.d_n == 0);
}

TEST_CASE("dot_product_and_init_weights") {
  const std::array<int32_t, 3> diffs{4, -2, 1};
  const std::array<int32_t, 3> weights{10, 3, -5};
  REQUIRE(modules::dot_product(diffs, weights) == 4 * 10 + (-2) * 3 + 1 * (-5));

  const auto full = modules::init_weights(false, 4, 5);
  REQUIRE(full.size() == 5);
  REQUIRE(full[0] == 14);
  REQUIRE(full[1] == 1);
  REQUIRE(std::all_of(full.begin() + 2, full.end(), [](int32_t v) { return v == 0; }));

  const auto reduced = modules::init_weights(true, 4, 4);
  REQUIRE(reduced.size() == 4);
  REQUIRE(reduced[0] == 14);
  REQUIRE(reduced[1] == 1);
  REQUIRE(reduced[2] == 0);
  REQUIRE(reduced[3] == 0);
}

TEST_CASE("predictor_handles_prev_band_and_numerator") {
  modules::CtrlSignals first_ctrl{};
  first_ctrl.first_line = true;
  first_ctrl.first_in_line = true;
  first_ctrl.scale_exponent = -6;

  modules::PredictorInputs first_inputs{};
  first_inputs.ctrl = first_ctrl;
  first_inputs.depth = 12;
  first_inputs.omega = 4;
  first_inputs.rbits = 16;
  first_inputs.prev_band_sample = 37;
  first_inputs.local_sum = 0;
  first_inputs.numerator = 0;
  const auto first = modules::predictor(first_inputs);
  REQUIRE(first.scaled_pred == (first_inputs.prev_band_sample << 1));
  REQUIRE(first.predicted == first_inputs.prev_band_sample);

  modules::CtrlSignals ctrl{};
  ctrl.first_line = false;
  ctrl.first_in_line = false;
  ctrl.scale_exponent = -4;

  modules::PredictorInputs general_inputs{};
  general_inputs.ctrl = ctrl;
  general_inputs.depth = 12;
  general_inputs.omega = 4;
  general_inputs.rbits = 16;
  general_inputs.prev_band_sample = -1;
  general_inputs.local_sum = 3;
  general_inputs.numerator = (int64_t{1} << (general_inputs.omega + 1));
  const auto general = modules::predictor(general_inputs);
  REQUIRE(general.scaled_pred > 0);
  REQUIRE(general.predicted == static_cast<int32_t>(general.scaled_pred >> 1));
}

TEST_CASE("weight_update_resets_and_tracks_error_sign") {
  std::vector<int32_t> weights(4, 5);
  const std::array<int32_t, 4> diffs{6, 4, 2, 0};

  modules::CtrlSignals reset_ctrl{};
  reset_ctrl.first_line = true;
  reset_ctrl.first_in_line = true;
  modules::WeightUpdateInputs reset_inputs{reset_ctrl, 4, 4, -6, 9, 0, 0, diffs};
  modules::weight_update(weights, reset_inputs, false);
  const auto expected_reset = modules::init_weights(false, 4, static_cast<int>(weights.size()));
  REQUIRE(weights == expected_reset);

  modules::CtrlSignals ctrl{};
  ctrl.scale_exponent = 0;
  ctrl.first_line = false;
  ctrl.first_in_line = false;

  modules::WeightUpdateInputs increase_inputs{ctrl, 4, 4, -6, 9, 16, 12, diffs};
  modules::weight_update(weights, increase_inputs, false);
  const auto after_increase = weights;
  REQUIRE(std::all_of(after_increase.begin(), after_increase.begin() + 3, [](int32_t v) { return v > 0; }));

  modules::WeightUpdateInputs decrease_inputs{ctrl, 4, 4, -6, 9, 32, 0, diffs};
  modules::weight_update(weights, decrease_inputs, false);
  for (std::size_t i = 0; i < after_increase.size(); ++i) {
    REQUIRE(weights[i] <= after_increase[i]);
  }
}

TEST_CASE("residual_mapping_invertibility") {
  modules::CtrlSignals ctrl{};
  ctrl.first_line = false;
  ctrl.first_in_line = false;
  ctrl.last_in_line = false;
  ctrl.last = false;
  ctrl.scale_exponent = 0;

  for (int scaled_pred = -512; scaled_pred <= 512; scaled_pred += 17) {
    for (int residual = -256; residual <= 256; ++residual) {
      const int depth = 12;
      const int32_t predicted = static_cast<int32_t>(scaled_pred >> 1);
      const int32_t sample = std::clamp(predicted + residual, 0, (1 << depth) - 1);
      const modules::ResidualMapperInputs inputs{ctrl, depth, sample, scaled_pred};
      const auto mapped = modules::residual_map(inputs);
      const auto recon = modules::residual_unmap(mapped.delta, scaled_pred, depth);
      REQUIRE(recon == mapped.residual);
    }
  }
}

TEST_CASE("control_sequence_scans_bip_order") {
  const int nx = 4;
  const int ny = 3;
  const int nz = 2;
  modules::ControlState control({nx, ny, nz, -6, 9, 4});
  std::vector<std::tuple<int, int, int>> visited;
  visited.reserve(static_cast<std::size_t>(nx) * ny * nz);
  std::vector<int> scale_exponents;
  for (int y = 0; y < ny; ++y) {
    for (int x = 0; x < nx; ++x) {
      for (int z = 0; z < nz; ++z) {
        const auto out = control.step();
        visited.emplace_back(x, y, out.z);
        scale_exponents.push_back(out.ctrl.scale_exponent);
      }
    }
  }
  REQUIRE(visited.front() == std::make_tuple(0, 0, 0));
  REQUIRE(visited.back() == std::make_tuple(nx - 1, ny - 1, nz - 1));
  for (int t = 0; t < nx * ny; ++t) {
    for (int z = 0; z < nz; ++z) {
      const auto &entry = visited[static_cast<std::size_t>(t * nz + z)];
      REQUIRE(std::get<0>(entry) == t % nx);
      REQUIRE(std::get<1>(entry) == t / nx);
      REQUIRE(std::get<2>(entry) == z);
    }
  }
  REQUIRE(scale_exponents.front() == -6);
  REQUIRE(std::all_of(scale_exponents.begin(), scale_exponents.end(), [](int v) {
    return v >= -6 && v <= 9;
  }));
  REQUIRE(std::is_sorted(scale_exponents.begin(), scale_exponents.end()));
}

