#pragma once

#include <cstddef>
#include <cstdint>

namespace modules {

struct ResidualSideInfo {
    std::int64_t theta{};
    std::int64_t scaled_prediction{};
};

struct ResidualMapResult {
    std::uint64_t delta{};
    std::uint64_t magnitude{};
    ResidualSideInfo side{};
};

ResidualMapResult residual_map(std::int64_t sample,
                               std::int64_t scaled_prediction,
                               std::size_t dynamic_range_bits);

}  // namespace modules
