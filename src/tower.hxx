#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

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

} // namespace fastipc
