module;
#include <aubio/types.h>

#include <aubio/fvec.h>
#include <aubio/onset/onset.h>
#include <aubio/pitch/pitch.h>
#include <aubio/tempo/tempo.h>
#include <pipewire/core.h>
#include <pipewire/keys.h>
#include <pipewire/loop.h>
#include <pipewire/main-loop.h>
#include <pipewire/pipewire.h>
#include <pipewire/port.h>
#include <pipewire/properties.h>
#include <pipewire/stream.h>
#include <spa/param/audio/format.h>
#include <spa/param/audio/raw-utils.h>
#include <spa/param/param.h>
#include <spa/pod/builder.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <print>
#include <ranges>
#include <stop_token>
#include <string_view>
#include <thread>
#include <vector>

module beat.detector;

import :aubio_raii;
import :pw_raii;
import audio.blocks;
import u8fmt;
import icons;

using namespace pw_raii;
using namespace aubio_raii;

namespace beat {

namespace {

constexpr std::uint32_t kSampleRate = 44100U;  // REVIEW: Configurable?
constexpr std::uint32_t kChannels   = 1U;

void featureLine(std::string_view label, bool enabled, std::u8string_view icon) {
    auto u8_icon = u8fmt::wrapU8string(icon);
    std::print("\t{} {}: {}\n",
               u8_icon,
               label,
               enabled ? u8fmt::wrapU8string(icons::kCheck) : u8fmt::wrapU8string(icons::kFail));
}

}  // namespace

class DetectorState {
public:
    pw_raii::MainLoopPtr main_loop {nullptr};
    pw_raii::StreamPtr   stream {nullptr};

    aubio_raii::TempoPtr tempo {nullptr};
    aubio_raii::FVecPtr  input_vector {nullptr};
    aubio_raii::FVecPtr  output_vector {nullptr};
    aubio_raii::OnsetPtr onset {nullptr};
    aubio_raii::PitchPtr pitch {nullptr};
    aubio_raii::FVecPtr  pitch_buffer {nullptr};

    // TODO: maybe make this private
    const std::uint32_t buffer_size;
    const std::uint32_t fft_size;
    bool                log_enabled;
    bool                stats_enabled;
    bool                pitch_enabled;
    bool                visual_enabled;

    std::ofstream                         log;
    std::vector<double>                   processing_times_ms;
    std::uint64_t                         total_beats {0}, total_onsets {0};
    std::chrono::steady_clock::time_point start, last_beat;
    float                                 last_bpm {0.F};

    static constexpr std::size_t kBPMCapacity = 10U;

    /*
     * Real-time (RT) -> Mainloop communication
     *
     * Implements a lock-free single-producer/single-consumer (SPSC) event queue
     * to pass analysis results from the real-time audio based thread into the PipeWire mainloop.
     *
     * Events are produced in the RT thread (beat/onset detection, BPM, pitch, etc.) and consumed
     * in the mainloop via a PipeWire loop event source (`event_src`).
     *
     * Synchronization uses atomics for head/tail indices: RT pushes to `ev_head`, the mainloop
     * consumes from `ev_tail`. This avoids locks, which are unsafe in real-time contexts.
     *
     * Teardown: `stopping` signals shutdown in progress, and `quit_monitor` observes quit requests
     * to exit the mainloop without signal-unsafe calls.
     */
    struct Event {
        bool   is_beat;
        bool   is_onset;
        float  bpm;
        float  pitch_hz;
        double process_ms;
    };

    static constexpr std::size_t kEventCap = 1024U;
    std::array<Event, kEventCap> events {};
    std::atomic<std::size_t>     ev_head {0U};         // write index (Real-time)
    std::atomic<std::size_t>     ev_tail {0U};         // read index (mainloop)
    spa_source*                  event_src {nullptr};  // pw_loop_add_event

    // Stop/teardown coordination
    std::atomic_bool stopping {false};

    // Monitor thread to observe 'quit' and quit mainloop safely (no signal-unsafe calls)
    std::jthread quit_monitor;

    struct BPMBuffer {
        std::array<float, kBPMCapacity> values {};
        std::size_t                     count {0U};
        std::size_t                     head {0U};
    };

    BPMBuffer bpm {};

    inline static std::atomic_bool quit {false};
    inline static DetectorState*   instance {nullptr};

    DetectorState(std::uint32_t buffer_size_in,
                  bool          enable_logging,
                  bool          enable_stats,
                  bool          enable_pitch_detection,
                  bool          enable_visualization)
        : buffer_size(buffer_size_in)
        , fft_size(buffer_size_in * 2)
        , log_enabled(enable_logging)
        , stats_enabled(enable_stats)
        , pitch_enabled(enable_pitch_detection)
        , visual_enabled(enable_visualization) {
        instance     = this;
        start        = std::chrono::steady_clock::now();
        // Spawn a tiny monitor that quits the mainloop when 'quit' flips
        quit_monitor = std::jthread([this](const std::stop_token& stop_token) -> void {
            using namespace std::chrono_literals;
            while (!stop_token.stop_requested()) {
                if (DetectorState::quit.load(std::memory_order_relaxed)) {
                    if (this->main_loop != nullptr) {
                        pw_main_loop_quit(this->main_loop.get());
                    }
                    break;
                }
                std::this_thread::sleep_for(50ms);
            }
        });
    }

    ~DetectorState() {
        instance = nullptr;
    }
};

// PIMPL
struct BeatDetector::Impl {
    std::unique_ptr<DetectorState> state;
};

BeatDetector::BeatDetector(std::uint32_t buffer_size,
                           bool          enable_logging,
                           bool          enable_performance_stats,
                           bool          enable_pitch_detection,
                           bool          enable_visual_feedback)
    : impl_(std::make_unique<Impl>()) {
    impl_->state = std::make_unique<DetectorState>(buffer_size,
                                                   enable_logging,
                                                   enable_performance_stats,
                                                   enable_pitch_detection,
                                                   enable_visual_feedback);
}

BeatDetector::~BeatDetector() {
    auto& current_state = *impl_->state;
    if (current_state.stats_enabled) {
        const auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::steady_clock::now() - current_state.start)
                                  .count();
        std::println("\n{} Final Statistics:", u8fmt::wrapU8string(icons::kStats));
        std::println("\t{} Total runtime: {} seconds",
                     u8fmt::wrapU8string(icons::kRuntime),
                     duration);
        std::println("\t{} Total beat detected: {}",
                     u8fmt::wrapU8string(icons::kNote),
                     current_state.total_beats);
        std::println("\t{} Total onsets detected: {}",
                     u8fmt::wrapU8string(icons::kNote),
                     current_state.total_onsets);
    }

    if (!current_state.processing_times_ms.empty()) {
        const auto [min_processing_it, max_processing_it] =
            std::ranges::minmax_element(current_state.processing_times_ms);
        const double average_processing_time =
            std::accumulate(current_state.processing_times_ms.begin(),
                            current_state.processing_times_ms.end(),
                            0.0)
            / static_cast<double>(current_state.processing_times_ms.size());

        std::println("\t{} Average processing time: {:.3F} ms",
                     u8fmt::wrapU8string(icons::kBolt),
                     average_processing_time);
        std::println("\t{} Max processing time: {:.3F} ms",
                     u8fmt::wrapU8string(icons::kUpChart),
                     *max_processing_it);
        std::println("\t{} Min processing time: {:.3F} ms",
                     u8fmt::wrapU8string(icons::kDownChart),
                     *min_processing_it);
    }

    if (auto& state_bpm = current_state.bpm; state_bpm.count > 0) {
        auto index_range = std::views::iota(std::size_t {0}, state_bpm.count);

        auto bpm_value_view = index_range | std::views::transform([&state_bpm](std::size_t index) {
                                  const std::size_t capacity = DetectorState::kBPMCapacity;
                                  const std::size_t first_index =
                                      (state_bpm.head + capacity - state_bpm.count) % capacity;
                                  return state_bpm.values[(first_index + index) % capacity];
                              });

        const float total_bpm = std::accumulate(bpm_value_view.begin(), bpm_value_view.end(), 0.0F);
        const float average_bpm = total_bpm / static_cast<float>(state_bpm.count);

        std::println("\t{} Final average BPM: {:.1F}",
                     u8fmt::wrapU8string(icons::kBpm),
                     average_bpm);
    }

    if (current_state.log.is_open()) {
        current_state.log.close();
    }

    impl_->state.reset();
    pw_deinit();
    std::println("\n{} Cleanup complete - All resources freed!",
                 u8fmt::wrapU8string(icons::kCheck));
}

[[nodiscard]] static auto averageBpm(const DetectorState& state) {
    const auto& state_bpm = state.bpm;

    if (state.bpm.count == 0) {
        return 0.0F;
    }

    const auto bpm_value_view = std::views::iota(std::size_t {0}, state_bpm.count)
                                | std::views::transform([&](std::size_t index) {
                                      const std::size_t capacity = DetectorState::kBPMCapacity;
                                      const std::size_t first_index =
                                          (state_bpm.head + capacity - state_bpm.count) % capacity;
                                      return state_bpm.values[(first_index + index) % capacity];
                                  });

    const float total_bpm   = std::accumulate(bpm_value_view.begin(), bpm_value_view.end(), 0.0F);
    const float average_bpm = total_bpm / static_cast<float>(state_bpm.count);
    return average_bpm;
}

auto BeatDetector::initialize() -> std::expected<void, std::string> {
    auto& current_state = *impl_->state;
    pw_init(nullptr, nullptr);

    if (current_state.log_enabled) {
        const auto current_time     = std::chrono::system_clock::now();
        const auto utc_current_time = std::chrono::clock_cast<std::chrono::utc_clock>(current_time);

        const std::filesystem::path log_file =
            std::format("beat_log_{:%Y%m%d_%H%M%S}Z.txt", utc_current_time);
        current_state.log.open(log_file, std::ios::out | std::ios::trunc);

        if (!current_state.log.is_open()) {
            return std::unexpected("failed to open log file");
        }

        std::println("{} Logging to: {}", u8fmt::wrapU8string(icons::kCircle), log_file.string());
        std::println(current_state.log, "# Beat Detection Log - {:%F %T}", utc_current_time);
        std::println(current_state.log, "# Timestamp,BPM,Onset,Pitch(Hz),ProcessTime(ms)");
    }

    current_state.main_loop.reset(pw_main_loop_new(nullptr));
    if (current_state.main_loop == nullptr) {
        return std::unexpected("failed to create main loop");
    }

    // NOTE: we let pw_stream_new_simple create its own context/core under the hood

    current_state.tempo.reset(
        new_aubio_tempo("default", current_state.fft_size, current_state.buffer_size, kSampleRate));
    if (current_state.tempo == nullptr) {
        return std::unexpected("failed to create aubio tempo");
    }

    current_state.input_vector.reset(new_fvec(current_state.buffer_size));
    current_state.output_vector.reset(new_fvec(1U));
    if (current_state.input_vector == nullptr || current_state.output_vector == nullptr) {
        return std::unexpected("failed to create aubio buffers");
    }

    // TODO: not entirely sure these parameters are correct, double check
    current_state.onset.reset(
        new_aubio_onset("default", current_state.fft_size, current_state.buffer_size, kSampleRate));
    if (current_state.onset == nullptr) {
        return std::unexpected("failed to create aubio onset");
    }

    if (current_state.pitch_enabled) {
        current_state.pitch.reset(new_aubio_pitch("default",
                                                  current_state.fft_size,
                                                  current_state.buffer_size,
                                                  kSampleRate));
        current_state.pitch_buffer.reset(new_fvec(1U));

        if (current_state.pitch == nullptr || current_state.pitch_buffer == nullptr) {
            return std::unexpected("failed to create aubio pitch");
        }
        aubio_pitch_set_unit(current_state.pitch.get(), "Hz");
    }

    static const pw_stream_events
        events {.version = PW_VERSION_STREAM_EVENTS,  // behave clang-format
                .destroy = +[](void* userdata) noexcept -> void {
                    auto* state = static_cast<DetectorState*>(userdata);
                    if (state != nullptr && state->stream != nullptr) {
                        // The stream is being destroyed by PipeWire right now
                        // Drop ownership without calling the deleter again
                        (void) state->stream.release();
                    }
                },

                // Force the lambda to decay to a function pointer with +[] (needed for C callback)
                .state_changed = +[](void* userdata,
                                     pw_stream_state /*old*/,
                                     pw_stream_state state,
                                     const char*     error) noexcept -> void {
                    auto* event_state = static_cast<DetectorState*>(userdata);

                    std::println("{} Stream state: {}",
                                 icons::pw::iconFor(state),
                                 pw_stream_state_as_string(state));

                    if (state == PW_STREAM_STATE_ERROR) {
                        std::println(std::cerr,
                                     "{} Stream error: {}",
                                     u8fmt::wrapU8string(icons::kFail),
                                     error ? error : "unknown");
                        if (event_state != nullptr && event_state->main_loop != nullptr) {
                            pw_main_loop_quit(event_state->main_loop.get());
                        }
                    }

                    // If we requested stop, disconnect once paused to avoid RT rac
                    if (state == PW_STREAM_STATE_PAUSED) {
                        if (event_state != nullptr
                            && event_state->stopping.load(std::memory_order_relaxed)) {
                            if (event_state->stream != nullptr) {
                                pw_stream_disconnect(event_state->stream.get());
                            }
                        }
                    }
                },
                .control_info  = nullptr,
                .io_changed    = nullptr,
                .param_changed = nullptr,
                .add_buffer    = nullptr,
                .remove_buffer = nullptr,

                .process = +[](void* userdata) noexcept -> void {
                    using Clock = std::chrono::steady_clock;

                    auto* process_state = static_cast<DetectorState*>(userdata);
                    if (process_state == nullptr
                        || beat::DetectorState::quit.load(std::memory_order_relaxed)) {
                        return;
                    }

                    if (auto* pw_buf = pw_stream_dequeue_buffer(process_state->stream.get());
                        pw_buf) {
                        // Make sure we always re-queue the buffer , even on early returns
                        struct BufferLease {
                            pw_stream* stream {};
                            pw_buffer* buffer {};

                            ~BufferLease() {
                                if (stream != nullptr && buffer != nullptr) {
                                    pw_stream_queue_buffer(stream, buffer);
                                }
                            }
                        } lease {.stream = process_state->stream.get(), .buffer = pw_buf};

                        if (auto* spa_buf = pw_buf->buffer; spa_buf != nullptr
                                                            && spa_buf->datas[0].data != nullptr
                                                            && spa_buf->datas[0].chunk != nullptr) {

                            auto process_view = [&](const audio_blocks::BufferView<float>& view)
                                -> std::expected<void, audio_blocks::ViewError> {
                                for (auto block : view.blocks()) {
                                    auto* destination =
                                        fvec_get_data(process_state->input_vector.get());

                                    std::ranges::copy(block, destination);

                                    aubio_tempo_do(process_state->tempo.get(),
                                                   process_state->input_vector.get(),
                                                   process_state->output_vector.get());
                                    const bool is_beat =
                                        process_state->output_vector->data[0] != 0.0F;

                                    aubio_onset_do(process_state->onset.get(),
                                                   process_state->input_vector.get(),
                                                   process_state->output_vector.get());
                                    const bool is_onset =
                                        process_state->output_vector->data[0] != 0.0F;

                                    float pitch_hz = 0.0F;
                                    if (process_state->pitch_enabled) {
                                        aubio_pitch_do(process_state->pitch.get(),
                                                       process_state->input_vector.get(),
                                                       process_state->pitch_buffer.get());
                                        pitch_hz = process_state->pitch_buffer->data[0];
                                    }

                                    // Real-time only bookkeeping
                                    bool  produced_event = false;
                                    float bpm_now        = process_state->last_bpm;

                                    if (is_beat) {
                                        ++process_state->total_beats;

                                        bpm_now = aubio_tempo_get_bpm(process_state->tempo.get());
                                        process_state->last_bpm  = bpm_now;
                                        process_state->last_beat = Clock::now();

                                        auto& bpm_buffer                   = process_state->bpm;
                                        bpm_buffer.values[bpm_buffer.head] = bpm_now;
                                        bpm_buffer.head =
                                            (bpm_buffer.head + 1) % DetectorState::kBPMCapacity;
                                        bpm_buffer.count = std::min(bpm_buffer.count + 1,
                                                                    DetectorState::kBPMCapacity);

                                        produced_event = true;
                                    }

                                    if (is_onset) {
                                        ++process_state->total_onsets;
                                        produced_event = true;
                                    }

                                    if (produced_event) {
                                        // Push to the SPSC ring buffer, overwriting the oldest if
                                        // full
                                        const auto head =
                                            process_state->ev_head.load(std::memory_order_relaxed);
                                        const auto tail =
                                            process_state->ev_tail.load(std::memory_order_acquire);

                                        auto next_head = (head + 1) % DetectorState::kEventCap;
                                        // Drop the oldest if full
                                        if (next_head == tail) {
                                            process_state->ev_tail
                                                .store((tail + 1) % DetectorState::kEventCap,
                                                       std::memory_order_release);
                                        }

                                        process_state->events[head] = DetectorState::Event {
                                            .is_beat    = is_beat,
                                            .is_onset   = is_onset,
                                            .bpm        = bpm_now,
                                            .pitch_hz   = pitch_hz,
                                            .process_ms = 0.0  // this is filled later
                                        };
                                        process_state->ev_head.store(next_head,
                                                                     std::memory_order_release);
                                        if (process_state->event_src != nullptr) {
                                            auto* main_loop = pw_main_loop_get_loop(
                                                process_state->main_loop.get());
                                            pw_loop_signal_event(main_loop,
                                                                 process_state->event_src);
                                        }
                                    }
                                }
                                return {};  // success
                            };

                            // Build a single bounded view over the whole SPA buffer
                            [[maybe_unused]] auto view_res =
                                audio_blocks::makeBufferViewFromSpaMonoF32(spa_buf,
                                                                           process_state
                                                                               ->buffer_size)
                                    .and_then(process_view)
                                    .or_else([&](audio_blocks::ViewError error)
                                                 -> std::expected<void, audio_blocks::ViewError> {
                                        std::println(stderr,
                                                     "SPA buffer rejected: {}",
                                                     audio_blocks::toString(error));
                                        return std::unexpected {error};
                                    });
                        }
                    }
                },

                .drained      = nullptr,
                .command      = nullptr,
                .trigger_done = nullptr};

    auto  properties     = pw_raii::makeAudioCaptureProperties();
    auto* raw_properties = properties.release();  // ownership passed to PipeWire on success

    auto* raw_stream = pw_stream_new_simple(pw_main_loop_get_loop(current_state.main_loop.get()),
                                            "beat-detector",
                                            raw_properties,  // ownership passed to PipeWire
                                            &events,
                                            &current_state);

    if (raw_stream == nullptr) {
        // Creation failed: free properties (we released ownership earlier)
        if (raw_properties != nullptr) {
            pw_properties_free(raw_properties);
        }
        return std::unexpected("failed to  create stream");
    }

    // On success we keep the stream and let PipeWire own the properties
    current_state.stream.reset(raw_stream);

    std::array<std::uint8_t, 1024> buffer {};
    spa_pod_builder                builder = SPA_POD_BUILDER_INIT(buffer.data(), buffer.size());

    spa_audio_info_raw audio_info {};
    audio_info.format   = SPA_AUDIO_FORMAT_F32_LE;  // REVIEW: might want to make this portable
    audio_info.channels = kChannels;
    audio_info.rate     = kSampleRate;
    audio_info.flags    = 0;

    auto params = std::to_array<const spa_pod*>(
        {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &audio_info)});

    if (pw_stream_connect(current_state.stream.get(),
                          PW_DIRECTION_INPUT,
                          PW_ID_ANY,
                          static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT
                                                       | PW_STREAM_FLAG_MAP_BUFFERS
                                                       | PW_STREAM_FLAG_RT_PROCESS),
                          params.data(),
                          params.size())
        < 0) {
        // If the connect fails we destroy the stream to avoid leaking it
        current_state.stream.reset();
        return std::unexpected("failed to connect to stream");
    }

    // Create a mainloop event to drain the real-time events and perform IO safely
    current_state.event_src = pw_loop_add_event(
        pw_main_loop_get_loop(current_state.main_loop.get()),
        +[](void* userdata, std::uint64_t /*count*/) -> void {
            auto* state = static_cast<DetectorState*>(userdata);
            if (state == nullptr) {
                return;
            }

            // Drain the SPSC
            for (;;) {
                const auto tail = state->ev_tail.load(std::memory_order_acquire);
                const auto head = state->ev_head.load(std::memory_order_acquire);
                // Break if empty
                if (tail == head) {
                    break;
                }

                const auto event = state->events[tail];
                state->ev_tail.store((tail + 1) % DetectorState::kEventCap,
                                     std::memory_order_release);

                // Stats accumulation (mainloop side)
                // Per-event timing does not exist (yet?)
                if (event.is_beat) {
                    if (state->visual_enabled) {
                        const auto intensity =
                            std::clamp(static_cast<int>(event.bpm / 20.0F), 0, 10);
                        std::print("\r{}", u8fmt::wrapU8string(icons::kMusic));

                        for (int i = 0; i < intensity; ++i) {
                            std::print("{}", u8fmt::wrapU8string(icons::kBlock));
                        }

                        for (int i = 0; i < 10; ++i) {
                            std::print("{}", u8fmt::wrapU8string(icons::kLight));
                        }

                        std::print(" BPM: {:.1f} | Avg {:.1f}", event.bpm, averageBpm(*state));
                        std::fflush(stdout);
                    } else {
                        std::println(" BPM: {:.1f}", event.bpm);
                    }
                }

                if (state->log_enabled && state->log.is_open()
                    && (event.is_beat || event.is_onset)) {
                    const auto current_time = std::chrono::system_clock::now();
                    const auto current_time_ms =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            current_time.time_since_epoch())
                        % 1000;
                    std::println(state->log,
                                 "{:%T}.{:03},{:.1f},{},{:.3f},",
                                 current_time,
                                 static_cast<int>(current_time_ms.count()),
                                 (event.is_beat ? event.bpm : 0.0F),
                                 event.is_onset ? 1 : 0,
                                 event.pitch_hz);
                }
            }
        },
        &current_state);

    return {};
}

void BeatDetector::run() {
    auto& current_state = *impl_->state;
    if (current_state.main_loop == nullptr) {
        return;
    }

    DetectorState::quit.store(false, std::memory_order_relaxed);

    std::println("\n{} Beat Detector Started!", u8fmt::wrapU8string(icons::kBpm));
    std::println("\t Buffer size: {} samples", current_state.buffer_size);
    std::println("\tSample rate: {} Hz", kSampleRate);
    std::println("\tFeatures enabled:");

    featureLine("Logging", current_state.log_enabled, icons::kCircle);
    featureLine("Performance", current_state.stats_enabled, icons::kStats);
    featureLine("Pitch", current_state.pitch_enabled, icons::kPitch);
    featureLine("Visual", current_state.visual_enabled, icons::kCircle);

    std::println("\n{} Listening for beats... Press Ctrl+C to stop.\n",
                 u8fmt::wrapU8string(icons::kNote));

    pw_main_loop_run(current_state.main_loop.get());
}

void BeatDetector::stop() noexcept {
    auto& current_state = *impl_->state;
    DetectorState::quit.store(true, std::memory_order_relaxed);

    if (current_state.stream != nullptr) {
        // Step 1: ask PipeWire to stop scheduling .process
        current_state.stopping.store(true, std::memory_order_relaxed);
        pw_stream_set_active(current_state.stream.get(), false);
        // Step 2: in state_changed(PAUSED) we will pw_stream_disconnect()
    }
}

void BeatDetector::signalHandler(int) noexcept {
    if (DetectorState::instance != nullptr) {
        // Async-signal safe: only set the flag
        beat::DetectorState::quit.store(true, std::memory_order_relaxed);
    }
}

}  // namespace beat
