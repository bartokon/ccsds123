#include "external/catch2/catch.hpp"

#include "ccsds123/modules.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using ccsds123::modules::CtrlSignals;
using ccsds123::modules::LocalDiffOutput;
using ccsds123::modules::LocalSamples;

namespace {

struct VectorCase {
  bool column_oriented{};
  CtrlSignals ctrl{};
  LocalSamples samples{};
  LocalDiffOutput expected{};
};

std::filesystem::path vector_path() {
  return std::filesystem::path(PROJECT_SOURCE_DIR) / "tests" / "data" / "local_diff_vectors.csv";
}

VectorCase parse_case(const std::string &line) {
  std::stringstream ss(line);
  std::string cell;
  VectorCase row{};
  auto read_bool = [](const std::string &value) -> bool { return std::stoi(value) != 0; };

  std::getline(ss, cell, ',');
  row.column_oriented = read_bool(cell);
  std::getline(ss, cell, ',');
  row.ctrl.first_line = read_bool(cell);
  std::getline(ss, cell, ',');
  row.ctrl.first_in_line = read_bool(cell);
  std::getline(ss, cell, ',');
  row.ctrl.last_in_line = read_bool(cell);
  std::getline(ss, cell, ',');
  row.samples.cur = std::stoi(cell);
  std::getline(ss, cell, ',');
  row.samples.north = std::stoi(cell);
  std::getline(ss, cell, ',');
  row.samples.north_east = std::stoi(cell);
  std::getline(ss, cell, ',');
  row.samples.north_west = std::stoi(cell);
  std::getline(ss, cell, ',');
  row.samples.west = std::stoi(cell);
  std::getline(ss, cell, ',');
  row.expected.local_sum = std::stoi(cell);
  std::getline(ss, cell, ',');
  row.expected.d_c = std::stoi(cell);
  std::getline(ss, cell, ',');
  row.expected.d_n = std::stoi(cell);
  std::getline(ss, cell, ',');
  row.expected.d_nw = std::stoi(cell);
  std::getline(ss, cell, ',');
  row.expected.d_w = std::stoi(cell);
  row.ctrl.last = false;
  row.ctrl.scale_exponent = 0;
  return row;
}

std::vector<VectorCase> load_cases() {
  std::ifstream file(vector_path());
  REQUIRE(file.is_open());
  std::string header;
  std::getline(file, header);
  std::vector<VectorCase> cases;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty()) {
      cases.push_back(parse_case(line));
    }
  }
  REQUIRE_FALSE(cases.empty());
  return cases;
}

} // namespace

TEST_CASE("local_diff_matches_python_reference") {
  const auto cases = load_cases();
  for (const auto &test_case : cases) {
    const auto actual = ccsds123::modules::local_diff(test_case.ctrl, test_case.samples, test_case.column_oriented);
    CAPTURE(test_case.column_oriented, test_case.ctrl.first_line, test_case.ctrl.first_in_line,
            test_case.ctrl.last_in_line, test_case.samples.cur, test_case.samples.north,
            test_case.samples.north_east, test_case.samples.north_west, test_case.samples.west);
    REQUIRE(actual.local_sum == test_case.expected.local_sum);
    REQUIRE(actual.d_c == test_case.expected.d_c);
    REQUIRE(actual.d_n == test_case.expected.d_n);
    REQUIRE(actual.d_nw == test_case.expected.d_nw);
    REQUIRE(actual.d_w == test_case.expected.d_w);
  }
}
