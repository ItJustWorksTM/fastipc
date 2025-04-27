/*
 *  local_proto.hxx
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
#include <cstdint>
#include <string_view>

namespace fastipc {

enum class RequesterType : std::uint8_t {
    Reader = 0,
    Writer = 1,
};

// NOLINTNEXTLINE(altera-struct-pack-align)
struct ClientRequest {
    RequesterType type;
    std::size_t max_payload_size;
    std::string_view topic_name;
};

} // namespace fastipc
