#include "Ccsds123Codec.hpp"

#ifdef __AIE__
#include <adf.h>

#include <array>
#include <span>

extern "C" {

void compress_aie(input_window<int16_t> *restrict input_samples,
                  output_stream<uint32_t> *restrict output_bytes,
                  const ccsds123::Params &params) {
  constexpr std::size_t kMaxSamples = 4096;
  constexpr std::size_t kMaxBytes = kMaxSamples * 4;
  const std::size_t total = static_cast<std::size_t>(params.NX) * params.NY * params.NZ;
  if (total > kMaxSamples) {
    return;
  }

  std::array<std::uint16_t, kMaxSamples> image{};
  for (std::size_t i = 0; i < total; ++i) {
    image[i] = static_cast<std::uint16_t>(window_readincr(input_samples));
  }

  std::array<std::uint8_t, kMaxBytes> buffer{};
  ccsds123::Bitstream bitstream(buffer.data(), buffer.size());
  ccsds123::encode(std::span<const std::uint16_t>(image.data(), total), bitstream, params);
  const auto bytes = bitstream.bytes();
  for (std::size_t i = 0; i < bitstream.size(); ++i) {
    writeincr(output_bytes, static_cast<std::uint32_t>(bytes[i]));
  }
}

void decompress_aie(input_stream<uint32_t> *restrict input_bytes,
                    output_window<int16_t> *restrict output_samples,
                    const ccsds123::Params &params) {
  constexpr std::size_t kMaxSamples = 4096;
  constexpr std::size_t kMaxBytes = kMaxSamples * 4;
  std::array<std::uint8_t, kMaxBytes> buffer{};
  std::size_t count = 0;
  while (!stream_empty(input_bytes) && count < buffer.size()) {
    buffer[count++] = static_cast<std::uint8_t>(readincr(input_bytes) & 0xFFu);
  }
  ccsds123::Bitstream bitstream(buffer.data(), buffer.size());
  for (std::size_t i = 0; i < count; ++i) {
    bitstream.push_back(buffer[i]);
  }
  const std::size_t total = static_cast<std::size_t>(params.NX) * params.NY * params.NZ;
  std::array<std::uint16_t, kMaxSamples> image{};
  ccsds123::decode(bitstream, std::span<std::uint16_t>(image.data(), total), params);
  for (std::size_t i = 0; i < total; ++i) {
    window_writeincr(output_samples, static_cast<int16_t>(image[i]));
  }
}

} // extern "C"

#endif

