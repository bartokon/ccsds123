#include "ccsds123/modules.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ccsds123::modules {

namespace {

inline int64_t clip_int64(int64_t value, int64_t lo, int64_t hi) noexcept {
  return std::min(std::max(value, lo), hi);
}

inline int64_t sign_extend(int64_t value, int bits) noexcept {
  if (bits <= 0 || bits >= 64) {
    return value;
  }
  const int64_t mask = (int64_t{1} << bits) - 1;
  value &= mask;
  const int64_t sign_bit = int64_t{1} << (bits - 1);
  if (value & sign_bit) {
    return value - (int64_t{1} << bits);
  }
  return value;
}

inline int64_t mod_pow2(int64_t value, int bits) noexcept {
  if (bits <= 0) {
    return 0;
  }
  if (bits >= 64) {
    return value;
  }
  const int64_t mask = (int64_t{1} << bits) - 1;
  return sign_extend(value & mask, bits);
}

inline int32_t theta_from_pred(int32_t pred, int depth) noexcept {
  const int32_t half = int32_t{1} << (depth - 1);
  const int32_t lo = pred + half;
  const int32_t hi = (half - 1) - pred;
  return std::min(lo, hi);
}

inline bool is_even(int64_t value) noexcept { return (value & 1LL) == 0; }

} // namespace

ControlState::ControlState(const ControlConfig &cfg) noexcept : cfg_(cfg) {}

ControlState::Output ControlState::step() {
  ControlState::Output out{};
  out.z = z_;
  out.ctrl.first_line = y_ == 0;
  out.ctrl.first_in_line = x_ == 0;
  out.ctrl.last_in_line = x_ == cfg_.nx - 1;
  out.ctrl.last = out.ctrl.last_in_line && y_ == cfg_.ny - 1 && z_ == cfg_.nz - 1;
  out.ctrl.scale_exponent = cfg_.v_min;
  const int limit = (cfg_.v_max - cfg_.v_min);
  const int t_adjusted = t_ - cfg_.nx;
  if (t_adjusted <= 0) {
    out.ctrl.scale_exponent = cfg_.v_min;
  } else if (t_adjusted >= (limit << cfg_.tinc_log)) {
    out.ctrl.scale_exponent = cfg_.v_max;
  } else {
    out.ctrl.scale_exponent = cfg_.v_min + (t_adjusted >> cfg_.tinc_log);
  }

  if (z_ + 1 > cfg_.nz - 1) {
    z_ = 0;
    const int spatial_limit = cfg_.nx * cfg_.ny;
    if (spatial_limit > 0) {
      t_ = (t_ + 1) % spatial_limit;
    } else {
      ++t_;
    }
    if (x_ == cfg_.nx - 1) {
      x_ = 0;
      if (y_ == cfg_.ny - 1) {
        y_ = 0;
      } else {
        ++y_;
      }
    } else {
      ++x_;
    }
  } else {
    ++z_;
  }

  return out;
}

LocalDiffOutput local_diff(const CtrlSignals &ctrl, const LocalSamples &samples, bool column_oriented) {
  LocalDiffOutput out{};
  int32_t term1 = 0;
  int32_t term2 = 0;
  if (column_oriented) {
    if (!ctrl.first_line) {
      term1 = 4 * samples.north;
    } else {
      term1 = 4 * samples.west;
    }
  } else {
    if (!ctrl.first_line && !ctrl.first_in_line && !ctrl.last_in_line) {
      term1 = samples.west + samples.north_west;
      term2 = samples.north + samples.north_east;
    } else if (ctrl.first_line && !ctrl.first_in_line) {
      term1 = 4 * samples.west;
    } else if (!ctrl.first_line && ctrl.first_in_line) {
      term1 = 2 * samples.north;
      term2 = 2 * samples.north_east;
    } else if (!ctrl.first_line && ctrl.last_in_line) {
      term1 = samples.west + samples.north_west;
      term2 = 2 * samples.north;
    }
  }
  out.local_sum = term1 + term2;
  if (ctrl.first_line && ctrl.first_in_line) {
    out.d_c = 0;
    out.local_sum = 0;
  } else {
    out.d_c = 4 * samples.cur - out.local_sum;
  }

  if (!ctrl.first_line) {
    out.d_n = 4 * samples.north - out.local_sum;
  } else {
    out.d_n = 0;
  }

  if (!ctrl.first_line) {
    if (!ctrl.first_in_line) {
      out.d_w = 4 * samples.west - out.local_sum;
      out.d_nw = 4 * samples.north_west - out.local_sum;
    } else {
      out.d_w = 4 * samples.north - out.local_sum;
      out.d_nw = 4 * samples.north - out.local_sum;
    }
  } else {
    out.d_w = 0;
    out.d_nw = 0;
  }

  return out;
}

int64_t dot_product(std::span<const int32_t> diffs, std::span<const int32_t> weights) {
  int64_t sum = 0;
  const std::size_t n = std::min(diffs.size(), weights.size());
  for (std::size_t i = 0; i < n; ++i) {
    sum += static_cast<int64_t>(diffs[i]) * static_cast<int64_t>(weights[i]);
  }
  return sum;
}

std::vector<int32_t> init_weights(bool reduced, int omega, int components) {
  std::vector<int32_t> weights(static_cast<std::size_t>(components), 0);
  const int p = reduced ? components : components - 3;
  if (p > 0) {
    const int32_t base = static_cast<int32_t>((int64_t{7} * (int64_t{1} << omega)) / 8);
    weights[0] = base;
    for (int i = 1; i < p; ++i) {
      weights[static_cast<std::size_t>(i)] = weights[static_cast<std::size_t>(i - 1)] / 8;
    }
  }
  if (!reduced) {
    for (int i = std::max(0, components - 3); i < components; ++i) {
      weights[static_cast<std::size_t>(i)] = 0;
    }
  }
  return weights;
}

PredictorResult predictor(const PredictorInputs &inputs) {
  PredictorResult result{};
  const int64_t loc_term = static_cast<int64_t>(inputs.local_sum) << inputs.omega;
  const int64_t temp = inputs.numerator + loc_term;
  const int64_t numerator = mod_pow2(temp, inputs.rbits);
  if (inputs.ctrl.first_line && inputs.ctrl.first_in_line) {
    if (inputs.prev_band_sample >= 0) {
      result.scaled_pred = static_cast<int64_t>(inputs.prev_band_sample) << 1;
    } else {
      result.scaled_pred = 0;
    }
  } else {
    const int64_t shifted = numerator >> (inputs.omega + 1);
    const int64_t candidate = shifted + 1;
    const int64_t clipped = clip_int64(candidate, -(int64_t{1} << inputs.depth), (int64_t{1} << inputs.depth) - 1);
    result.scaled_pred = clipped;
  }
  result.predicted = static_cast<int32_t>(result.scaled_pred >> 1);
  return result;
}

void weight_update(std::span<int32_t> weights, const WeightUpdateInputs &inputs, bool reduced) {
  if (weights.empty()) {
    return;
  }
  if (inputs.ctrl.first_line && inputs.ctrl.first_in_line) {
    auto init = init_weights(reduced, inputs.omega, static_cast<int>(weights.size()));
    std::copy(init.begin(), init.end(), weights.begin());
    return;
  }

  const bool non_negative_error = (static_cast<int64_t>(inputs.sample) << 1) >= inputs.scaled_pred;
  const int shift = inputs.ctrl.scale_exponent + (inputs.depth - inputs.omega);
  const int32_t limit = 1 << (inputs.omega + 2);
  for (std::size_t i = 0; i < weights.size() && i < inputs.diffs.size(); ++i) {
    int64_t diff = static_cast<int64_t>(inputs.diffs[i]);
    if (!non_negative_error) {
      diff = -diff;
    }
    int64_t adjusted = diff;
    if (shift > 0) {
      adjusted >>= shift;
    } else if (shift < 0) {
      adjusted <<= -shift;
    }
    const int64_t mux = (adjusted + 1) >> 1;
    const int64_t updated = static_cast<int64_t>(weights[i]) + mux;
    weights[i] = static_cast<int32_t>(clip_int64(updated, -static_cast<int64_t>(limit), static_cast<int64_t>(limit) - 1));
  }
}

ResidualMapperResult residual_map(const ResidualMapperInputs &inputs) {
  ResidualMapperResult result{};
  const int32_t pred = static_cast<int32_t>(inputs.scaled_pred >> 1);
  const int32_t residual = inputs.sample - pred;
  const int32_t theta = theta_from_pred(pred, inputs.depth);
  const int32_t abs_res = std::abs(residual);
  const bool pred_even = is_even(inputs.scaled_pred);
  if (abs_res > theta) {
    result.delta = static_cast<std::uint32_t>(abs_res + theta);
  } else {
    const std::uint32_t magnitude = static_cast<std::uint32_t>(abs_res);
    const bool cond = (pred_even && residual >= 0) || (!pred_even && residual <= 0);
    if (cond) {
      result.delta = magnitude << 1U;
    } else {
      result.delta = (magnitude << 1U) - 1U;
    }
  }
  result.residual = residual;
  result.theta = theta;
  return result;
}

std::int32_t residual_unmap(std::uint32_t delta, int64_t scaled_pred, int depth) {
  const int32_t pred = static_cast<int32_t>(scaled_pred >> 1);
  const int32_t theta = theta_from_pred(pred, depth);
  const bool pred_even = is_even(scaled_pred);
  int32_t residual = 0;
  if (delta > static_cast<std::uint32_t>(theta * 2)) {
    const int32_t magnitude = static_cast<int32_t>(delta) - theta;
    residual = pred_even ? magnitude : -magnitude;
  } else if ((delta & 1U) == 0U) {
    const int32_t magnitude = static_cast<int32_t>(delta >> 1U);
    residual = pred_even ? magnitude : -magnitude;
  } else {
    const int32_t magnitude = static_cast<int32_t>((delta + 1U) >> 1U);
    residual = pred_even ? -magnitude : magnitude;
  }
  const int32_t offset = 1 << (depth - 1);
  const int32_t min_val = -offset;
  const int32_t max_val = offset - 1;
  const int32_t sample = pred + residual;
  if (sample < min_val || sample > max_val) {
    residual = -residual;
  }
  return residual;
}

} // namespace ccsds123::modules
