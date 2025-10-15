#include "modules/residual_map.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

#include "modules/detail/bit_math.hpp"

namespace modules {
namespace {

struct StageOneResult {
    std::int64_t residual{};
    std::uint64_t magnitude{};
    std::int64_t theta{};
    std::int64_t scaled_prediction{};
    bool scaled_prediction_is_odd{};
};

StageOneResult compute_stage_one(std::int64_t sample,
                                 std::int64_t scaled_prediction,
                                 std::size_t dynamic_range_bits) {
    if (dynamic_range_bits == 0) {
        throw std::invalid_argument("Dynamic range must be greater than zero");
    }

    const auto prediction = detail::arithmetic_shift_right(scaled_prediction, 1U);
    const std::int64_t lower_bound = -(std::int64_t{1} << (dynamic_range_bits - 1));
    const std::int64_t upper_bound = (std::int64_t{1} << (dynamic_range_bits - 1)) - 1;

    const std::int64_t distance_to_lower = prediction - lower_bound;
    const std::int64_t distance_to_upper = upper_bound - prediction;
    const std::int64_t theta = std::min(distance_to_lower, distance_to_upper);
    if (theta < 0) {
        throw std::logic_error("Computed theta is negative");
    }

    const std::int64_t residual = sample - prediction;
    const std::uint64_t magnitude = static_cast<std::uint64_t>(std::llabs(residual));

    StageOneResult stage{};
    stage.residual = residual;
    stage.magnitude = magnitude;
    stage.theta = theta;
    stage.scaled_prediction = scaled_prediction;
    stage.scaled_prediction_is_odd = (scaled_prediction & 1) != 0;
    return stage;
}

std::uint64_t compute_stage_two(const StageOneResult& stage) {
    const auto theta_u = static_cast<std::uint64_t>(stage.theta);
    if (stage.magnitude > theta_u) {
        return stage.magnitude + theta_u;
    }

    const bool residual_non_negative = stage.residual >= 0;
    const bool residual_non_positive = stage.residual <= 0;

    if ((!stage.scaled_prediction_is_odd && residual_non_negative) ||
        (stage.scaled_prediction_is_odd && residual_non_positive)) {
        return stage.magnitude << 1U;
    }

    return (stage.magnitude << 1U) - 1U;
}

}  // namespace

ResidualMapResult residual_map(std::int64_t sample,
                               std::int64_t scaled_prediction,
                               std::size_t dynamic_range_bits) {
    const auto stage = compute_stage_one(sample, scaled_prediction, dynamic_range_bits);
    const auto delta = compute_stage_two(stage);

    ResidualMapResult result{};
    result.delta = delta;
    result.magnitude = stage.magnitude;
    result.side.theta = stage.theta;
    result.side.scaled_prediction = stage.scaled_prediction;
    return result;
}

}  // namespace modules
