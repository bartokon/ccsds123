#include "modules/detail/bit_math.hpp"
#include "modules/residual_map.hpp"
#include "modules/residual_unmap.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct TestCase {
    std::int64_t dynamic_range_bits{};
    std::int64_t sample{};
    std::int64_t scaled_prediction{};
    std::int64_t expected_delta{};
};

std::int64_t clip(std::int64_t value, std::int64_t min, std::int64_t max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

std::vector<TestCase> load_cases(const std::filesystem::path& file_path) {
    std::ifstream input(file_path);
    if (!input) {
        throw std::runtime_error("Failed to open test vector file: " + file_path.string());
    }

    std::vector<TestCase> cases;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }

        std::istringstream stream(line);
        TestCase test_case{};
        if (!(stream >> test_case.dynamic_range_bits >> test_case.sample >> test_case.scaled_prediction >> test_case.expected_delta)) {
            throw std::runtime_error("Malformed line in test vector file: " + line);
        }
        cases.push_back(test_case);
    }

    return cases;
}

bool execute_case(const TestCase& test_case) {
    const auto prediction = modules::detail::arithmetic_shift_right(test_case.scaled_prediction, 1U);
    const auto expected_residual = test_case.sample - prediction;
    const auto lower_bound = -(std::int64_t{1} << (test_case.dynamic_range_bits - 1));
    const auto upper_bound = (std::int64_t{1} << (test_case.dynamic_range_bits - 1)) - 1;

    const auto result = modules::residual_map(test_case.sample,
                                              test_case.scaled_prediction,
                                              static_cast<std::size_t>(test_case.dynamic_range_bits));

    if (static_cast<std::int64_t>(result.delta) != test_case.expected_delta) {
        std::cerr << "Delta mismatch for sample " << test_case.sample
                  << ": expected " << test_case.expected_delta
                  << ", got " << result.delta << '\n';
        return false;
    }

    const auto expected_magnitude = static_cast<std::uint64_t>(std::llabs(expected_residual));
    if (result.magnitude != expected_magnitude) {
        std::cerr << "Magnitude mismatch for sample " << test_case.sample
                  << ": expected " << expected_magnitude
                  << ", got " << result.magnitude << '\n';
        return false;
    }

    const auto residual = modules::residual_unmap(result.delta,
                                                  result.side,
                                                  static_cast<std::size_t>(test_case.dynamic_range_bits));
    if (residual != expected_residual) {
        std::cerr << "Residual mismatch for sample " << test_case.sample
                  << ": expected " << expected_residual
                  << ", got " << residual << '\n';
        return false;
    }

    const auto reconstructed_sample = clip(prediction + residual, lower_bound, upper_bound);
    if (reconstructed_sample != test_case.sample) {
        std::cerr << "Sample reconstruction mismatch for sample " << test_case.sample
                  << ": expected " << test_case.sample
                  << ", got " << reconstructed_sample << '\n';
        return false;
    }

    const auto theta_expected = std::min(prediction - lower_bound, upper_bound - prediction);
    if (result.side.theta != theta_expected) {
        std::cerr << "Theta mismatch for sample " << test_case.sample
                  << ": expected " << theta_expected
                  << ", got " << result.side.theta << '\n';
        return false;
    }

    return true;
}

}  // namespace

int main() {
    try {
        const std::filesystem::path data_dir(TEST_DATA_DIR);
        const auto cases = load_cases(data_dir / "hdl_residual_map_edges.txt");

        bool all_passed = true;
        for (const auto& test_case : cases) {
            all_passed = execute_case(test_case) && all_passed;
        }

        if (!all_passed) {
            return EXIT_FAILURE;
        }

        std::cout << "All residual mapping tests passed (" << cases.size() << " cases).\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "Test execution failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
