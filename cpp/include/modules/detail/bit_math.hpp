#pragma once

#include <cstdint>

namespace modules::detail {

inline std::int64_t arithmetic_shift_right(std::int64_t value, unsigned shift) {
    if (shift == 0 || value == 0) {
        return value;
    }

    const std::int64_t divisor = static_cast<std::int64_t>(std::int64_t{1} << shift);
    if (value > 0) {
        return value / divisor;
    }

    const std::int64_t adjusted = (-value + divisor - 1) / divisor;
    return -adjusted;
}

}  // namespace modules::detail
