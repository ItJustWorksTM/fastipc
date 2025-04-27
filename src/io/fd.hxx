#pragma once

#include <cerrno>
#include <expected>
#include <utility>

#include <unistd.h>

#include "result.hxx"

namespace fastipc {
namespace io {

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

[[nodiscard]] constexpr io::expected<Fd> adoptSysFd(int fd) noexcept {
    return sysVal(fd).transform([](int fd) { return Fd{fd}; });
}

} // namespace io
} // namespace fastipc
