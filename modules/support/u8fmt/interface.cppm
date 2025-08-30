module;
#include <format>
#include <string_view>

export module support.u8fmt;

export namespace u8fmt {
/// Wrapper type so we can legally specialize std::formatter
/// for a u8string_view-like object.
struct U8StringViewWrapper {
    std::u8string_view u8_string_view;
};

/// Convenience function for constructing the wrapper
constexpr auto wrapU8string(std::u8string_view input) noexcept -> U8StringViewWrapper {
    return {input};
}

}  // namespace u8fmt

// std::formatter specialization must be declared in namespace std
export template <> struct std::formatter<u8fmt::U8StringViewWrapper, char> {
    // Only "{}" is supported, ignore custom format specifiers for now.
    constexpr auto parse(std::format_parse_context& parse_context)
        -> decltype(parse_context.begin()) {
        return parse_context.begin();
    }

    template <class FormatContext>
    auto format(const u8fmt::U8StringViewWrapper& wrapper, FormatContext& format_context) const {
        // Convert char8_t view -> char-based string_view (no copy, no reinterpret)
        std::string_view utf8_bytes {reinterpret_cast<const char*>(wrapper.u8_string_view.data()),
                                     wrapper.u8_string_view.size()};

        // Forward formatting to the built-in std::string_view formatter
        return std::formatter<std::string_view, char> {}.format(utf8_bytes, format_context);
    }
};
