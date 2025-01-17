/*
   Copyright 2023 The Silkworm Authors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#pragma once

#include <string>

#include <silkworm/infra/concurrency/task.hpp>

#include <grpcpp/grpcpp.h>

namespace silkworm::rpc {

bool is_disconnect_error(const grpc::Status& status, grpc::Channel& channel);

inline static constexpr int64_t kDefaultMinBackoffReconnectTimeout{5'000};
inline static constexpr int64_t kDefaultMaxBackoffReconnectTimeout{600'000};

Task<void> reconnect_channel(grpc::Channel& channel,
                             std::string log_prefix,
                             int64_t min_msec = kDefaultMinBackoffReconnectTimeout,
                             int64_t max_msec = kDefaultMaxBackoffReconnectTimeout);

}  // namespace silkworm::rpc
