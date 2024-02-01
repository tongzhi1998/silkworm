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

#include <silkworm/rpc/types/writer.hpp>

namespace silkworm::rpc {

class Channel : public StreamWriter {
  public:
    Channel() = default;
    ~Channel() override = default;

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    virtual Task<void> open_stream() = 0;
    virtual Task<void> write_rsp(const std::string& content) = 0;
};

}  // namespace silkworm::rpc