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

#include "state_reader.hpp"

#include <silkworm/infra/concurrency/task.hpp>

#include <catch2/catch_test_macros.hpp>
#include <evmc/evmc.hpp>
#include <gmock/gmock.h>

#include <silkworm/core/common/base.hpp>
#include <silkworm/db/tables.hpp>
#include <silkworm/db/test_util/mock_transaction.hpp>
#include <silkworm/infra/test_util/context_test_base.hpp>
#include <silkworm/rpc/common/util.hpp>

namespace silkworm::db::state {

using kv::api::KeyValue;
using testing::_;
using testing::InvokeWithoutArgs;

#ifndef SILKWORM_SANITIZE
static const evmc::address kZeroAddress{};
static const Bytes kEncodedAccount{*from_hex(
    "0f01020203e8010520f1885eda54b7a053318cd41e2093220dab15d65381b1157a3633a83bfd5c9239")};
static const Bytes kEncodedAccountHistory{*from_hex(
    "0100000000000000000000003a300000020000006e001b006f000f001800000050000000e042e442f5420e4320"
    "432d433c4343435e436343e550e750eb50a160a4604f6175c504c6c7cedaceeccedbd0f3d0f4d001d104d105d1"
    "09d18613b113061e801f861fd11fda1fe6323833ac3943651f67226c406d36706f70")};
static const Bytes kEncodedAccountWithoutCodeHash{*from_hex(
    "07023039080de0b6b3a76400000105")};

static const evmc::bytes32 kLocationHash{};
static const Bytes kStorageLocation{*from_hex(
    "0000000000000000000000000000000000000000000000000000000000000000")};
static const Bytes kEncodedStorageHistory{*from_hex(
    "0100000000000000000000003a3000000700000044000a004600010048000100490005004c0001004d0001005e"
    "00000040000000560000005a0000005e0000006a0000006e000000720000005da562a563a565a567a56aa59da5"
    "a0a5f0a5f5a57ef926a863a8eb520b535d1b951bb71b3c1c741caa4f53f5b0f5184f536018f6")};

static const Bytes kBinaryCode{*from_hex("0x60045e005c60016000555d")};
static const evmc::bytes32 kCodeHash{0xef722d9baf50b9983c2fce6329c5a43a15b8d5ba79cd792e7199d615be88284d_bytes32};

struct StateReaderTest : public silkworm::test_util::ContextTestBase {
    db::test_util::MockTransaction transaction_;
    StateReader state_reader_{transaction_};
};

TEST_CASE_METHOD(StateReaderTest, "StateReader::read_account") {
    SECTION("no account for history empty and current state empty") {
        // Set the call expectations:
        // 1. DatabaseReader::get call on kAccountHistory returns empty key-value
        EXPECT_CALL(transaction_, get(db::table::kAccountHistoryName, _)).WillOnce(InvokeWithoutArgs([]() -> Task<KeyValue> {
            co_return KeyValue{};
        }));
        // 2. DatabaseReader::get_one call on kPlainState returns empty value
        EXPECT_CALL(transaction_, get_one(db::table::kPlainStateName, ByteView{kZeroAddress.bytes})).WillOnce(InvokeWithoutArgs([]() -> Task<Bytes> {
            co_return Bytes{};
        }));

        // Execute the test: calling read_account should return no account
        std::optional<Account> account;
        CHECK_NOTHROW(account = spawn_and_wait(state_reader_.read_account(kZeroAddress, kEarliestBlockNumber)));
        CHECK(!account);
    }

    SECTION("account found in current state") {
        // Set the call expectations:
        // 1. DatabaseReader::get call on kAccountHistory returns empty key-value
        EXPECT_CALL(transaction_, get(db::table::kAccountHistoryName, _)).WillOnce(InvokeWithoutArgs([]() -> Task<KeyValue> {
            co_return KeyValue{};
        }));
        // 2. DatabaseReader::get_one call on kPlainState returns account data
        EXPECT_CALL(transaction_, get_one(db::table::kPlainStateName, ByteView{kZeroAddress.bytes})).WillOnce(InvokeWithoutArgs([]() -> Task<Bytes> {
            co_return kEncodedAccount;
        }));

        // Execute the test: calling read_account should return the expected account
        std::optional<Account> account;
        CHECK_NOTHROW(account = spawn_and_wait(state_reader_.read_account(kZeroAddress, kEarliestBlockNumber)));
        CHECK(account);
        if (account) {
            CHECK(account->nonce == 2);
            CHECK(account->balance == 1000);
            CHECK(account->code_hash == 0xf1885eda54b7a053318cd41e2093220dab15d65381b1157a3633a83bfd5c9239_bytes32);
            CHECK(account->incarnation == 5);
        }
    }

    SECTION("account found in history") {
        // Set the call expectations:
        // 1. DatabaseReader::get call on kAccountHistory returns the account bitmap
        EXPECT_CALL(transaction_, get(db::table::kAccountHistoryName, _)).WillOnce(InvokeWithoutArgs([]() -> Task<KeyValue> {
            co_return KeyValue{Bytes{ByteView{kZeroAddress.bytes}}, kEncodedAccountHistory};
        }));
        // 2. DatabaseReader::get_both_range call on kPlainAccountChangeSet returns the account data
        EXPECT_CALL(transaction_, get_both_range(db::table::kAccountChangeSetName, _, _)).WillOnce(InvokeWithoutArgs([]() -> Task<std::optional<Bytes>> {
            co_return kEncodedAccount;
        }));

        // Execute the test: calling read_account should return expected account
        std::optional<Account> account;
        CHECK_NOTHROW(account = spawn_and_wait(state_reader_.read_account(kZeroAddress, kEarliestBlockNumber)));
        CHECK(account);
        if (account) {
            CHECK(account->nonce == 2);
            CHECK(account->balance == 1000);
            CHECK(account->code_hash == 0xf1885eda54b7a053318cd41e2093220dab15d65381b1157a3633a83bfd5c9239_bytes32);
            CHECK(account->incarnation == 5);
        }
    }

    SECTION("account w/o code hash found current state") {
        // Set the call expectations:
        // 1. DatabaseReader::get call on kAccountHistory returns empty key-value
        EXPECT_CALL(transaction_, get(db::table::kAccountHistoryName, _)).WillOnce(InvokeWithoutArgs([]() -> Task<KeyValue> {
            co_return KeyValue{};
        }));
        // 2. DatabaseReader::get_one call on kPlainState returns account data
        EXPECT_CALL(transaction_, get_one(db::table::kPlainStateName, ByteView{kZeroAddress.bytes})).WillOnce(InvokeWithoutArgs([]() -> Task<Bytes> {
            co_return kEncodedAccountWithoutCodeHash;
        }));
        // 3. DatabaseReader::get_one call on kPlainContractCode returns account code hash
        EXPECT_CALL(transaction_, get_one(db::table::kPlainCodeHashName, _)).WillOnce(InvokeWithoutArgs([]() -> Task<Bytes> {
            co_return Bytes{kCodeHash.bytes, kHashLength};
        }));

        // Execute the test: calling read_account should return the expected account
        std::optional<Account> account;
        CHECK_NOTHROW(account = spawn_and_wait(state_reader_.read_account(kZeroAddress, kEarliestBlockNumber)));
        CHECK(account);
        if (account) {
            CHECK(account->nonce == 12345);
            CHECK(account->balance == kEther);
            CHECK(account->code_hash == kCodeHash);
            CHECK(account->incarnation == 5);
        }
    }
}

TEST_CASE_METHOD(StateReaderTest, "StateReader::read_storage") {
    SECTION("empty storage for history empty and current state empty") {
        // Set the call expectations:
        // 1. DatabaseReader::get call on kStorageHistory returns empty key-value
        EXPECT_CALL(transaction_, get(db::table::kStorageHistoryName, _)).WillOnce(InvokeWithoutArgs([]() -> Task<KeyValue> {
            co_return KeyValue{};
        }));
        // 2. DatabaseReader::get_both_range call on kPlainState returns empty value
        EXPECT_CALL(transaction_, get_both_range(db::table::kPlainStateName, _, _)).WillOnce(InvokeWithoutArgs([]() -> Task<std::optional<Bytes>> {
            co_return Bytes{};
        }));

        // Execute the test: calling read_storage should return empty storage value
        evmc::bytes32 location;
        CHECK_NOTHROW(location = spawn_and_wait(state_reader_.read_storage(kZeroAddress, 0, kLocationHash, kEarliestBlockNumber)));
        CHECK(location == evmc::bytes32{});
    }

    SECTION("storage found in current state") {
        // Set the call expectations:
        // 1. DatabaseReader::get call on kStorageHistory returns empty key-value
        EXPECT_CALL(transaction_, get(db::table::kStorageHistoryName, _)).WillOnce(InvokeWithoutArgs([]() -> Task<KeyValue> {
            co_return KeyValue{};
        }));
        // 2. DatabaseReader::get_both_range call on kPlainState returns empty value
        EXPECT_CALL(transaction_, get_both_range(db::table::kPlainStateName, _, _)).WillOnce(InvokeWithoutArgs([]() -> Task<std::optional<Bytes>> {
            co_return kStorageLocation;
        }));

        // Execute the test: calling read_storage should return expected storage location
        evmc::bytes32 location;
        CHECK_NOTHROW(location = spawn_and_wait(state_reader_.read_storage(kZeroAddress, 0, kLocationHash, kEarliestBlockNumber)));
        CHECK(location == to_bytes32(kStorageLocation));
    }

    SECTION("storage found in history") {
        // Set the call expectations:
        // 1. DatabaseReader::get call on kStorageHistory returns the storage bitmap
        EXPECT_CALL(transaction_, get(db::table::kStorageHistoryName, _)).WillOnce(InvokeWithoutArgs([]() -> Task<KeyValue> {
            co_return KeyValue{
                db::storage_history_key(kZeroAddress, kLocationHash, kEarliestBlockNumber),
                kEncodedStorageHistory};
        }));
        // 2. DatabaseReader::get_both_range call on kPlainAccountChangeSet the storage location value
        EXPECT_CALL(transaction_, get_both_range(db::table::kStorageChangeSetName, _, _)).WillOnce(InvokeWithoutArgs([]() -> Task<std::optional<Bytes>> {
            co_return kStorageLocation;
        }));

        // Execute the test: calling read_storage should return expected storage location
        evmc::bytes32 location;
        CHECK_NOTHROW(location = spawn_and_wait(state_reader_.read_storage(kZeroAddress, 0, kLocationHash, kEarliestBlockNumber)));
        CHECK(location == to_bytes32(kStorageLocation));
    }
}

TEST_CASE_METHOD(StateReaderTest, "StateReader::read_code") {
    SECTION("no code for empty code hash") {
        // Execute the test: calling read_code should return no code for empty hash
        std::optional<Bytes> code;
        CHECK_NOTHROW(code = spawn_and_wait(state_reader_.read_code(kEmptyHash)));
        CHECK(!code);
    }

    SECTION("empty code found for code hash") {
        // Set the call expectations:
        // 1. DatabaseReader::get_one call on kCode returns the binary code
        EXPECT_CALL(transaction_, get_one(db::table::kCodeName, _)).WillOnce(InvokeWithoutArgs([]() -> Task<Bytes> {
            co_return Bytes{};
        }));

        // Execute the test: calling read_code should return an empty code
        std::optional<Bytes> code;
        CHECK_NOTHROW(code = spawn_and_wait(state_reader_.read_code(kCodeHash)));
        CHECK(code);
        if (code) {
            CHECK(code->empty());
        }
    }

    SECTION("non-empty code found for code hash") {
        // Set the call expectations:
        // 1. DatabaseReader::get_one call on kCode returns the binary code
        EXPECT_CALL(transaction_, get_one(db::table::kCodeName, _)).WillOnce(InvokeWithoutArgs([]() -> Task<Bytes> {
            co_return kBinaryCode;
        }));

        // Execute the test: calling read_code should return a non-empty code
        std::optional<Bytes> code;
        CHECK_NOTHROW(code = spawn_and_wait(state_reader_.read_code(kCodeHash)));
        CHECK(code);
        if (code) {
            CHECK(to_hex(*code) == to_hex(kBinaryCode));
        }
    }
}
#endif  // SILKWORM_SANITIZE

}  // namespace silkworm::db::state