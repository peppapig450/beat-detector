module;  // global module fragment to ensure C header are private to this unit
#include <pipewire/context.h>
#include <pipewire/core.h>
#include <pipewire/keys.h>
#include <pipewire/main-loop.h>
#include <pipewire/properties.h>
#include <pipewire/stream.h>
#include <spa/utils/dict.h>

#include <array>
#include <memory>

export module beat.detector:pw_raii;

namespace pw_raii {

struct MainLoopDeleter {
    void operator()(pw_main_loop* loop) const noexcept {
        if (loop != nullptr) {
            pw_main_loop_destroy(loop);
        }
    }
};

struct ContextDeleter {
    void operator()(pw_context* ctx) const noexcept {
        if (ctx != nullptr) {
            pw_context_destroy(ctx);
        }
    }
};

struct CoreDeleter {
    void operator()(pw_core* core) const noexcept {
        if (core != nullptr) {
            pw_core_disconnect(core);
        }
    }
};

struct StreamDeleter {
    void operator()(pw_stream* stream) const noexcept {
        if (stream != nullptr) {
            pw_stream_destroy(stream);
        }
    }
};

using MainLoopPtr = std::unique_ptr<pw_main_loop, MainLoopDeleter>;
using ContextPtr  = std::unique_ptr<pw_context, ContextDeleter>;
using CorePtr     = std::unique_ptr<pw_core, CoreDeleter>;
using StreamPtr   = std::unique_ptr<pw_stream, StreamDeleter>;

struct PropertiesDeleter {
    void operator()(pw_properties* props) const noexcept {
        if (props != nullptr) {
            pw_properties_free(props);
        }
    }
};

using PropertiesPtr = std::unique_ptr<pw_properties, PropertiesDeleter>;

[[nodiscard]] inline auto makeAudioCaptureProperties() noexcept -> PropertiesPtr {
    static constexpr auto dict_items = std::to_array<spa_dict_item>({
        spa_dict_item {.key = PW_KEY_MEDIA_TYPE, .value = "Audio"},
        spa_dict_item {.key = PW_KEY_MEDIA_CATEGORY, .value = "Capture"},
        spa_dict_item {.key = PW_KEY_MEDIA_ROLE, .value = "Music"},
    });

    static constexpr spa_dict dict = SPA_DICT_INIT(dict_items.data(), dict_items.size());
    return PropertiesPtr {pw_properties_new_dict(&dict)};
}

}  // namespace pw_raii
