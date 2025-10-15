#include "modules/residual_unmap.hpp"

#include <cstdlib>
#include <stdexcept>

#include "modules/detail/bit_math.hpp"

namespace modules {
namespace {

std::int64_t distance_to_lower_bound(std::int64_t prediction,
                                     std::size_t dynamic_range_bits) {
    const std::int64_t lower = -(std::int64_t{1} << (dynamic_range_bits - 1));
    return prediction - lower;
}

}  // namespace

std::int64_t residual_unmap(std::uint64_t delta,
                            const ResidualSideInfo& side,
                            std::size_t dynamic_range_bits) {
    if (dynamic_range_bits == 0) {
        throw std::invalid_argument("Dynamic range must be greater than zero");
    }

    const auto prediction = detail::arithmetic_shift_right(side.scaled_prediction, 1U);
    const auto theta = side.theta;
    if (theta < 0) {
        throw std::logic_error("Theta side information must be non-negative");
    }
    const auto two_theta = static_cast<std::uint64_t>(theta) << 1U;

    if (delta > two_theta) {
        const auto magnitude = static_cast<std::int64_t>(delta - theta);
        const auto lower_distance = distance_to_lower_bound(prediction, dynamic_range_bits);
        const auto residual_positive = theta == lower_distance;
        return residual_positive ? magnitude : -magnitude;
    }

    const bool delta_is_even = (delta % 2U) == 0U;
    const auto magnitude = delta_is_even ? static_cast<std::int64_t>(delta / 2U)
                                         : static_cast<std::int64_t>((delta + 1U) / 2U);

    if (magnitude == 0) {
        return 0;
    }

    const bool scaled_prediction_is_odd = (side.scaled_prediction & 1) != 0;
    const bool residual_positive = (delta_is_even && !scaled_prediction_is_odd) ||
                                   (!delta_is_even && scaled_prediction_is_odd);
    return residual_positive ? magnitude : -magnitude;
}

}  // namespace modules
