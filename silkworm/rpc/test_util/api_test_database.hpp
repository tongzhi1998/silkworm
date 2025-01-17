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

#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

#include <boost/asio/co_spawn.hpp>
#include <nlohmann/json.hpp>

#include <silkworm/core/chain/genesis.hpp>
#include <silkworm/core/common/empty_hashes.hpp>
#include <silkworm/core/execution/execution.hpp>
#include <silkworm/core/state/in_memory_state.hpp>
#include <silkworm/core/types/address.hpp>
#include <silkworm/core/types/block.hpp>
#include <silkworm/core/types/receipt.hpp>
#include <silkworm/db/access_layer.hpp>
#include <silkworm/db/buffer.hpp>
#include <silkworm/db/genesis.hpp>
#include <silkworm/db/kv/api/state_cache.hpp>
#include <silkworm/db/test_util/test_database_context.hpp>
#include <silkworm/infra/test_util/log.hpp>
#include <silkworm/rpc/common/constants.hpp>
#include <silkworm/rpc/common/worker_pool.hpp>
#include <silkworm/rpc/ethdb/file/local_database.hpp>
#include <silkworm/rpc/json_rpc/request_handler.hpp>
#include <silkworm/rpc/json_rpc/validator.hpp>
#include <silkworm/rpc/test_util/service_context_test_base.hpp>
#include <silkworm/rpc/transport/stream_writer.hpp>

namespace silkworm::rpc::test_util {

class ChannelForTest : public StreamWriter {
  public:
    Task<void> open_stream() override { co_return; }
    Task<void> close_stream() override { co_return; }
    Task<std::size_t> write(std::string_view /* content */, bool /* last */) override { co_return 0; }
};

class RequestHandler_ForTest : public json_rpc::RequestHandler {
  public:
    RequestHandler_ForTest(ChannelForTest* channel,
                           commands::RpcApi& rpc_api,
                           const commands::RpcApiTable& rpc_api_table)
        : json_rpc::RequestHandler(channel, rpc_api, rpc_api_table) {}

    Task<void> request_and_create_reply(const nlohmann::json& request_json, std::string& response) {
        co_await RequestHandler::handle_request_and_create_reply(request_json, response);
    }

    Task<void> handle_request(const std::string& request, std::string& response) {
        auto answer = co_await RequestHandler::handle(request);
        if (answer) {
            response = *answer;
        }
    }

  private:
    inline static const std::vector<std::string> allowed_origins;
};

class LocalContextTestBase : public ServiceContextTestBase {
  public:
    explicit LocalContextTestBase(db::kv::api::StateCache* state_cache, mdbx::env& chaindata_env) : ServiceContextTestBase() {
        add_private_service<ethdb::Database>(io_context_, std::make_unique<ethdb::file::LocalDatabase>(state_cache, chaindata_env));
    }
};

template <typename TestRequestHandler>
class RpcApiTestBase : public LocalContextTestBase {
  public:
    explicit RpcApiTestBase(mdbx::env& chaindata_env)
        : LocalContextTestBase(&state_cache_, chaindata_env),
          workers_{1},
          socket_{io_context_},
          rpc_api_{io_context_, workers_},
          rpc_api_table_{kDefaultEth1ApiSpec} {
    }

    template <auto method, typename... Args>
    auto run(Args&&... args) {
        ChannelForTest channel;
        TestRequestHandler handler{&channel, rpc_api_, rpc_api_table_};
        return spawn_and_wait((handler.*method)(std::forward<Args>(args)...));
    }

    WorkerPool workers_;
    boost::asio::ip::tcp::socket socket_;
    commands::RpcApi rpc_api_;
    commands::RpcApiTable rpc_api_table_;
    db::kv::api::CoherentStateCache state_cache_;
};

class RpcApiE2ETest : public db::test_util::TestDatabaseContext, RpcApiTestBase<RequestHandler_ForTest> {
  public:
    explicit RpcApiE2ETest() : RpcApiTestBase<RequestHandler_ForTest>(get_mdbx_env()) {
        // Ensure JSON RPC spec has been loaded into the validator
        if (!jsonrpc_spec_loaded) {
            json_rpc::Validator::load_specification();
            jsonrpc_spec_loaded = true;
        }
    }
    using RpcApiTestBase<RequestHandler_ForTest>::run;

  private:
    static inline silkworm::test_util::SetLogVerbosityGuard log_guard_{log::Level::kNone};
    static inline bool jsonrpc_spec_loaded{false};
};

}  // namespace silkworm::rpc::test_util
