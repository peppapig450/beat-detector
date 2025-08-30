module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>

export module audio.blocks:core;

export namespace audio_blocks {

using namespace std::string_view_literals;

enum class ViewError : std::uint8_t {
    NullData,
    ZeroBlockSize,
    MisalignedBytes,
    UnsupportedStride,
};

[[nodiscard]] constexpr auto toString(ViewError error) noexcept -> std::string_view {
    switch (error) {
        using enum ViewError;
        // clang-format off
        case NullData:          return "null data pointer"sv;
        case ZeroBlockSize:     return "block size must be > 0"sv;
        case MisalignedBytes:   return "byte size is not a multiple of sample size"sv;
        case UnsupportedStride: return "unsupported stride/layout"sv;
        default:                return "unsupported error"sv;
        // clang-format on
    }
}

template <typename Sample>
    requires(std::is_trivially_copyable_v<Sample>)
class BufferView {
public:
    using SampleType = Sample;

    constexpr BufferView() = default;

    constexpr BufferView(std::span<const SampleType> samples,
                         std::size_t                 block_size_samples) noexcept
        : samples_(samples)
        , block_size_(block_size_samples) {}

    [[nodiscard]] constexpr auto samples() const noexcept -> std::span<const SampleType> {
        return samples_;
    }

    [[nodiscard]] constexpr auto blockSize() const noexcept -> std::size_t {
        return block_size_;
    }

    [[nodiscard]] constexpr auto empty() const noexcept -> bool {
        return samples_.empty();
    }

    [[nodiscard]] constexpr auto size() const noexcept -> std::size_t {
        return samples_.size();
    }

    class BlockRange {
    public:
        class Iterator {
        public:
            using ValueType      = std::span<const SampleType>;
            using DifferenceType = std::ptrdiff_t;

            constexpr Iterator(std::span<const SampleType> all,
                               std::size_t                 block,
                               std::size_t                 offset)
                : all_(all)
                , block_(block)
                , offset_(offset) {}

            [[nodiscard]] constexpr auto operator!=(const Iterator& rhs) const noexcept -> bool {
                return offset_ != rhs.offset_;
            }

            [[nodiscard]] constexpr auto operator*() const noexcept -> ValueType {
                return all_.subspan(offset_, block_);
            }

            constexpr auto operator++() noexcept -> Iterator& {
                offset_ += block_;
                return *this;
            }

        private:
            std::span<const SampleType> all_ {};
            std::size_t                 block_ {};
            std::size_t                 offset_ {};
        };

        constexpr BlockRange(std::span<const SampleType> all, std::size_t block) noexcept
            : all_(all)
            , block_(block) {}

        [[nodiscard]] constexpr auto begin() const noexcept -> Iterator {
            return Iterator {all_, block_, 0};
        }

        [[nodiscard]] constexpr auto end() const noexcept -> Iterator {
            const std::size_t usable = (all_.size() / block_) * block_;
            return Iterator {all_, block_, usable};
        }

    private:
        std::span<const SampleType> all_ {};
        std::size_t                 block_ {};
    };

    [[nodiscard]] constexpr auto blocks() const noexcept -> BlockRange {
        return BlockRange {samples_, block_size_};
    }

    [[nodiscard]] constexpr auto tailPartial() const noexcept -> std::span<const SampleType> {
        const auto tail_size = samples_.size() % block_size_;
        return tail_size ? samples_.last(tail_size) : std::span<const SampleType> {};
    }

private:
    std::span<const SampleType> samples_ {};
    std::size_t                 block_size_ {};
};

template <typename Sample>
    requires(std::is_trivially_copyable_v<Sample>)
[[nodiscard]] constexpr auto makeBufferViewFromSpan(std::span<const Sample> samples,
                                                    std::size_t block_size_samples) noexcept
    -> std::expected<BufferView<Sample>, ViewError> {
    using enum ViewError;

    if (block_size_samples == 0U) {
        return std::unexpected {ZeroBlockSize};
    }

    return BufferView<Sample> {samples, block_size_samples};
}

template <typename Sample>
    requires(std::is_trivially_copyable_v<Sample>)
[[nodiscard]] inline auto makeBufferViewFromBytes(const void* data,
                                                  std::size_t byte_size,
                                                  std::size_t block_size_samples) noexcept
    -> std::expected<BufferView<Sample>, ViewError> {
    using enum ViewError;

    if (data == nullptr) {
        return std::unexpected {NullData};
    }

    if (block_size_samples == 0U) {
        return std::unexpected {ZeroBlockSize};
    }

    if (byte_size % sizeof(Sample)) {
        return std::unexpected {MisalignedBytes};
    }

    const auto count = byte_size / sizeof(Sample);
    return BufferView<Sample> {std::span {static_cast<const Sample*>(data), count},
                               block_size_samples};
}
}  // namespace audio_blocks
