/*
 *  io_env.hxx
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

#include "co/coroutine.hxx"
#include "co/scheduler.hxx"
#include "reactor.hxx"

namespace fastipc::io {

struct Env final {
    co::Scheduler* scheduler;
    Reactor* reactor;
};

template <class T>
using Co = co::Co<T, Env>;

template <class T>
using Promise = co::Promise<T, Env>;

} // namespace fastipc::io
