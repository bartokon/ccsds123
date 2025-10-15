#pragma once

#include <cstddef>
#include <cstdint>

#include "modules/residual_map.hpp"

namespace modules {

std::int64_t residual_unmap(std::uint64_t delta,
                            const ResidualSideInfo& side,
                            std::size_t dynamic_range_bits);

}  // namespace modules
