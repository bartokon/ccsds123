#include "Ccsds123Codec.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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
  int nx{};
  int ny{};
  int nz{3};
  int d{8};
  bool force_ppm{false};
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
    } else if (arg == "-nx" && i + 1 < args.size()) {
      opts.nx = std::stoi(std::string(args[++i]));
    } else if (arg == "-ny" && i + 1 < args.size()) {
      opts.ny = std::stoi(std::string(args[++i]));
    } else if (arg == "-nz" && i + 1 < args.size()) {
      opts.nz = std::stoi(std::string(args[++i]));
    } else if (arg == "-d" && i + 1 < args.size()) {
      opts.d = std::stoi(std::string(args[++i]));
    } else if (arg == "--ppm") {
      opts.force_ppm = true;
    } else if (arg == "-h" || arg == "--help") {
      std::cout << "Usage: ccsds123_encode -i <input> -o <output> -nx <X> -ny <Y> -nz <Z> -d <bits>\n";
      std::cout << "       Use --ppm to read binary PPM (P6). Dimensions inferred from header.\n";
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

ImageU16 load_bsq(const CliOptions &opts, int &nx, int &ny, int &nz, int &d) {
  nx = opts.nx;
  ny = opts.ny;
  nz = opts.nz;
  d = opts.d;
  const auto bytes = read_file(opts.input_path);
  require(nx > 0 && ny > 0 && nz > 0, "Invalid dimensions for BSQ input");
  const std::size_t samples = static_cast<std::size_t>(nx) * ny * nz;
  require(bytes.size() == samples * sizeof(std::uint16_t),
          "BSQ input size does not match dimensions");
  ImageU16 img(samples);
  for (std::size_t i = 0; i < samples; ++i) {
    const std::size_t offset = i * 2;
    img[i] = static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
  }
  return img;
}

ImageU16 load_ppm(const CliOptions &opts, int &nx, int &ny, int &nz, int &d) {
  auto bytes = read_file(opts.input_path);
  const std::string_view data(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  require(data.size() >= 2 && data.substr(0, 2) == "P6", "PPM must be binary P6");
  std::size_t pos = 2;
  auto skip_ws = [&]() {
    while (pos < data.size() && std::isspace(static_cast<unsigned char>(data[pos]))) {
      ++pos;
    }
  };
  auto read_token = [&]() -> std::string {
    while (true) {
      skip_ws();
      if (pos >= data.size()) {
        throw std::runtime_error("Unexpected EOF in PPM header");
      }
      if (data[pos] == '#') {
        while (pos < data.size() && data[pos] != '\n') {
          ++pos;
        }
        continue;
      }
      std::size_t start = pos;
      while (pos < data.size() && !std::isspace(static_cast<unsigned char>(data[pos]))) {
        ++pos;
      }
      return std::string(data.substr(start, pos - start));
    }
  };
  nx = std::stoi(read_token());
  ny = std::stoi(read_token());
  nz = 3;
  d = std::stoi(read_token());
  skip_ws();
  require(pos < data.size(), "Missing pixel data in PPM");
  const std::size_t pixels = static_cast<std::size_t>(nx) * ny;
  const std::size_t expected_bytes = pixels * 3 * (d > 255 ? 2 : 1);
  require(pos + expected_bytes <= data.size(), "PPM pixel data truncated");
  ImageU16 img(pixels * 3);
  if (d > 255) {
    for (std::size_t i = 0; i < pixels * 3; ++i) {
      const std::size_t offset = pos + i * 2;
      img[i] = static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[offset]) << 8) |
                                          static_cast<std::uint8_t>(data[offset + 1]));
    }
    d = 16;
  } else {
    for (std::size_t i = 0; i < pixels * 3; ++i) {
      img[i] = static_cast<std::uint8_t>(data[pos + i]);
    }
    d = 8;
  }
  ImageU16 bsq(img.size());
  for (int band = 0; band < 3; ++band) {
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
      bsq[static_cast<std::size_t>(band) * pixels + pixel] =
          img[pixel * 3 + static_cast<std::size_t>(band)];
    }
  }
  return bsq;
}

ImageU16 load_image(const CliOptions &opts, int &nx, int &ny, int &nz, int &d) {
  if (opts.force_ppm || std::filesystem::path(opts.input_path).extension() == ".ppm" ||
      std::filesystem::path(opts.input_path).extension() == ".PPM") {
    return load_ppm(opts, nx, ny, nz, d);
  }
  return load_bsq(opts, nx, ny, nz, d);
}

void write_file(const std::filesystem::path &path, const ccsds123::Bitstream &bitstream) {
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }
  const auto data = bitstream.bytes();
  std::ofstream out(path, std::ios::binary);
  require(out.good(), "Unable to open output file");
  out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
}

} // namespace

int main(int argc, char **argv) {
  try {
    auto opts = parse_cli(argc, argv);
    const std::filesystem::path input_path(opts.input_path);
    std::filesystem::path output_path(opts.output_path);

    auto encode_single = [&](const std::filesystem::path &in_file, const std::filesystem::path &out_file) {
      CliOptions local_opts = opts;
      local_opts.input_path = in_file.string();
      int nx = 0;
      int ny = 0;
      int nz = 0;
      int d = 0;
      const auto image = load_image(local_opts, nx, ny, nz, d);

      Params params;
      params.NX = nx;
      params.NY = ny;
      params.NZ = nz;
      params.D = d;
      params.P = 0;
      params.local_sum = Params::LocalSumMode::NeighborNarrow;
      params.theta = 0;

      Bitstream bitstream;
      encode(image, bitstream, params);
      write_file(out_file, bitstream);
    };

    if (std::filesystem::is_directory(input_path)) {
      require(!opts.force_ppm, "Directory inputs do not support --ppm");
      if (std::filesystem::exists(output_path)) {
        require(std::filesystem::is_directory(output_path),
                "Output path must be a directory when encoding a sequence");
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
      require(!inputs.empty(), "No input frames found in directory");
      for (const auto &file : inputs) {
        auto out_file = output_path / file.stem();
        out_file.replace_extension(".c123");
        encode_single(file, out_file);
      }
    } else {
      std::filesystem::path out_file = output_path;
      if (std::filesystem::is_directory(out_file)) {
        out_file /= input_path.stem();
        out_file.replace_extension(".c123");
      } else if (out_file.extension().empty()) {
        out_file.replace_extension(".c123");
      }
      encode_single(input_path, out_file);
    }
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "ccsds123_encode: " << ex.what() << "\n";
    return 1;
  }
}

