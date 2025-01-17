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

#include "receipts.hpp"

#include <silkworm/core/types/address.hpp>
#include <silkworm/core/types/evmc_bytes32.hpp>
#include <silkworm/db/tables.hpp>
#include <silkworm/db/util.hpp>
#include <silkworm/infra/common/log.hpp>
#include <silkworm/rpc/ethdb/cbor.hpp>
#include <silkworm/rpc/ethdb/walk.hpp>

namespace silkworm::rpc::core {

using ethdb::walk;

Task<Receipts> get_receipts(db::kv::api::Transaction& tx, const silkworm::BlockWithHash& block_with_hash) {
    const auto cached_receipts = co_await read_receipts(tx, block_with_hash);
    if (cached_receipts) {
        co_return *cached_receipts;
    }

    // If not already present, retrieve receipts by executing transactions
    // auto block = co_await core::rawdb::read_block(tx, hash, number);
    // TODO(canepat): implement
    SILK_WARN << "retrieve receipts by executing transactions NOT YET IMPLEMENTED";
    co_return Receipts{};
}

Task<std::optional<Receipts>> read_receipts(db::kv::api::Transaction& tx, const silkworm::BlockWithHash& block_with_hash) {
    const evmc::bytes32 block_hash = block_with_hash.hash;
    uint64_t block_number = block_with_hash.block.header.number;
    auto raw_receipts = co_await read_raw_receipts(tx, block_number);
    if (!raw_receipts || raw_receipts->empty()) {
        co_return raw_receipts;
    }
    auto& receipts = *raw_receipts;

    // Add derived fields to the receipts
    auto& transactions = block_with_hash.block.transactions;
    SILK_DEBUG << "#transactions=" << block_with_hash.block.transactions.size() << " #receipts=" << receipts.size();
    if (transactions.size() != receipts.size()) {
        throw std::runtime_error{"#transactions and #receipts do not match in read_receipts"};
    }
    uint32_t log_index{0};
    for (size_t i{0}; i < receipts.size(); i++) {
        // The tx hash can be calculated by the tx content itself
        auto tx_hash{transactions[i].hash()};
        receipts[i].tx_hash = to_bytes32(tx_hash.bytes);
        receipts[i].tx_index = uint32_t(i);

        receipts[i].block_hash = block_hash;
        receipts[i].block_number = block_number;

        // When tx receiver is not set, create a contract with address depending on tx sender and its nonce
        if (!transactions[i].to.has_value()) {
            receipts[i].contract_address = create_address(*transactions[i].sender(), transactions[i].nonce);
        }

        // The gas used can be calculated by the previous receipt
        if (i == 0) {
            receipts[i].gas_used = receipts[i].cumulative_gas_used;
        } else {
            receipts[i].gas_used = receipts[i].cumulative_gas_used - receipts[i - 1].cumulative_gas_used;
        }

        receipts[i].from = transactions[i].sender();
        receipts[i].to = transactions[i].to;
        receipts[i].type = static_cast<uint8_t>(transactions[i].type);

        // The derived fields of receipt are taken from block and transaction
        for (size_t j{0}; j < receipts[i].logs.size(); j++) {
            receipts[i].logs[j].block_number = block_number;
            receipts[i].logs[j].block_hash = block_hash;
            receipts[i].logs[j].tx_hash = receipts[i].tx_hash;
            receipts[i].logs[j].tx_index = uint32_t(i);
            receipts[i].logs[j].index = log_index++;
            receipts[i].logs[j].removed = false;
        }
    }

    co_return raw_receipts;
}

Task<std::optional<Receipts>> read_raw_receipts(db::kv::api::Transaction& tx, BlockNum block_number) {
    const auto block_key = db::block_key(block_number);
    const auto data = co_await tx.get_one(db::table::kBlockReceiptsName, block_key);
    SILK_TRACE << "read_raw_receipts data: " << silkworm::to_hex(data);
    if (data.empty()) {
        co_return std::nullopt;
    }

    Receipts receipts{};
    const bool decoding_ok{cbor_decode(data, receipts)};
    if (!decoding_ok) {
        throw std::runtime_error("cannot decode raw receipts in block: " + std::to_string(block_number));
    }
    SILK_TRACE << "#receipts: " << receipts.size();
    if (receipts.empty()) {
        co_return receipts;
    }

    auto log_key = db::log_key(block_number, 0);
    SILK_DEBUG << "log_key: " << silkworm::to_hex(log_key);
    auto walker = [&](const silkworm::Bytes& k, const silkworm::Bytes& v) {
        if (k.size() != sizeof(uint64_t) + sizeof(uint32_t)) {
            return false;
        }
        auto tx_id = endian::load_big_u32(&k[sizeof(uint64_t)]);
        const bool decode_ok{cbor_decode(v, receipts[tx_id].logs)};
        if (!decode_ok) {
            SILK_WARN << "cannot decode logs for receipt: " << tx_id << " in block: " << block_number;
            return false;
        }
        receipts[tx_id].bloom = bloom_from_logs(receipts[tx_id].logs);
        SILK_DEBUG << "#receipts[" << tx_id << "].logs: " << receipts[tx_id].logs.size();
        return true;
    };
    co_await walk(tx, db::table::kLogsName, log_key, 8 * CHAR_BIT, walker);

    co_return receipts;
}

}  // namespace silkworm::rpc::core
