/*
 *  writer.cxx
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

#include <iostream>
#include <print>
#include <string_view>

#include <fastipc.hxx>

using namespace std::literals;

int main() {
    fastipc::Writer writer{"channel"sv, 256u}; // NOLINT(*-magic-numbers)

    auto sample = writer.prepare();

    std::print("Enter value for seq-id {}: ", sample.getSequenceId());
    std::cin.getline(static_cast<char*>(sample.getPayload()), 256u); // NOLINT(*-magic-numbers)

    writer.submit(sample);
}
