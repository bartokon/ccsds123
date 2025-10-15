#include "Ccsds123Codec.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ccsds123::Bitstream;
using ccsds123::ImageU16;
using ccsds123::Params;

struct CliOptions {
  std::string input_path;
  std::string output_path;
};

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::vector<std::string_view> split_args(int argc, char **argv) {
  std::vector<std::string_view> args;
  args.reserve(static_cast<std::size_t>(argc));
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }
  return args;
}

CliOptions parse_cli(int argc, char **argv) {
  CliOptions opts;
  const auto args = split_args(argc, argv);
  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto &arg = args[i];
    if (arg == "-i" && i + 1 < args.size()) {
      opts.input_path = std::string(args[++i]);
    } else if (arg == "-o" && i + 1 < args.size()) {
      opts.output_path = std::string(args[++i]);
    } else if (arg == "-h" || arg == "--help") {
      std::cout << "Usage: ccsds123_decode -i <input> -o <output>\n";
      std::exit(0);
    }
  }
  require(!opts.input_path.empty(), "Missing -i input path");
  require(!opts.output_path.empty(), "Missing -o output path");
  return opts;
}

std::vector<std::uint8_t> read_file(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  require(in.good(), "Unable to open input file");
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_bsq(const std::string &path, const ImageU16 &image, const Params &params) {
  const std::size_t samples = static_cast<std::size_t>(params.NX) * params.NY * params.NZ;
  std::vector<std::uint8_t> bytes(samples * sizeof(std::uint16_t));
  for (std::size_t i = 0; i < samples; ++i) {
    bytes[i * 2] = static_cast<std::uint8_t>(image[i] & 0xFFu);
    bytes[i * 2 + 1] = static_cast<std::uint8_t>((image[i] >> 8) & 0xFFu);
  }
  std::ofstream out(path, std::ios::binary);
  require(out.good(), "Unable to open output file");
  out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main(int argc, char **argv) {
  try {
    const auto opts = parse_cli(argc, argv);
    Bitstream bitstream;
    const auto bytes = read_file(opts.input_path);
    for (std::uint8_t byte : bytes) {
      bitstream.push_back(byte);
    }
    const auto &const_bitstream = static_cast<const Bitstream &>(bitstream);
    const auto summary = ccsds123::read_summary(const_bitstream.bytes());

    Params params = summary.params;
    ImageU16 image;
    decode(const_bitstream, image, params);
    write_bsq(opts.output_path, image, params);
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "ccsds123_decode: " << ex.what() << "\n";
    return 1;
  }
}

