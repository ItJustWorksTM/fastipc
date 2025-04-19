#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "io/cursor.hxx"

namespace fastipc {

enum class RequesterType : std::uint8_t {
    Reader = 0,
    Writer = 1,
};

struct ClientRequest {
    RequesterType type;
    std::size_t max_payload_size;
    std::string_view topic_name;
};

constexpr ClientRequest readClientRequest(std::span<const std::uint8_t>& buf) noexcept {
    const auto requester_type = io::getBuf<std::uint8_t>(buf);
    const auto max_payload_size = io::getBuf<std::size_t>(buf);
    const auto topic_name_buf = io::takeBuf(buf, io::getBuf<std::uint8_t>(buf));

    assert(requester_type < 2);

    return ClientRequest{
        static_cast<RequesterType>(requester_type),
        max_payload_size,
        {reinterpret_cast<const char*>(topic_name_buf.data()), topic_name_buf.size()},
    };
}

constexpr void writeClientRequest(std::span<std::uint8_t>& buf, const ClientRequest& request) noexcept {
    const auto topic_name_buf = std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(request.topic_name.data()), request.topic_name.size()};

    io::putBuf(buf, static_cast<std::uint8_t>(std::to_underlying(request.type)));
    io::putBuf(buf, request.max_payload_size);
    io::putBuf(buf, static_cast<std::uint8_t>(topic_name_buf.size()));
    io::putBuf(buf, topic_name_buf);
}

} // namespace fastipc
