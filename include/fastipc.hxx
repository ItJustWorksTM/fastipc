/*
 *  fastipc.hxx
 *  Copyright 2025 ItJustWorksTM
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

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
        explicit Sample(void* shadow) noexcept : m_shadow{shadow} {}
        void* m_shadow;
    };

    /// Creates a Reader for the given channel, validating the expected payload
    /// size
    Reader(std::string_view channel_name, std::size_t max_payload_size);

    Reader(const Reader&) = delete;
    Reader(Reader&& from) noexcept : m_shadow{std::exchange(from.m_shadow, nullptr)} {}
    Reader& operator=(const Reader&) = delete;
    Reader& operator=(Reader&& from) & noexcept {
        auto other = std::move(from);
        std::swap(m_shadow, other.m_shadow);
        return *this;
    }
    ~Reader() noexcept;

    /// Indicates whether a sample with a greater sequence id is available
    [[nodiscard]] auto hasNewData(std::uint64_t sequence_id) const -> bool;

    /// Acquires the latest available data sample
    [[nodiscard]] auto acquire() -> Sample;

    /// Release the provided sample
    ///
    /// @attention Must have been obtained by a call to @a acquire
    void release(Sample sample_handle);

  private:
    void* m_shadow;
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
        explicit Sample(void* shadow) noexcept : m_shadow{shadow} {}
        void* m_shadow;
    };

    /// Creates a Writer for the given channel, setting the expected payload size
    Writer(std::string_view channel_name, std::size_t max_payload_size);

    Writer(const Writer&) = delete;
    Writer(Writer&& from) noexcept : m_shadow{std::exchange(from.m_shadow, nullptr)} {}
    Writer& operator=(const Writer&) = delete;
    Writer& operator=(Writer&& from) & noexcept {
        auto other = std::move(from);
        std::swap(m_shadow, other.m_shadow);
        return *this;
    }
    ~Writer() noexcept;

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
