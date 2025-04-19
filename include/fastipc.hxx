#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fastipc {

/// Channel reader
class Reader final {
  public:
    class Sample final {
      public:
        [[nodiscard]] auto getSequenceId() const -> std::uint64_t;
        [[nodiscard]] auto getTimestamp() const -> std::chrono::system_clock::time_point;
        [[nodiscard]] auto getPayload() const -> const void*;

      private:
        friend class Reader;
        explicit Sample(void* shadow) noexcept : shadow_{shadow} {}
        void* shadow_;
    };

    /// Creates a Reader for the given channel, validating the expected payload
    /// size
    Reader(std::string_view channel_name, std::size_t max_payload_size);

    /// Indicates whether a sample with a greater sequence id is available
    [[nodiscard]] auto hasNewData(std::uint64_t sequence_id) const -> bool;

    /// Acquires the latest available data sample
    [[nodiscard]] auto acquire() -> Sample;

    /// Release the provided sample
    ///
    /// @attention Must have been obtained by a call to @a acquire
    void release(Sample sample_handle);

  private:
    void* shadow_;
};

/// Channel writer
class Writer final {
  public:
    class Sample final {
      public:
        [[nodiscard]] auto getSequenceId() const -> std::uint64_t;
        [[nodiscard]] auto getPayload() -> void*;

      private:
        friend class Writer;
        explicit Sample(void* shadow) noexcept : shadow_{shadow} {}
        void* shadow_;
    };

    /// Creates a Writer for the given channel, setting the expected payload size
    Writer(std::string_view channel_name, std::size_t max_payload_size);

    /// Prepares a new sample to fill
    ///
    /// @note This method has undeterministic worst-case execution time.
    [[nodiscard]] auto prepare() -> Sample;

    /// Submit the filled sample to the system
    ///
    /// @attention Must have been obtained by a call to @a prepare
    void submit(Sample sample_handle);

  private:
    void* m_shadow;
};

class Logger final {
  public:
    explicit Logger(int sock_fd);

  private:
    // ...
};

} // namespace fastipc
