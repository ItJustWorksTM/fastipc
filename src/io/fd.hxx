/*
 *  fd.hxx
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

#include <cstddef>
#include <expected>
#include <span>
#include <utility>

#include <unistd.h>

#include "result.hxx"

namespace fastipc::io {

class Fd final {
  public:
    constexpr explicit Fd() noexcept = default;
    constexpr explicit Fd(int fd) noexcept : m_fd{fd} {}

    constexpr Fd(const Fd&) noexcept = delete;
    constexpr Fd& operator=(const Fd&) noexcept = delete;

    constexpr Fd(Fd&& it) noexcept : Fd{std::exchange(it.m_fd, -1)} {}
    constexpr Fd& operator=(Fd&& rhs) noexcept {
        auto tmp = std::move(rhs);

        std::swap(tmp.m_fd, m_fd);

        return *this;
    }

    ~Fd() noexcept {
        if (m_fd > 0) {
            ::close(m_fd);
        }
    }

    [[nodiscard]] constexpr const int& fd() const noexcept { return m_fd; }

  private:
    int m_fd{-1};
};

[[nodiscard]] constexpr expected<Fd> adoptSysFd(int fd) noexcept {
    return sysVal(fd).transform([](int fd) { return Fd{fd}; });
}

[[nodiscard]] inline expected<std::size_t> write(const Fd& fd, std::span<const std::byte> buf) noexcept {
    return sysVal(::write(fd.fd(), buf.data(), buf.size())).transform([](int written) {
        return static_cast<std::size_t>(written);
    });
}

[[nodiscard]] inline expected<std::size_t> read(const Fd& fd, std::span<std::byte> buf) noexcept {
    return sysVal(::read(fd.fd(), buf.data(), buf.size())).transform([](int read) {
        return static_cast<std::size_t>(read);
    });
}

} // namespace fastipc::io
