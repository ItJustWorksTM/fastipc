/*
 *  main.cxx
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

#include <stop_token>
#include "io/context.hxx"
#include "tower.hxx"

namespace fastipc {
namespace {

co::Co<int> main() {
    auto tower = co_await fastipc::Tower::create("fastipcd");
    std::stop_source stop_source{};
    static_cast<void>(co_await tower.run(stop_source.get_token()));

    co_return 0;
}

} // namespace
} // namespace fastipc

int main() { return fastipc::io::block_on(fastipc::main); }
