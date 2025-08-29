import beat.detector;

#include <cctype>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <iostream>
#include <limits>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace detail {

[[nodiscard]] auto programName(std::span<std::string_view> args) -> std::string {
    if (args.empty()) {
        return "beat_cli";
    }

    namespace fs = std::filesystem;
    return fs::path(args.front()).filename().string();
}

void printUsage(std::string_view argv0) {
    std::println(" Beat Detector Usage:");
    std::println(" {} [buffer_size] [options]\n", argv0);
    std::println("Options:");
    std::println("\t--no-log\t\t\tDisable logging to file");
    std::println("\t--no-stats\t\t\tDisable performance statistics");
    std::println("\t--pitch\t\t\tEnable pitch detection");
    std::println("\t--no-visual\t\t\tDisable visual feedback");
    std::println("\t--help,-h\t\t\tShow this help\n");
}

// Parses a base-10 unsigned integer from a string_view with overflow checks.
// Returns {value, nullptr} on success, {0, "msg"} on failure.
static auto parseU32(std::string_view input) -> std::pair<std::uint32_t, const char*> {
    if (input.empty()) {
        return {0U, "empty"};
    }

    std::uint32_t           parsed_value        = 0;
    constexpr std::uint32_t max_before_multiply = std::numeric_limits<std::uint32_t>::max() / 10U;
    constexpr std::uint32_t max_last_digit      = std::numeric_limits<std::uint32_t>::max() % 10U;

    for (auto raw_char : input) {
        auto unsigned_char_value = static_cast<unsigned char>(raw_char);

        if (std::isdigit(unsigned_char_value) == 0) {
            return {0U, "nondigit"};
        }

        auto digit_value = static_cast<std::uint32_t>(unsigned_char_value - '0');
        if (parsed_value > max_before_multiply
            || (parsed_value == max_before_multiply && digit_value > max_last_digit)) {
            return {0U, "nondigit"};
        }

        parsed_value = (parsed_value * 10U) + digit_value;
    }

    return {parsed_value, nullptr};
}

}  // namespace detail
}  // namespace

namespace beat_detector {

constexpr std::uint32_t kDefaultBufferSize = 512U;

struct Options {
    std::uint32_t buffer_size {kDefaultBufferSize};
    bool          logging {true};
    bool          stats {true};
    bool          pitch {false};
    bool          visual {true};
};

constexpr std::uint32_t kMinBufferSize = 64U;
constexpr std::uint32_t kMaxBufferSize = 8192U;

struct ParseError {
    enum class Kind : std::uint8_t { Help, Invalid } kind {Kind::Invalid};
    std::string message;
};

[[nodiscard]] static auto parseArgs(std::span<std::string_view> args)
    -> std::expected<Options, ParseError> {
    Options options {};
    bool    saw_positional = false;

    // Skip program name (args[0]) if present
    for (std::size_t i = args.size() > 0U ? 1U : 0U; i < args.size(); ++i) {
        std::string_view arg = args[i];
        using enum ParseError::Kind;

        if (arg == "--help" || arg == "-h") {
            return std::unexpected {ParseError {.kind = Help, .message = {}}};
        }

        if (!arg.empty() && arg.front() != '-') {
            if (saw_positional) {
                return std::unexpected(ParseError {.kind    = Invalid,
                                                   .message = "Too many positional arguments (only "
                                                              "buffer_size is allowed)/"});
            }

            saw_positional = true;

            auto [end_ptr, parse_err] = detail::parseU32(arg);

            if (parse_err != nullptr) {
                return std::unexpected {
                    ParseError {.kind    = Invalid,
                                .message = "buffer_size must be a base-10 unsigned integer"}};
            }

            if (end_ptr < kMinBufferSize || end_ptr > kMaxBufferSize) {
                return std::unexpected {
                    ParseError {.kind = Invalid, .message = "buffer_size out of range [64, 8192]"}};
            }

            options.buffer_size = end_ptr;
            continue;
        }

        // clang-format off
        if (arg == "--no-log")    { options.logging = false; continue; }
        if (arg == "--no-stats")  { options.stats   = false; continue; }
        if (arg == "--pitch")     { options.pitch   = true;  continue; }
        if (arg == "--no-visual") { options.visual  = false; continue; }
        // clang-format on

        return std::unexpected {
            ParseError {.kind = Invalid, .message = std::format("Unknown option '{}'", arg)}};
    }

    return options;
}
}  // namespace beat_detector

auto main(int argc, char* argv[]) -> int {
    using beat::BeatDetector;

    auto raw_args = std::views::counted(argv, static_cast<std::ptrdiff_t>(argc));

    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (char* raw_string : raw_args) {
        args.emplace_back(raw_string);
    }

    const auto program_name = detail::programName(args);

    const auto parsed = beat_detector::parseArgs(args);
    if (!parsed) {
        if (parsed.error().kind == beat_detector::ParseError::Kind::Help) {
            detail::printUsage(program_name);
            return 0;
        }

        std::println(std::cerr, "{}", parsed.error().message);
        detail::printUsage(program_name);
        return 1;
    }

    const beat_detector::Options& options = *parsed;

    // Signal handlers provided by BeatDetector
    std::signal(SIGINT, &BeatDetector::signalHandler);
    std::signal(SIGTERM, &BeatDetector::signalHandler);

    BeatDetector detector(options.buffer_size,
                          options.logging,
                          options.stats,
                          options.pitch,
                          options.visual);

    if (auto is_ok = detector.initialize(); !is_ok) {
        std::println(std::cerr, "Init error: {}", is_ok.error());
        return 1;
    }

    try {
        detector.run();
    } catch (const std::exception& err) {
        std::println(std::cerr, "Error: {}", err.what());
        return 1;
    }

    return 0;
}
