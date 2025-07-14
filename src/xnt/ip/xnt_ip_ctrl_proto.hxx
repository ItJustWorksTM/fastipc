#pragma once

#include <cstdint>

namespace fastipc::xnt::ctrl {

using Magic = std::uint16_t;
constexpr Magic kMagic{0xDABE};

using Length = std::uint32_t;

enum class RequestType : std::uint8_t {
    hello = 0x00u,
    advert = 0x01u,
    subscribe = 0x02u,
};

struct HeloResponse {
    RequestType type;
};

} // namespace fastipc::xnt::ctrl
