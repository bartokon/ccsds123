#include "Ccsds123Codec.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <filesystem>
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

std::vector<std::uint8_t> read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  require(in.good(), "Unable to open input file");
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_bsq(const std::filesystem::path &path, const ImageU16 &image, const Params &params) {
  const std::size_t samples = static_cast<std::size_t>(params.NX) * params.NY * params.NZ;
  std::vector<std::uint8_t> bytes(samples * sizeof(std::uint16_t));
  for (std::size_t i = 0; i < samples; ++i) {
    bytes[i * 2] = static_cast<std::uint8_t>(image[i] & 0xFFu);
    bytes[i * 2 + 1] = static_cast<std::uint8_t>((image[i] >> 8) & 0xFFu);
  }
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }
  std::ofstream out(path, std::ios::binary);
  require(out.good(), "Unable to open output file");
  out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main(int argc, char **argv) {
  try {
    const auto opts = parse_cli(argc, argv);
    const std::filesystem::path input_path(opts.input_path);
    std::filesystem::path output_path(opts.output_path);

    auto decode_single = [&](const std::filesystem::path &in_file, const std::filesystem::path &out_file) {
      Bitstream bitstream;
      const auto bytes = read_file(in_file);
      for (std::uint8_t byte : bytes) {
        bitstream.push_back(byte);
      }
      const auto &const_bitstream = static_cast<const Bitstream &>(bitstream);
      const auto summary = ccsds123::read_summary(const_bitstream.bytes());

      Params params = summary.params;
      ImageU16 image;
      decode(const_bitstream, image, params);
      write_bsq(out_file, image, params);
    };

    if (std::filesystem::is_directory(input_path)) {
      if (std::filesystem::exists(output_path)) {
        require(std::filesystem::is_directory(output_path),
                "Output path must be a directory when decoding a sequence");
      } else {
        std::filesystem::create_directories(output_path);
      }
      std::vector<std::filesystem::path> inputs;
      for (const auto &entry : std::filesystem::directory_iterator(input_path)) {
        if (entry.is_regular_file()) {
          inputs.push_back(entry.path());
        }
      }
      std::sort(inputs.begin(), inputs.end());
      require(!inputs.empty(), "No input containers found in directory");
      for (const auto &file : inputs) {
        auto out_file = output_path / file.stem();
        out_file.replace_extension(".bsq");
        decode_single(file, out_file);
      }
    } else {
      std::filesystem::path out_file = output_path;
      if (std::filesystem::is_directory(out_file)) {
        out_file /= input_path.stem();
        out_file.replace_extension(".bsq");
      } else if (out_file.extension().empty()) {
        out_file.replace_extension(".bsq");
      }
      decode_single(input_path, out_file);
    }
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "ccsds123_decode: " << ex.what() << "\n";
    return 1;
  }
}

