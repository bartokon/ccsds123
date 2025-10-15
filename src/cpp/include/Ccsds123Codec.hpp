#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include "ccsds123/modules.hpp"

namespace ccsds123 {

using ImageU16 = std::vector<std::uint16_t>;

namespace detail {
inline void bitstream_overflow() {
#ifdef __AIE__
  __builtin_trap();
#else
  throw std::runtime_error("Bitstream external buffer overflow");
#endif
}
} // namespace detail

class Bitstream {
public:
  Bitstream() = default;
  Bitstream(std::uint8_t *external, std::size_t capacity) noexcept : external_(external, capacity) {}

  void clear() noexcept;
  void reserve(std::size_t count);
  void resize(std::size_t count);
  void push_back(std::uint8_t value);
  void set(std::size_t index, std::uint8_t value);
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] bool empty() const noexcept { return size() == 0; }
  [[nodiscard]] std::span<std::uint8_t> bytes() noexcept;
  [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;

private:
  [[nodiscard]] bool uses_external() const noexcept { return external_.data() != nullptr; }

  std::vector<std::uint8_t> owned_{};
  std::span<std::uint8_t> external_{};
  std::size_t external_size_{0};
};

inline void Bitstream::clear() noexcept {
  if (uses_external()) {
    external_size_ = 0;
  } else {
    owned_.clear();
  }
}

inline void Bitstream::reserve(std::size_t count) {
  if (!uses_external()) {
    owned_.reserve(count);
  }
}

inline void Bitstream::resize(std::size_t count) {
  if (uses_external()) {
    if (count > external_.size()) {
      detail::bitstream_overflow();
      count = external_.size();
    }
    external_size_ = count;
  } else {
    owned_.resize(count);
  }
}

inline void Bitstream::push_back(std::uint8_t value) {
  if (uses_external()) {
    if (external_size_ >= external_.size()) {
      detail::bitstream_overflow();
      return;
    }
    external_[external_size_++] = value;
  } else {
    owned_.push_back(value);
  }
}

inline void Bitstream::set(std::size_t index, std::uint8_t value) {
  if (uses_external()) {
    if (index >= external_.size()) {
      detail::bitstream_overflow();
      return;
    }
    if (index >= external_size_) {
      external_size_ = index + 1;
    }
    external_[index] = value;
  } else {
    owned_[index] = value;
  }
}

inline std::size_t Bitstream::size() const noexcept {
  return uses_external() ? external_size_ : owned_.size();
}

inline std::span<std::uint8_t> Bitstream::bytes() noexcept {
  if (uses_external()) {
    return {external_.data(), external_size_};
  }
  return {owned_.data(), owned_.size()};
}

inline std::span<const std::uint8_t> Bitstream::bytes() const noexcept {
  if (uses_external()) {
    return {external_.data(), external_size_};
  }
  return {owned_.data(), owned_.size()};
}

struct SampleAdaptiveCoderParams {
  int u_max{18};
  int counter_size{6};
  int initial_count_exponent{1};
  int kz_prime{0};
};

struct Params {
  int NX{0};
  int NY{0};
  int NZ{0};
  int D{0};
  int P{0};

  bool reduced{false};
  bool column_oriented{false};

  enum class LocalSumMode { NeighborWide, NeighborNarrow, ColumnWide, ColumnNarrow };
  LocalSumMode local_sum{LocalSumMode::NeighborNarrow};

  int theta{0};
  std::vector<int> phi{};
  std::vector<int> psi{};
  std::vector<int> az{};
  std::vector<int> rz{};

  int omega{19};
  int register_bits{64};
  int v_min{-1};
  int v_max{3};
  int tinc_log{6};

  SampleAdaptiveCoderParams coder{};
};

void encode(std::span<const std::uint16_t> input, Bitstream &out, const Params &params);
void encode(const ImageU16 &input, Bitstream &out, const Params &params);
void decode(const Bitstream &in, std::span<std::uint16_t> output, const Params &params);
void decode(const Bitstream &in, ImageU16 &output, const Params &params);

struct ContainerSummary {
  Params params;
  std::size_t payload_bits{};
};

ContainerSummary read_summary(std::span<const std::uint8_t> container_bytes);

} // namespace ccsds123

