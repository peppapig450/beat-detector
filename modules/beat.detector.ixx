module;
#include <cstdint>
#include <expected>
#include <memory>
#include <string>

export module beat.detector;

export import :aubio_raii;
export import :pw_raii;

export namespace beat {

class BeatDetector {
public:
    static constexpr std::uint32_t kDefaultBufferSize = 512U;

    explicit BeatDetector(std::uint32_t buffer_size              = kDefaultBufferSize,
                          bool          enable_logging           = true,
                          bool          enable_performance_stats = true,
                          bool          enable_pitch_detection   = false,
                          bool          enable_visual_feedback   = true);
    ~BeatDetector();

    BeatDetector(const BeatDetector&)                    = delete;
    auto operator=(const BeatDetector&) -> BeatDetector& = delete;

    [[nodiscard]] auto initialize() -> std::expected<void, std::string>;
    void               run();
    void               stop() noexcept;
    static void        signalHandler(int) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace beat
