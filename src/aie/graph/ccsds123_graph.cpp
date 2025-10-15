#include "Ccsds123Codec.hpp"

#ifdef __AIE__
#include <adf.h>

extern "C" {
void compress_aie(input_window<int16_t> *restrict input_samples,
                  output_stream<uint32_t> *restrict output_bytes,
                  const ccsds123::Params &params);
void decompress_aie(input_stream<uint32_t> *restrict input_bytes,
                    output_window<int16_t> *restrict output_samples,
                    const ccsds123::Params &params);
}

namespace ccsds123::aie {

struct Graph : public adf::graph {
  adf::kernel compress_kernel;
  adf::kernel decompress_kernel;

  Graph() {
    compress_kernel = adf::kernel::create(compress_aie);
    decompress_kernel = adf::kernel::create(decompress_aie);
  }
};

} // namespace ccsds123::aie

#else

namespace ccsds123::aie {

struct Graph {
  Graph() = default;
};

} // namespace ccsds123::aie

#endif
