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
#include <utility>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "result.hxx"

namespace fastipc::io {

template <class T>
concept AsFd = requires(const T& t) {
    { t.fd() } -> std::same_as<const int&>;
};

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

[[nodiscard]] inline expected<std::size_t> write(const AsFd auto& fd, std::span<const std::byte> buf) noexcept {
    return sysVal(::write(fd.fd(), buf.data(), buf.size())).transform([](int written) {
        return static_cast<std::size_t>(written);
    });
}

[[nodiscard]] inline expected<std::size_t> read(const AsFd auto& fd, std::span<std::byte> buf) noexcept {
    return sysVal(::read(fd.fd(), buf.data(), buf.size())).transform([](int read) {
        return static_cast<std::size_t>(read);
    });
}

inline expected<void> setBlocking(const AsFd auto& fd, bool blocking) noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    return sysVal(::fcntl(fd.fd(), F_GETFL, 0)).and_then([&](auto flags) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        return sysCheck(::fcntl(fd.fd(), F_SETFL, blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK));
    });
}

[[nodiscard]] inline expected<std::pair<Fd, Fd>> makePipe() {
    std::array<int, 2> raw_fds{};

    return sysCheck(::pipe2(raw_fds.data(), SOCK_CLOEXEC)).transform([&]() {
        auto read_fd = io::Fd{raw_fds[0]};
        auto write_fd = io::Fd{raw_fds[1]};

        return std::pair<Fd, Fd>{std::move(read_fd), std::move(write_fd)};
    });
}

} // namespace fastipc::io
