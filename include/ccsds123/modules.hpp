#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace ccsds123::modules {

struct CtrlSignals {
  bool first_line{};
  bool first_in_line{};
  bool last_in_line{};
  bool last{};
  int scale_exponent{};
};

struct ControlConfig {
  int nx{};
  int ny{};
  int nz{};
  int v_min{};
  int v_max{};
  int tinc_log{};
};

struct ControlState {
  explicit ControlState(const ControlConfig &cfg) noexcept;

  struct Output {
    CtrlSignals ctrl{};
    int z{};
  };

  [[nodiscard]] Output step();

private:
  ControlConfig cfg_{};
  int x_{0};
  int y_{0};
  int z_{0};
  int t_{0};
};

struct LocalSamples {
  int32_t cur{};
  int32_t north{};
  int32_t north_east{};
  int32_t north_west{};
  int32_t west{};
};

struct LocalDiffOutput {
  int32_t local_sum{};
  int32_t d_c{};
  int32_t d_n{};
  int32_t d_w{};
  int32_t d_nw{};
};

LocalDiffOutput local_diff(const CtrlSignals &ctrl, const LocalSamples &samples, bool column_oriented);

int64_t dot_product(std::span<const int32_t> diffs, std::span<const int32_t> weights);

std::vector<int32_t> init_weights(bool reduced, int omega, int components);

struct PredictorInputs {
  CtrlSignals ctrl;
  int depth{};
  int omega{};
  int rbits{};
  int prev_band_sample{};
  int64_t numerator{};
  int32_t local_sum{};
};

struct PredictorResult {
  int32_t predicted{};
  int64_t scaled_pred{};
};

PredictorResult predictor(const PredictorInputs &inputs);

struct WeightUpdateInputs {
  CtrlSignals ctrl;
  int depth{};
  int omega{};
  int v_min{};
  int v_max{};
  int64_t scaled_pred{};
  int32_t sample{};
  std::span<const int32_t> diffs;
};

void weight_update(std::span<int32_t> weights, const WeightUpdateInputs &inputs, bool reduced);

struct ResidualMapperInputs {
  CtrlSignals ctrl;
  int depth{};
  int32_t sample{};
  int64_t scaled_pred{};
};

struct ResidualMapperResult {
  std::uint32_t delta{};
  int32_t residual{};
  int32_t theta{};
};

ResidualMapperResult residual_map(const ResidualMapperInputs &inputs);
std::int32_t residual_unmap(std::uint32_t delta, int64_t scaled_pred, int depth);

} // namespace ccsds123::modules
