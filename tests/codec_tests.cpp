#define CATCH_CONFIG_MAIN
#include "external/catch2/catch.hpp"

#include "Ccsds123Codec.hpp"
#include "ccsds123/modules.hpp"

#include <algorithm>
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

