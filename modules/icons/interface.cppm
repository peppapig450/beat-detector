module;
#include <pipewire/stream.h>

#include <print>
#include <string_view>

export module icons;

import u8fmt;

export namespace icons {

// UI / stats
inline constexpr std::u8string_view kStats   = u8"\U0000E64d";  // 
inline constexpr std::u8string_view kRuntime = u8"\U0001F3EB";  // 🏫
inline constexpr std::u8string_view kNote    = u8"\uf025";      //  (headphones)

// Status / charts
inline constexpr std::u8string_view kBolt      = u8"\u26A1";      // ⚡
inline constexpr std::u8string_view kUpChart   = u8"\U0001F4C8";  // 📈
inline constexpr std::u8string_view kDownChart = u8"\U0001F4C9";  // 📉

// Beat detector
inline constexpr std::u8string_view kBpm    = u8"\uf75a";  //  (metronome)
inline constexpr std::u8string_view kCheck  = u8"\uf14a";  //  (check square)
inline constexpr std::u8string_view kCircle = u8"\ueaaf";  //  (circle glyph)
inline constexpr std::u8string_view kFail   = u8"\uf467";  //  (cross/close)

// Misc
inline constexpr std::u8string_view kMusic = u8"\uf001";      //  (musical note)
inline constexpr std::u8string_view kBlock = u8"\u2588";      // █
inline constexpr std::u8string_view kLight = u8"\u2591";      // ░
inline constexpr std::u8string_view kPitch = u8"\U000F05C5";  // 󰗅

namespace pw {

constexpr auto iconFor(pw_stream_state state) noexcept -> u8fmt::U8StringViewWrapper {
    constexpr auto wrap = u8fmt::wrapU8string;

    // clang-format off
    switch (state) {
        case PW_STREAM_STATE_CONNECTING:  return wrap(u8"\U000F0119"); // 󰄙
        case PW_STREAM_STATE_PAUSED:      return wrap(u8"\uf04c");     // 
        case PW_STREAM_STATE_STREAMING:   return wrap(u8"\U000F076A"); // 󰝚
        case PW_STREAM_STATE_ERROR:       return wrap(u8"\uf46f");     // 
        case PW_STREAM_STATE_UNCONNECTED: return wrap(u8"\uead0");     // 
        default:                          return wrap(u8"\U000F0453"); // 󰑓
    }
    // clang-format on
}

}  // namespace pw

}  // namespace icons
