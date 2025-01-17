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

#include "ots_api.hpp"

#include <numeric>
#include <string>
#include <utility>

#include <silkworm/core/common/empty_hashes.hpp>
#include <silkworm/core/protocol/ethash_rule_set.hpp>
#include <silkworm/core/types/address.hpp>
#include <silkworm/core/types/evmc_bytes32.hpp>
#include <silkworm/db/access_layer.hpp>
#include <silkworm/db/kv/api/endpoint/key_value.hpp>
#include <silkworm/db/mdbx/bitmap.hpp>
#include <silkworm/db/state/state_reader.hpp>
#include <silkworm/db/tables.hpp>
#include <silkworm/infra/common/ensure.hpp>
#include <silkworm/infra/common/log.hpp>
#include <silkworm/rpc/core/blocks.hpp>
#include <silkworm/rpc/core/cached_chain.hpp>
#include <silkworm/rpc/core/evm_trace.hpp>
#include <silkworm/rpc/core/receipts.hpp>
#include <silkworm/rpc/json/types.hpp>
#include <silkworm/rpc/protocol/errors.hpp>

namespace silkworm::rpc::commands {

using db::state::StateReader;

constexpr int kCurrentApiLevel{8};

Task<void> OtsRpcApi::handle_ots_get_api_level(const nlohmann::json& request, nlohmann::json& reply) {
    reply = make_json_content(request, kCurrentApiLevel);
    co_return;
}

Task<void> OtsRpcApi::handle_ots_has_code(const nlohmann::json& request, nlohmann::json& reply) {
    const auto& params = request["params"];
    if (params.size() != 2) {
        const auto error_msg = "invalid ots_hasCode params: " + params.dump();
        SILK_ERROR << error_msg;
        reply = make_json_error(request, kInvalidParams, error_msg);
        co_return;
    }
    const auto address = params[0].get<evmc::address>();
    const auto block_id = params[1].is_string() ? params[1].get<std::string>() : to_quantity(params[1].get<uint64_t>());

    SILK_DEBUG << "address: " << address << " block_id: " << block_id;

    auto tx = co_await database_->begin();

    try {
        // Check if target block is the latest one: use local state cache (if any) for target transaction
        const bool is_latest_block = co_await core::is_latest_block_number(BlockNumberOrHash{block_id}, *tx);
        tx->set_state_cache_enabled(is_latest_block);

        StateReader state_reader{*tx};

        const auto block_number = co_await core::get_block_number(block_id, *tx);
        std::optional<silkworm::Account> account{co_await state_reader.read_account(address, block_number + 1)};

        if (account) {
            auto code{co_await state_reader.read_code(account->code_hash)};
            reply = make_json_content(request, code.has_value());
        } else {
            reply = make_json_content(request, false);
        }
    } catch (const std::exception& e) {
        SILK_ERROR << "exception: " << e.what() << " processing request: " << request.dump();
        reply = make_json_error(request, kInternalError, e.what());
    } catch (...) {
        SILK_ERROR << "unexpected exception processing request: " << request.dump();
        reply = make_json_error(request, kServerError, "unexpected exception");
    }

    co_await tx->close();  // RAII not (yet) available with coroutines
}

Task<void> OtsRpcApi::handle_ots_get_block_details(const nlohmann::json& request, nlohmann::json& reply) {
    auto params = request["params"];
    if (params.size() != 1) {
        auto error_msg = "invalid handle_ots_getBlockDetails params: " + params.dump();
        SILK_ERROR << error_msg;
        reply = make_json_error(request, kInvalidParams, error_msg);
        co_return;
    }
    const auto block_id = params[0].is_string() ? params[0].get<std::string>() : to_quantity(params[0].get<uint64_t>());

    SILK_DEBUG << "block_id: " << block_id;

    auto tx = co_await database_->begin();

    try {
        const auto block_number = co_await core::get_block_number(block_id, *tx);
        const auto chain_storage = tx->create_storage();
        const auto block_with_hash = co_await core::read_block_by_number(*block_cache_, *chain_storage, block_number);
        if (block_with_hash) {
            const auto total_difficulty{co_await chain_storage->read_total_difficulty(block_with_hash->hash, block_number)};
            ensure(total_difficulty.has_value(), [&]() { return "no total difficulty for block: " + std::to_string(block_number); });
            const Block extended_block{block_with_hash, *total_difficulty, false};
            const auto block_size = extended_block.get_block_size();
            const BlockDetails block_details{block_size, block_with_hash->hash, block_with_hash->block.header, *total_difficulty,
                                             block_with_hash->block.transactions.size(), block_with_hash->block.ommers,
                                             block_with_hash->block.withdrawals};
            const auto receipts = co_await core::get_receipts(*tx, *block_with_hash);
            const auto chain_config = co_await chain_storage->read_chain_config();
            const IssuanceDetails issuance = get_issuance(chain_config, *block_with_hash);
            const intx::uint256 total_fees = get_block_fees(*block_with_hash, receipts);
            const BlockDetailsResponse block_details_response{block_details, issuance, total_fees};
            reply = make_json_content(request, block_details_response);
        } else {
            reply = make_json_content(request, nlohmann::detail::value_t::null);
        }
    } catch (const std::invalid_argument& iv) {
        SILK_WARN << "invalid_argument: " << iv.what() << " processing request: " << request.dump();
        reply = make_json_content(request, nlohmann::detail::value_t::null);
    } catch (const std::exception& e) {
        SILK_ERROR << "exception: " << e.what() << " processing request: " << request.dump();
        reply = make_json_error(request, kInternalError, e.what());
    } catch (...) {
        SILK_ERROR << "unexpected exception processing request: " << request.dump();
        reply = make_json_error(request, kServerError, "unexpected exception");
    }

    co_await tx->close();  // RAII not (yet) available with coroutines
}

Task<void> OtsRpcApi::handle_ots_get_block_details_by_hash(const nlohmann::json& request, nlohmann::json& reply) {
    auto params = request["params"];
    if (params.size() != 1) {
        auto error_msg = "invalid ots_getBlockDetailsByHash params: " + params.dump();
        SILK_ERROR << error_msg;
        reply = make_json_error(request, kInvalidParams, error_msg);
        co_return;
    }
    auto block_hash = params[0].get<evmc::bytes32>();

    SILK_DEBUG << "block_hash: " << silkworm::to_hex(block_hash);

    auto tx = co_await database_->begin();

    try {
        const auto chain_storage = tx->create_storage();
        const auto block_with_hash = co_await core::read_block_by_hash(*block_cache_, *chain_storage, block_hash);
        if (block_with_hash) {
            const auto block_number = block_with_hash->block.header.number;
            const auto total_difficulty{co_await chain_storage->read_total_difficulty(block_with_hash->hash, block_number)};
            ensure(total_difficulty.has_value(), [&]() { return "no total difficulty for block: " + std::to_string(block_number); });
            const Block extended_block{block_with_hash, *total_difficulty, false};
            const auto block_size = extended_block.get_block_size();
            const BlockDetails block_details{block_size, block_with_hash->hash, block_with_hash->block.header, *total_difficulty,
                                             block_with_hash->block.transactions.size(), block_with_hash->block.ommers,
                                             block_with_hash->block.withdrawals};
            const auto receipts = co_await core::get_receipts(*tx, *block_with_hash);
            const auto chain_config = co_await chain_storage->read_chain_config();
            const IssuanceDetails issuance = get_issuance(chain_config, *block_with_hash);
            const intx::uint256 total_fees = get_block_fees(*block_with_hash, receipts);
            const BlockDetailsResponse block_details_response{block_details, issuance, total_fees};
            reply = make_json_content(request, block_details_response);
        } else {
            reply = make_json_content(request, nlohmann::detail::value_t::null);
        }
    } catch (const std::invalid_argument& iv) {
        SILK_WARN << "invalid_argument: " << iv.what() << " processing request: " << request.dump();
        reply = make_json_content(request, {});
    } catch (const std::exception& e) {
        SILK_ERROR << "exception: " << e.what() << " processing request: " << request.dump();
        reply = make_json_error(request, kInternalError, e.what());
    } catch (...) {
        SILK_ERROR << "unexpected exception processing request: " << request.dump();
        reply = make_json_error(request, kServerError, "unexpected exception");
    }

    co_await tx->close();  // RAII not (yet) available with coroutines
}

Task<void> OtsRpcApi::handle_ots_get_block_transactions(const nlohmann::json& request, nlohmann::json& reply) {
    const auto& params = request["params"];
    if (params.size() != 3) {
        auto error_msg = "invalid ots_getBlockTransactions params: " + params.dump();
        SILK_ERROR << error_msg;
        reply = make_json_error(request, kInvalidParams, error_msg);
        co_return;
    }

    const auto block_id = params[0].is_string() ? params[0].get<std::string>() : to_quantity(params[0].get<uint64_t>());
    const auto page_number = params[1].get<std::size_t>();
    const auto page_size = params[2].get<std::size_t>();

    SILK_DEBUG << "block_id: " << block_id << " page_number: " << page_number << " page_size: " << page_size;

    auto tx = co_await database_->begin();

    try {
        const auto block_number = co_await core::get_block_number(block_id, *tx);
        const auto chain_storage = tx->create_storage();

        const auto block_with_hash = co_await core::read_block_by_number(*block_cache_, *chain_storage, block_number);
        if (block_with_hash) {
            const auto total_difficulty{co_await chain_storage->read_total_difficulty(block_with_hash->hash, block_number)};
            ensure(total_difficulty.has_value(), [&]() { return "no total difficulty for block: " + std::to_string(block_number); });
            const Block extended_block{block_with_hash, *total_difficulty, false};
            auto receipts = co_await core::get_receipts(*tx, *block_with_hash);
            auto block_size = extended_block.get_block_size();
            auto transaction_count = block_with_hash->block.transactions.size();

            BlockTransactionsResponse block_transactions{
                block_size,
                block_with_hash->hash,
                block_with_hash->block.header,
                *total_difficulty,
                transaction_count,
                block_with_hash->block.ommers,
                {},  // receipt
                {},  // transactions
                block_with_hash->block.withdrawals};

            auto page_end = block_with_hash->block.transactions.size() - (page_size * page_number);

            if (page_end > block_with_hash->block.transactions.size()) {
                page_end = 0;
            }

            auto page_start = page_end - page_size;

            if (page_start > page_end) {
                page_start = 0;
            }

            for (auto i = page_start; i < page_end; i++) {
                block_transactions.receipts.push_back(receipts.at(i));
                block_transactions.transactions.push_back(block_with_hash->block.transactions.at(i));
            }

            reply = make_json_content(request, block_transactions);
        } else {
            reply = make_json_content(request, {});
        }
    } catch (const std::invalid_argument& iv) {
        SILK_WARN << "invalid_argument: " << iv.what() << " processing request: " << request.dump();
        reply = make_json_content(request, {});
    } catch (const std::exception& e) {
        SILK_ERROR << "exception: " << e.what() << " processing request: " << request.dump();
        reply = make_json_error(request, kInternalError, e.what());
    } catch (...) {
        SILK_ERROR << "unexpected exception processing request: " << request.dump();
        reply = make_json_error(request, kServerError, "unexpected exception");
    }

    co_await tx->close();  // RAII not (yet) available with coroutines
}

Task<void> OtsRpcApi::handle_ots_get_transaction_by_sender_and_nonce(const nlohmann::json& request, nlohmann::json& reply) {
    const auto& params = request["params"];
    if (params.size() != 2) {
        const auto error_msg = "invalid ots_getTransactionBySenderAndNonce params: " + params.dump();
        SILK_ERROR << error_msg;
        reply = make_json_error(request, kInvalidParams, error_msg);
        co_return;
    }

    const auto sender = params[0].get<evmc::address>();
    const auto nonce = params[1].get<uint64_t>();

    SILK_DEBUG << "sender: " << sender << " nonce: " << nonce;
    auto tx = co_await database_->begin();

    try {
        auto account_history_cursor = co_await tx->cursor(db::table::kAccountHistoryName);
        auto account_change_set_cursor = co_await tx->cursor_dup_sort(db::table::kAccountChangeSetName);
        const ByteView sender_byte_view{sender.bytes};
        auto key_value = co_await account_history_cursor->seek(sender_byte_view);

        std::vector<BlockNum> account_block_numbers;

        BlockNum max_block_prev_chunk = 0;
        roaring::Roaring64Map bitmap;

        while (true) {
            if (key_value.key.empty() || !key_value.key.starts_with(sender_byte_view)) {
                auto plain_state_cursor = co_await tx->cursor(db::table::kPlainStateName);
                auto account_payload = co_await plain_state_cursor->seek(sender_byte_view);
                auto account = Account::from_encoded_storage(account_payload.value);

                if (account.has_value() && account.value().nonce > nonce) {
                    break;
                }

                reply = make_json_content(request, nlohmann::detail::value_t::null);
                co_await tx->close();
                co_return;
            }

            bitmap = db::bitmap::parse(key_value.value);
            auto const max_block = bitmap.maximum();
            auto block_key{db::block_key(max_block)};
            auto account_payload = co_await account_change_set_cursor->seek_both(block_key, sender_byte_view);
            if (account_payload.starts_with(sender_byte_view)) {
                account_payload = account_payload.substr(sender_byte_view.length());
                auto account = Account::from_encoded_storage(account_payload);

                if (account.has_value() && account.value().nonce > nonce) {
                    break;
                }
            }
            max_block_prev_chunk = max_block;
            key_value = co_await account_history_cursor->next();
        }

        uint64_t cardinality = bitmap.cardinality();
        account_block_numbers.reserve(cardinality);
        bitmap.toUint64Array(account_block_numbers.data());

        uint64_t idx = 0;
        for (uint64_t i = 0; i < cardinality; i++) {
            auto block_number = account_block_numbers[i];
            auto block_key{db::block_key(block_number)};
            auto account_payload = co_await account_change_set_cursor->seek_both(block_key, sender_byte_view);
            if (account_payload.starts_with(sender_byte_view)) {
                account_payload = account_payload.substr(sender_byte_view.length());
                auto account = Account::from_encoded_storage(account_payload);

                if (account.has_value() && account.value().nonce > nonce) {
                    idx = i;
                    break;
                }
            }
        }

        auto nonce_block = max_block_prev_chunk;
        if (idx > 0) {
            nonce_block = account_block_numbers[idx - 1];
        }

        const auto chain_storage{tx->create_storage()};
        auto block_with_hash = co_await core::read_block_by_number(*block_cache_, *chain_storage, nonce_block);
        if (block_with_hash) {
            for (const auto& transaction : block_with_hash->block.transactions) {
                if (transaction.sender() == sender && transaction.nonce == nonce) {
                    reply = make_json_content(request, transaction.hash());
                    co_await tx->close();
                    co_return;
                }
            }
            reply = make_json_content(request, nlohmann::detail::value_t::null);
        } else {
            reply = make_json_content(request, nlohmann::detail::value_t::null);
        }
    } catch (const std::invalid_argument& iv) {
        SILK_WARN << "invalid_argument: " << iv.what() << " processing request: " << request.dump();
        reply = make_json_content(request, nlohmann::detail::value_t::null);
    } catch (const std::exception& e) {
        SILK_ERROR << "exception: " << e.what() << " processing request: " << request.dump();
        reply = make_json_error(request, kInternalError, e.what());
    } catch (...) {
        SILK_ERROR << "unexpected exception processing request: " << request.dump();
        reply = make_json_error(request, kServerError, "unexpected exception");
    }

    co_await tx->close();  // RAII not (yet) available with coroutines
}

Task<void> OtsRpcApi::handle_ots_get_contract_creator(const nlohmann::json& request, nlohmann::json& reply) {
    const auto& params = request["params"];
    if (params.size() != 1) {
        const auto error_msg = "invalid ots_getContractCreator params: " + params.dump();
        SILK_ERROR << error_msg;
        reply = make_json_error(request, kInvalidParams, error_msg);
        co_return;
    }

    const auto contract_address = params[0].get<evmc::address>();

    SILK_DEBUG << "contract_address: " << contract_address;

    auto tx = co_await database_->begin();

    try {
        const ByteView contract_address_byte_view{contract_address.bytes};
        auto plain_state_cursor = co_await tx->cursor(db::table::kPlainStateName);
        auto account_payload = co_await plain_state_cursor->seek(contract_address_byte_view);
        auto plain_state_account = Account::from_encoded_storage(account_payload.value);

        if (!plain_state_account.has_value()) {
            reply = make_json_content(request, nlohmann::detail::value_t::null);
            co_await tx->close();
            co_return;
        }

        if (plain_state_account.value().code_hash == kEmptyHash) {
            reply = make_json_content(request, nlohmann::detail::value_t::null);
            co_await tx->close();
            co_return;
        }

        auto account_history_cursor = co_await tx->cursor(db::table::kAccountHistoryName);
        auto account_change_set_cursor = co_await tx->cursor_dup_sort(db::table::kAccountChangeSetName);

        auto key_value = co_await account_history_cursor->seek(db::account_history_key(contract_address, 0));

        std::vector<BlockNum> account_block_numbers;

        BlockNum max_block_prev_chunk = 0;
        roaring::Roaring64Map bitmap;

        if (key_value.key.empty() || !key_value.key.starts_with(contract_address_byte_view)) {
            reply = make_json_content(request, nlohmann::detail::value_t::null);
            co_await tx->close();
            co_return;
        }
        while (true) {
            bitmap = db::bitmap::parse(key_value.value);
            auto const max_block = bitmap.maximum();
            auto block_key{db::block_key(max_block)};
            auto address_and_payload = co_await account_change_set_cursor->seek_both(block_key, contract_address_byte_view);
            if (!address_and_payload.starts_with(contract_address_byte_view)) {
                reply = make_json_content(request, nlohmann::detail::value_t::null);
                co_await tx->close();
                co_return;
            }
            auto payload = address_and_payload.substr(contract_address_byte_view.length());
            auto account = Account::from_encoded_storage(payload);

            if (!account) {
                reply = make_json_content(request, nlohmann::detail::value_t::null);
                co_await tx->close();
                co_return;
            }

            if (account->incarnation >= plain_state_account->incarnation) {
                break;
            }
            max_block_prev_chunk = max_block;
            key_value = co_await account_history_cursor->next();

            if (key_value.key.empty() || !key_value.key.starts_with(contract_address_byte_view)) {
                break;
            }
        }

        uint64_t cardinality = bitmap.cardinality();
        account_block_numbers.resize(cardinality);
        bitmap.toUint64Array(account_block_numbers.data());

        uint64_t idx = 0;
        for (uint64_t i = 0; i < cardinality; i++) {
            auto block_number = account_block_numbers[i];
            auto block_key{db::block_key(block_number)};
            auto address_and_payload = co_await account_change_set_cursor->seek_both(block_key, contract_address_byte_view);

            if (!address_and_payload.starts_with(contract_address_byte_view)) {
                reply = make_json_content(request, nlohmann::detail::value_t::null);
                co_await tx->close();
                co_return;
            }

            auto payload = address_and_payload.substr(contract_address_byte_view.length());
            auto account = Account::from_encoded_storage(payload);

            if (!account) {
                reply = make_json_content(request, nlohmann::detail::value_t::null);
                co_await tx->close();
                co_return;
            }

            if (account.has_value() && account->incarnation >= plain_state_account->incarnation) {
                idx = i;
                break;
            }
        }

        auto block_found = max_block_prev_chunk;
        if (idx > 0) {
            block_found = account_block_numbers[idx - 1];
        }

        const auto chain_storage{tx->create_storage()};

        auto block_with_hash = co_await core::read_block_by_number(*block_cache_, *chain_storage, block_found);
        if (block_with_hash) {
            trace::TraceCallExecutor executor{*block_cache_, *chain_storage, workers_, *tx};
            const auto result = co_await executor.trace_deploy_transaction(block_with_hash->block, contract_address);
            reply = make_json_content(request, result);
        } else {
            reply = make_json_content(request, nlohmann::detail::value_t::null);
        }
    } catch (const std::invalid_argument& iv) {
        SILK_WARN << "invalid_argument: " << iv.what() << " processing request: " << request.dump();
        reply = make_json_content(request, nlohmann::detail::value_t::null);
    } catch (const std::exception& e) {
        SILK_ERROR << "exception: " << e.what() << " processing request: " << request.dump();
        reply = make_json_error(request, kInternalError, e.what());
    } catch (...) {
        SILK_ERROR << "unexpected exception processing request: " << request.dump();
        reply = make_json_error(request, kServerError, "unexpected exception");
    }

    co_await tx->close();  // RAII not (yet) available with coroutines
}

Task<void> OtsRpcApi::handle_ots_trace_transaction(const nlohmann::json& request, nlohmann::json& reply) {
    const auto& params = request["params"];
    if (params.size() != 1) {
        const auto error_msg = "invalid ots_traceTransaction params: " + params.dump();
        SILK_ERROR << error_msg << "\n";
        reply = make_json_error(request, kInvalidParams, error_msg);
        co_return;
    }

    const auto transaction_hash = params[0].get<evmc::bytes32>();

    SILK_DEBUG << "transaction_hash: " << silkworm::to_hex(transaction_hash);

    auto tx = co_await database_->begin();

    try {
        const auto chain_storage{tx->create_storage()};
        trace::TraceCallExecutor executor{*block_cache_, *chain_storage, workers_, *tx};

        const auto transaction_with_block = co_await core::read_transaction_by_hash(*block_cache_, *chain_storage, transaction_hash);

        if (!transaction_with_block.has_value()) {
            const auto error_msg = "transaction 0x" + silkworm::to_hex(transaction_hash) + " not found";
            reply = make_json_error(request, kServerError, error_msg);
            co_await tx->close();  // RAII not (yet) available with coroutines
            co_return;
        }

        const auto result = co_await executor.trace_transaction_entries(transaction_with_block.value());

        reply = make_json_content(request, result);

    } catch (const std::invalid_argument& iv) {
        SILK_WARN << "invalid_argument: " << iv.what() << " processing request: " << request.dump();
        reply = make_json_content(request, nlohmann::detail::value_t::null);
    } catch (const std::exception& e) {
        SILK_ERROR << "exception: " << e.what() << " processing request: " << request.dump();
        reply = make_json_error(request, kInternalError, e.what());
    } catch (...) {
        SILK_ERROR << "unexpected exception processing request: " << request.dump();
        reply = make_json_error(request, kServerError, "unexpected exception");
    }

    co_await tx->close();  // RAII not (yet) available with coroutines
}

Task<void> OtsRpcApi::handle_ots_get_transaction_error(const nlohmann::json& request, nlohmann::json& reply) {
    const auto& params = request["params"];
    if (params.size() != 1) {
        const auto error_msg = "invalid ots_getTransactionError params: " + params.dump();
        SILK_ERROR << error_msg << "\n";
        reply = make_json_error(request, kInvalidParams, error_msg);
        co_return;
    }

    const auto transaction_hash = params[0].get<evmc::bytes32>();

    SILK_DEBUG << "transaction_hash: " << silkworm::to_hex(transaction_hash);

    auto tx = co_await database_->begin();

    try {
        const auto chain_storage{tx->create_storage()};
        trace::TraceCallExecutor executor{*block_cache_, *chain_storage, workers_, *tx};

        const auto transaction_with_block = co_await core::read_transaction_by_hash(*block_cache_, *chain_storage, transaction_hash);

        if (!transaction_with_block.has_value()) {
            const auto error_msg = "transaction 0x" + silkworm::to_hex(transaction_hash) + " not found";
            reply = make_json_error(request, kServerError, error_msg);
            co_await tx->close();
            co_return;
        }

        const auto result = co_await executor.trace_transaction_error(transaction_with_block.value());

        reply = make_json_content(request, result);

    } catch (const std::invalid_argument& iv) {
        SILK_WARN << "invalid_argument: " << iv.what() << " processing request: " << request.dump();
        reply = make_json_content(request, nlohmann::detail::value_t::null);
    } catch (const std::exception& e) {
        SILK_ERROR << "exception: " << e.what() << " processing request: " << request.dump();
        reply = make_json_error(request, kInternalError, e.what());
    } catch (...) {
        SILK_ERROR << "unexpected exception processing request: " << request.dump();
        reply = make_json_error(request, kServerError, "unexpected exception");
    }

    co_await tx->close();  // RAII not (yet) available with coroutines
}

Task<void> OtsRpcApi::handle_ots_get_internal_operations(const nlohmann::json& request, nlohmann::json& reply) {
    const auto& params = request["params"];
    if (params.size() != 1) {
        const auto error_msg = "invalid ots_getInternalOperations params: " + params.dump();
        SILK_ERROR << error_msg << "\n";
        reply = make_json_error(request, kInvalidParams, error_msg);
        co_return;
    }

    const auto transaction_hash = params[0].get<evmc::bytes32>();

    SILK_DEBUG << "transaction_hash: " << silkworm::to_hex(transaction_hash);

    auto tx = co_await database_->begin();

    try {
        const auto chain_storage{tx->create_storage()};
        trace::TraceCallExecutor executor{*block_cache_, *chain_storage, workers_, *tx};

        const auto transaction_with_block = co_await core::read_transaction_by_hash(*block_cache_, *chain_storage, transaction_hash);

        if (!transaction_with_block.has_value()) {
            const auto error_msg = "transaction 0x" + silkworm::to_hex(transaction_hash) + " not found";
            reply = make_json_error(request, kServerError, error_msg);
            co_await tx->close();
            co_return;
        }

        const auto result = co_await executor.trace_operations(transaction_with_block.value());

        reply = make_json_content(request, result);

    } catch (const std::invalid_argument& iv) {
        SILK_WARN << "invalid_argument: " << iv.what() << " processing request: " << request.dump();
        reply = make_json_content(request, nlohmann::detail::value_t::null);
    } catch (const std::exception& e) {
        SILK_ERROR << "exception: " << e.what() << " processing request: " << request.dump();
        reply = make_json_error(request, kInternalError, e.what());
    } catch (...) {
        SILK_ERROR << "unexpected exception processing request: " << request.dump();
        reply = make_json_error(request, kServerError, "unexpected exception");
    }

    co_await tx->close();  // RAII not (yet) available with coroutines
}

Task<void> OtsRpcApi::handle_ots_search_transactions_before(const nlohmann::json& request, nlohmann::json& reply) {
    const auto& params = request["params"];
    if (params.size() != 3) {
        const auto error_msg = "invalid ots_search_transactions_before params: " + params.dump();
        SILK_ERROR << error_msg;
        reply = make_json_error(request, kInvalidParams, error_msg);
        co_return;
    }

    const auto address = params[0].get<evmc::address>();
    auto block_number = params[1].get<BlockNum>();
    const auto page_size = params[2].get<uint64_t>();

    SILK_DEBUG << "address: " << address << " block_number: " << block_number << " page_size: " << page_size;

    if (page_size > kMaxPageSize) {
        auto error_msg = "max allowed page size: " + std::to_string(kMaxPageSize);
        SILK_ERROR << error_msg;
        reply = make_json_error(request, kServerError, error_msg);
        co_return;
    }

    auto tx = co_await database_->begin();

    try {
        auto call_from_cursor = co_await tx->cursor(db::table::kCallFromIndexName);
        auto call_to_cursor = co_await tx->cursor(db::table::kCallToIndexName);

        bool is_first_page = false;

        if (block_number == 0) {
            is_first_page = true;
        } else {
            // Internal search code considers blockNum [including], so adjust the value
            block_number--;
        }

        BackwardBlockProvider from_provider{call_from_cursor.get(), address, block_number};
        BackwardBlockProvider to_provider{call_to_cursor.get(), address, block_number};
        FromToBlockProvider from_to_provider{false, &from_provider, &to_provider};

        uint64_t result_count = 0;
        bool has_more = true;

        TransactionsWithReceipts results{
            .first_page = is_first_page};

        while (result_count < page_size && has_more) {
            std::vector<TransactionsWithReceipts> transactions_with_receipts_vec;

            has_more = co_await trace_blocks(from_to_provider, *tx, address, page_size, result_count, transactions_with_receipts_vec);

            for (const auto& item : transactions_with_receipts_vec) {
                results.receipts.insert(results.receipts.end(), item.receipts.rbegin(), item.receipts.rend());
                results.transactions.insert(results.transactions.end(), item.transactions.rbegin(), item.transactions.rend());
                results.blocks.insert(results.blocks.end(), item.blocks.rbegin(), item.blocks.rend());

                result_count += item.transactions.size();

                if (result_count >= page_size) {
                    break;
                }
            }
        }

        results.last_page = !has_more;
        reply = make_json_content(request, results);

    } catch (const std::invalid_argument& iv) {
        SILK_WARN << "invalid_argument: " << iv.what() << " processing request: " << request.dump();
        reply = make_json_content(request, nlohmann::detail::value_t::null);
    } catch (const std::exception& e) {
        SILK_ERROR << "exception: " << e.what() << " processing request: " << request.dump();
        reply = make_json_error(request, kInternalError, e.what());
    } catch (...) {
        SILK_ERROR << "unexpected exception processing request: " << request.dump();
        reply = make_json_error(request, kServerError, "unexpected exception");
    }

    co_await tx->close();  // RAII not (yet) available with coroutines
}

Task<void> OtsRpcApi::handle_ots_search_transactions_after(const nlohmann::json& request, nlohmann::json& reply) {
    const auto& params = request["params"];
    if (params.size() != 3) {
        const auto error_msg = "invalid handle_ots_search_transactions_after params: " + params.dump();
        SILK_ERROR << error_msg;
        reply = make_json_error(request, kInvalidParams, error_msg);
        co_return;
    }

    const auto address = params[0].get<evmc::address>();
    auto block_number = params[1].get<BlockNum>();
    const auto page_size = params[2].get<uint64_t>();

    SILK_DEBUG << "address: " << address << " block_number: " << block_number << " page_size: " << page_size;

    if (page_size > kMaxPageSize) {
        auto error_msg = "max allowed page size: " + std::to_string(kMaxPageSize);
        SILK_ERROR << error_msg;
        reply = make_json_error(request, kServerError, error_msg);
        co_return;
    }

    auto tx = co_await database_->begin();

    try {
        auto call_from_cursor = co_await tx->cursor(db::table::kCallFromIndexName);
        auto call_to_cursor = co_await tx->cursor(db::table::kCallToIndexName);

        bool is_last_page = false;

        if (block_number == 0) {
            is_last_page = true;
        } else {
            // Internal search code considers blockNum [including], so adjust the value
            block_number++;
        }

        ForwardBlockProvider from_provider{call_from_cursor.get(), address, block_number};
        ForwardBlockProvider to_provider{call_to_cursor.get(), address, block_number};
        FromToBlockProvider from_to_provider{true, &from_provider, &to_provider};

        uint64_t result_count = 0;
        bool has_more = true;

        TransactionsWithReceipts results{
            .last_page = is_last_page};

        while (result_count < page_size && has_more) {
            std::vector<TransactionsWithReceipts> transactions_with_receipts_vec;

            has_more = co_await trace_blocks(from_to_provider, *tx, address, page_size, result_count, transactions_with_receipts_vec);

            for (const auto& item : transactions_with_receipts_vec) {
                results.receipts.insert(results.receipts.end(), item.receipts.begin(), item.receipts.end());
                results.transactions.insert(results.transactions.end(), item.transactions.begin(), item.transactions.end());
                results.blocks.insert(results.blocks.end(), item.blocks.begin(), item.blocks.end());

                result_count += item.transactions.size();

                if (result_count >= page_size) {
                    break;
                }
            }
        }

        // Reverse results
        std::reverse(results.transactions.begin(), results.transactions.end());
        std::reverse(results.receipts.begin(), results.receipts.end());
        std::reverse(results.blocks.begin(), results.blocks.end());

        results.first_page = !has_more;
        reply = make_json_content(request, results);

    } catch (const std::invalid_argument& iv) {
        SILK_WARN << "invalid_argument: " << iv.what() << " processing request: " << request.dump();
        reply = make_json_content(request, nlohmann::detail::value_t::null);
    } catch (const std::exception& e) {
        SILK_ERROR << "exception: " << e.what() << " processing request: " << request.dump();
        reply = make_json_error(request, kInternalError, e.what());
    } catch (...) {
        SILK_ERROR << "unexpected exception processing request: " << request.dump();
        reply = make_json_error(request, kServerError, "unexpected exception");
    }

    co_await tx->close();  // RAII not (yet) available with coroutines
}

Task<bool> OtsRpcApi::trace_blocks(
    FromToBlockProvider& from_to_provider,
    db::kv::api::Transaction& tx,
    const evmc::address& address,
    uint64_t page_size,
    uint64_t result_count,
    std::vector<TransactionsWithReceipts>& results) {
    uint64_t est_blocks_to_trace = page_size - result_count;
    bool has_more = true;
    results.clear();
    results.reserve(est_blocks_to_trace);

    for (size_t i = 0; i < est_blocks_to_trace; i++) {
        TransactionsWithReceipts transactions_with_receipts;

        auto from_to_response = co_await from_to_provider.get();  // extract_next_block(from_cursor,to_cursor);
        auto next_block = from_to_response.block_number;
        has_more = from_to_response.has_more;
        if (!from_to_response.has_more && next_block == 0) {
            break;
        }

        co_await trace_block(tx, next_block, address, transactions_with_receipts);
        results.push_back(std::move(transactions_with_receipts));
    }

    co_return has_more;
}

Task<void> OtsRpcApi::trace_block(db::kv::api::Transaction& tx, BlockNum block_number, const evmc::address& search_addr, TransactionsWithReceipts& results) {
    const auto chain_storage = tx.create_storage();
    const auto block_with_hash = co_await core::read_block_by_number(*block_cache_, *chain_storage, block_number);
    if (!block_with_hash) {
        co_return;
    }

    const auto total_difficulty{co_await chain_storage->read_total_difficulty(block_with_hash->hash, block_number)};
    ensure(total_difficulty.has_value(), [&]() { return "no total difficulty for block: " + std::to_string(block_number); });
    const auto receipts = co_await core::get_receipts(tx, *block_with_hash);
    const Block extended_block{block_with_hash, *total_difficulty, false};

    trace::TraceCallExecutor executor{*block_cache_, *chain_storage, workers_, tx};
    co_await executor.trace_touch_block(*block_with_hash, search_addr, extended_block.get_block_size(), *total_difficulty, receipts, results);
}

IssuanceDetails OtsRpcApi::get_issuance(const silkworm::ChainConfig& config, const silkworm::BlockWithHash& block) {
    const auto rule_set_factory = protocol::rule_set_factory(config);
    const auto block_reward{rule_set_factory->compute_reward(block.block)};

    intx::uint256 ommers_reward = std::accumulate(block_reward.ommers.begin(), block_reward.ommers.end(), intx::uint256{0});

    IssuanceDetails issuance{
        .miner_reward = block_reward.miner,
        .ommers_reward = ommers_reward,
        .total_reward = block_reward.miner + ommers_reward};

    return issuance;
}

intx::uint256 OtsRpcApi::get_block_fees(const silkworm::BlockWithHash& block, const std::vector<Receipt>& receipts) {
    intx::uint256 fees = 0;
    for (const auto& receipt : receipts) {
        auto& txn = block.block.transactions[receipt.tx_index];

        // effective_gas_price contains already baseFee
        intx::uint256 base_fee = block.block.header.base_fee_per_gas.value_or(0);
        const auto effective_gas_price = txn.effective_gas_price(base_fee);

        fees += effective_gas_price * receipt.gas_used;
    }
    return fees;
}

Task<ChunkProviderResponse> ChunkProvider::get() {
    if (error_) {
        co_return ChunkProviderResponse{Bytes{0}, false, true};
    }

    if (eof_) {
        co_return ChunkProviderResponse{Bytes{0}, false, false};
    }

    KeyValue key_value;

    try {
        if (first_) {
            first_ = false;
            key_value = first_seek_key_value_;
        } else {
            if (navigate_forward_) {
                key_value = co_await cursor_->next();
            } else {
                key_value = co_await cursor_->previous();
            }
        }
    } catch (const std::exception& e) {
        error_ = true;
    }

    if (error_) {
        eof_ = true;
        co_return ChunkProviderResponse{Bytes{0}, false, true};
    }

    if (key_value.key.empty() || !key_value.key.starts_with(address_)) {
        eof_ = true;
        co_return ChunkProviderResponse{Bytes{0}, false, false};
    }

    co_return ChunkProviderResponse{key_value.value, true, false};
}

ChunkProvider::ChunkProvider(db::kv::api::Cursor* cursor, const evmc::address& address, bool navigate_forward, db::kv::api::KeyValue first_seek_key_value) {
    cursor_ = cursor;
    address_ = address;
    navigate_forward_ = navigate_forward;
    first_seek_key_value_ = std::move(first_seek_key_value);
}

Task<ChunkLocatorResponse> ChunkLocator::get(BlockNum min_block) {
    KeyValue key_value;
    try {
        key_value = co_await cursor_->seek(db::account_history_key(address_, min_block));

        if (key_value.key.empty()) {
            co_return ChunkLocatorResponse{ChunkProvider{cursor_, address_, navigate_forward_, key_value}, false, false};
        }

        co_return ChunkLocatorResponse{ChunkProvider{cursor_, address_, navigate_forward_, key_value}, true, false};

    } catch (const std::exception& e) {
        co_return ChunkLocatorResponse{ChunkProvider{cursor_, address_, navigate_forward_, key_value}, false, true};
    }
}

ChunkLocator::ChunkLocator(db::kv::api::Cursor* cursor, const evmc::address& address, bool navigate_forward) {
    cursor_ = cursor;
    address_ = address;
    navigate_forward_ = navigate_forward;
}

Task<BlockProviderResponse> ForwardBlockProvider::get() {
    if (finished_) {
        co_return BlockProviderResponse{0, false, false};
    }

    if (is_first_) {
        is_first_ = false;

        auto chunk_loc_res = co_await chunk_locator_.get(min_block_);
        chunk_provider_ = chunk_loc_res.chunk_provider;

        if (chunk_loc_res.error) {
            finished_ = true;
            co_return BlockProviderResponse{0, false, true};
        }

        if (!chunk_loc_res.ok) {
            finished_ = true;
            co_return BlockProviderResponse{0, false, false};
        }

        auto chunk_provider_res = co_await chunk_provider_.get();

        if (chunk_provider_res.error) {
            finished_ = true;
            co_return BlockProviderResponse{0, false, true};
        }

        if (!chunk_provider_res.ok) {
            finished_ = true;
            co_return BlockProviderResponse{0, false, false};
        }

        try {
            roaring::Roaring64Map bitmap = db::bitmap::parse(chunk_provider_res.chunk);

            iterator(bitmap);

            // It can happen that on the first chunk we'll get a chunk that contains
            // the first block >= minBlock in the middle of the chunk/bitmap, so we
            // skip all previous blocks before it.
            advance_if_needed(min_block_);

            // This means it is the last chunk and the min block is > the last one
            if (!has_next()) {
                finished_ = true;
                co_return BlockProviderResponse{0, false, false};
            }

        } catch (std::exception& e) {
            finished_ = true;
            co_return BlockProviderResponse{0, false, true};
        }
    }

    BlockNum next_block = next();
    bool has_next_ = has_next();

    if (!has_next_) {
        auto chunk_provider_res = co_await chunk_provider_.get();

        if (chunk_provider_res.error) {
            co_return BlockProviderResponse{0, false, true};
        }

        if (!chunk_provider_res.ok) {
            finished_ = true;
            co_return BlockProviderResponse{next_block, false, false};
        }

        has_next_ = true;

        try {
            auto bitmap = db::bitmap::parse(chunk_provider_res.chunk);
            iterator(bitmap);

        } catch (std::exception& e) {
            finished_ = true;
            co_return BlockProviderResponse{0, false, true};
        }
    }

    co_return BlockProviderResponse{next_block, has_next_, false};
}

bool ForwardBlockProvider::has_next() {
    return bitmap_index_ < bitmap_vector_.size();
}

BlockNum ForwardBlockProvider::next() {
    uint64_t result = bitmap_vector_.at(bitmap_index_);
    bitmap_index_++;
    return result;
}

void ForwardBlockProvider::iterator(roaring::Roaring64Map& bitmap) {
    bitmap_vector_.resize(bitmap.cardinality());
    bitmap.toUint64Array(bitmap_vector_.data());
    bitmap_index_ = 0;
}

void ForwardBlockProvider::advance_if_needed(BlockNum min_block) {
    auto found_index = bitmap_vector_.size();
    for (size_t i = bitmap_index_; i < bitmap_vector_.size(); i++) {
        if (bitmap_vector_.at(i) >= min_block) {
            found_index = i;
            break;
        }
    }
    bitmap_index_ = found_index;
}

Task<BlockProviderResponse> BackwardBlockProvider::get() {
    if (finished_) {
        co_return BlockProviderResponse{0, false, false};
    }

    if (is_first_) {
        is_first_ = false;

        auto chunk_loc_res = co_await chunk_locator_.get(max_block_);
        chunk_provider_ = chunk_loc_res.chunk_provider;

        if (chunk_loc_res.error) {
            finished_ = true;
            co_return BlockProviderResponse{0, false, true};
        }

        if (!chunk_loc_res.ok) {
            finished_ = true;
            co_return BlockProviderResponse{0, false, false};
        }

        auto chunk_provider_res = co_await chunk_provider_.get();

        if (chunk_provider_res.error) {
            finished_ = true;
            co_return BlockProviderResponse{0, false, true};
        }

        if (!chunk_provider_res.ok) {
            finished_ = true;
            co_return BlockProviderResponse{0, false, false};
        }

        try {
            roaring::Roaring64Map bitmap = db::bitmap::parse(chunk_provider_res.chunk);

            // It can happen that on the first chunk we'll get a chunk that contains
            // the last block <= maxBlock in the middle of the chunk/bitmap, so we
            // remove all blocks after it (since there is no AdvanceIfNeeded() in
            // IntIterable64)
            if (max_block_ != std::numeric_limits<uint64_t>::max()) {
                // bm.RemoveRange(maxBlock+1, MaxBlockNum)
                bitmap.removeRange(max_block_ + 1, std::numeric_limits<uint64_t>::max());
            }

            reverse_iterator(bitmap);

            if (!has_next()) {
                chunk_provider_res = co_await chunk_provider_.get();

                if (chunk_provider_res.error) {
                    finished_ = true;
                    co_return BlockProviderResponse{0, false, true};
                }

                if (!chunk_provider_res.ok) {
                    finished_ = true;
                    co_return BlockProviderResponse{0, false, false};
                }

                bitmap = db::bitmap::parse(chunk_provider_res.chunk);
                reverse_iterator(bitmap);
            }

        } catch (std::exception& e) {
            finished_ = true;
            co_return BlockProviderResponse{0, false, true};
        }
    }

    BlockNum next_block = next();
    bool has_next_ = has_next();

    if (!has_next_) {
        auto chunk_provider_res = co_await chunk_provider_.get();

        if (chunk_provider_res.error) {
            co_return BlockProviderResponse{0, false, true};
        }

        if (!chunk_provider_res.ok) {
            finished_ = true;
            co_return BlockProviderResponse{next_block, false, false};
        }

        has_next_ = true;

        try {
            auto bitmap = db::bitmap::parse(chunk_provider_res.chunk);
            reverse_iterator(bitmap);

        } catch (std::exception& e) {
            finished_ = true;
            co_return BlockProviderResponse{0, false, true};
        }
    }

    co_return BlockProviderResponse{next_block, has_next_, false};
}

bool BackwardBlockProvider::has_next() {
    return bitmap_index_ < bitmap_vector_.size();
}

uint64_t BackwardBlockProvider::next() {
    uint64_t result = bitmap_vector_.at(bitmap_index_);
    bitmap_index_++;
    return result;
}

void BackwardBlockProvider::reverse_iterator(roaring::Roaring64Map& bitmap) {
    bitmap_vector_.resize(bitmap.cardinality());
    bitmap.toUint64Array(bitmap_vector_.data());
    std::reverse(bitmap_vector_.begin(), bitmap_vector_.end());
    bitmap_index_ = 0;
}

Task<BlockProviderResponse> FromToBlockProvider::get() {
    if (!initialized_) {
        initialized_ = true;

        auto from_prov_res = co_await callFromProvider_->get();
        if (from_prov_res.error) {
            co_return BlockProviderResponse{0, false, true};
        }
        next_from_ = from_prov_res.block_number;
        has_more_from_ = from_prov_res.has_more || next_from_ != 0;

        auto to_prov_res = co_await callToProvider_->get();
        if (to_prov_res.error) {
            co_return BlockProviderResponse{0, false, true};
        }
        next_to_ = to_prov_res.block_number;
        has_more_to_ = to_prov_res.has_more || next_to_ != 0;
    }

    if (!has_more_from_ && !has_more_to_) {
        co_return BlockProviderResponse{0, false, true};
    }

    BlockNum block_num{0};

    if (!has_more_from_) {
        block_num = next_to_;
    } else if (!has_more_to_) {
        block_num = next_from_;
    } else {
        block_num = next_from_;
        if (is_backwards_) {
            if (next_to_ < next_from_) {
                block_num = next_to_;
            }
        } else {
            if (next_to_ > next_from_) {
                block_num = next_to_;
            }
        }
    }

    // Pull next; it may be that from AND to contains the same blockNum
    if (has_more_from_ && block_num == next_from_) {
        auto from_prov_res = co_await callFromProvider_->get();

        if (from_prov_res.error) {
            co_return BlockProviderResponse{0, false, true};
        }
        next_from_ = from_prov_res.block_number;
        has_more_from_ = from_prov_res.has_more || next_from_ != 0;
    }

    if (has_more_to_ && block_num == next_to_) {
        auto to_prov_res = co_await callToProvider_->get();

        if (to_prov_res.error) {
            co_return BlockProviderResponse{0, false, true};
        }

        next_to_ = to_prov_res.block_number;
        has_more_to_ = to_prov_res.has_more || next_to_ != 0;
    }

    co_return BlockProviderResponse{block_num, has_more_from_ || has_more_to_, false};
}

FromToBlockProvider::FromToBlockProvider(bool is_backwards, BlockProvider* callFromProvider, BlockProvider* callToProvider) {
    is_backwards_ = is_backwards;
    callFromProvider_ = callFromProvider;
    callToProvider_ = callToProvider;
    initialized_ = false;
}

}  // namespace silkworm::rpc::commands
