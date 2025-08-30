module;

#include <pipewire/stream.h>

export module support.icons:pw;

import support.u8fmt;

export namespace icons::pw {

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

}  // namespace icons::pw

