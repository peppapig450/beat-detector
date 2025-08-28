module;
#include <expected>
#include <spa/buffer/buffer.h>
#include <cstddef>

export module audio.blocks:spa;

import audio.blocks;

namespace audio_blocks::detail {

[[nodiscard]] inline auto spaDataPtr(const spa_buffer* buffer) noexcept -> const void* {
    return (buffer != nullptr && buffer->datas[0].data != nullptr) ? buffer->datas[0].data
                                                                   : nullptr;
}

[[nodiscard]] inline auto spaSizeBytes(const spa_buffer* buffer) noexcept -> std::size_t {
    if (buffer != nullptr && buffer->datas[0].chunk != nullptr) {
        return static_cast<std::size_t>(buffer->datas[0].chunk->size);
    }

    return 0U;
}

[[nodiscard]] inline auto spaStrideBytes(const spa_buffer* buffer,
                                         std::size_t       default_stride) noexcept -> std::size_t {
    if (buffer == nullptr || buffer->datas[0].chunk == nullptr) {
        return 0U;
    }

    const auto stride = static_cast<std::size_t>(buffer->datas[0].chunk->stride);
    return (stride != 0U) ? stride : default_stride;
}

}  // namespace audio_blocks::detail

export [[nodiscard]] inline auto
makeBufferViewFromSpaMonoF32(const spa_buffer* buffer, std::size_t block_size_samples) noexcept
    -> std::expected<audio_blocks::BufferView<float>, audio_blocks::ViewError> {
    using namespace audio_blocks::detail;
    using enum audio_blocks::ViewError;

    const void* data_ptr = spaDataPtr(buffer);
    const auto  byte_count = spaSizeBytes(buffer);
    const auto  stride     = spaStrideBytes(buffer, sizeof(float));

    if (data_ptr == nullptr) {
        return std::unexpected {NullData};
    }
    if (block_size_samples == 0) {
        return std::unexpected{ZeroBlockSize};
    }
    if (stride != sizeof(float)) {
        return std::unexpected{UnsupportedStride};
    }
    if ((byte_count % sizeof(float)) != 0U) {
        return std::unexpected{MisalignedBytes};
    }


    return audio_blocks::makeBufferViewFromBytes<float>(data_ptr, byte_count, block_size_samples);
}

