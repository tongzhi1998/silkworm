#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include <CLI/CLI.hpp>
#include <brotli/decode.h>
#include <brotli/encode.h>
#include <brotli/types.h>

#include <silkworm/db/snapshot_bundle_factory_impl.hpp>
#include <silkworm/core/execution/execution.hpp>
#include <silkworm/core/protocol/rule_set.hpp>
#include <silkworm/infra/common/directories.hpp>
#include <silkworm/db/access_layer.hpp>
#include <silkworm/db/buffer.hpp>
#include <silkworm/db/snapshots/repository.hpp>
#include <silkworm/db/snapshots/settings.hpp>

using namespace silkworm;

class FileDb final {
    class Impl;

    std::unique_ptr<Impl> impl_;

  public:
    FileDb(char const* dir);
    ~FileDb();

    std::optional<std::string> get(char const* key) const;

    void upsert(char const* key, std::string_view value) const;
    bool remove(char const* key) const;
};

class FileDb::Impl {
    std::filesystem::path const dir_;

  public:
    explicit Impl(char const* const dir)
        : dir_{dir} {
        std::filesystem::create_directories(dir_);
    }

    std::optional<std::string> get(char const* const key) const {
        auto const path = dir_ / key;
        std::ifstream in{path, std::ios::in | std::ios::binary};
        if (!in) {
            return std::nullopt;
        }
        std::string value;
        in.seekg(0, std::ios::end);
        auto const pos = in.tellg();
        value.resize(static_cast<size_t>(pos));
        in.seekg(0, std::ios::beg);
        in.read(&value[0], static_cast<std::streamsize>(value.size()));
        in.close();
        return value;
    }

    void upsert(char const* const key, std::string_view const value) const {
        auto const path = dir_ / key;
        std::stringstream temp_name;
        temp_name << '_' << key << '.' << std::this_thread::get_id();
        auto const temp_path = dir_ / temp_name.str();
        std::ofstream out{
            temp_path, std::ios::out | std::ios::trunc | std::ios::binary};
        out.write(&value[0], static_cast<std::streamsize>(value.size()));
        out.close();
        std::filesystem::rename(temp_path, path);
    }

    bool remove(char const* const key) const {
        auto const path = dir_ / key;
        return std::filesystem::remove(path);
    }
};

FileDb::FileDb(char const* const dir)
    : impl_{new Impl{dir}} {
}

FileDb::~FileDb() = default;

std::optional<std::string> FileDb::get(char const* const key) const {
    return impl_->get(key);
}

void FileDb::upsert(char const* const key, std::string_view const value) const {
    impl_->upsert(key, value);
}

bool FileDb::remove(char const* const key) const {
    return impl_->remove(key);
}

class BlockDb {
    FileDb db_;

  public:
    BlockDb(std::filesystem::path const&);

    bool get(uint64_t, Block&) const;

    void upsert(uint64_t, Block const&) const;
    bool remove(uint64_t) const;
};

BlockDb::BlockDb(std::filesystem::path const& dir)
    : db_{dir.c_str()} {
}

void BlockDb::upsert(
    silkworm::BlockNum const num, silkworm::Block const& block) const {
    auto const key = std::to_string(num);
    silkworm::Bytes bytes;
    silkworm::rlp::encode(bytes, block);
    size_t brotli_size = BrotliEncoderMaxCompressedSize(bytes.size());
    SILKWORM_ASSERT(brotli_size);
    silkworm::Bytes brotli_buffer;
    brotli_buffer.resize(brotli_size);
    auto const brotli_result = BrotliEncoderCompress(
        BROTLI_DEFAULT_QUALITY,
        BROTLI_DEFAULT_WINDOW,
        BROTLI_MODE_GENERIC,
        bytes.size(),
        bytes.data(),
        &brotli_size,
        brotli_buffer.data());
    SILKWORM_ASSERT(brotli_result == BROTLI_TRUE);
    brotli_buffer.resize(brotli_size);
    std::string_view const value{
        reinterpret_cast<char const*>(brotli_buffer.data()),
        brotli_buffer.size()};
    db_.upsert(key.c_str(), value);
}

static std::unique_ptr<snapshots::SnapshotBundleFactory> bundle_factory() {
    return std::make_unique<db::SnapshotBundleFactoryImpl>();
}

int main(int argc, char* argv[]) {
    CLI::App app{"Executes Ethereum blocks and scans txs for errored txs"};
    using namespace silkworm;

    std::string chaindata{DataDirectory{}.chaindata().path().string()};
    app.add_option("--chaindata", chaindata, "Path to a database populated by Erigon")
        ->capture_default_str()
        ->check(CLI::ExistingDirectory);

    uint64_t from{1};
    app.add_option("--from", from, "start from block number (inclusive)");

    uint64_t to{UINT64_MAX};
    app.add_option("--to", to, "check up to block number (exclusive)");

    std::filesystem::path block_db_path;
    app.add_option("--block_db", block_db_path, "output block db path");

    CLI11_PARSE(app, argc, argv);

    if (from > to) {
        std::cerr << "--from (" << from << ") must be less than or equal to --to (" << to << ").\n";
        return -1;
    }

    int retvar{0};

    try {
        auto data_dir{DataDirectory::from_chaindata(chaindata)};
        BlockDb block_db{block_db_path};
        db::EnvConfig db_config{data_dir.chaindata().path().string()};
        auto env{db::open_env(db_config)};
        db::ROTxnManaged txn{env};

        std::cerr << "Path of db_config: " << data_dir.chaindata().path().string() << std::endl;

        db::DataModel data_model(txn);

        snapshots::SnapshotSettings setting{};
        setting.repository_dir = data_dir.snapshots().path();

        snapshots::SnapshotRepository repository{setting, bundle_factory()};
        repository.reopen_folder();

        data_model.set_snapshot_repository(&repository);

        for (uint64_t block_num{from}; block_num < to; block_num++) {
            Block block;
            if (!data_model.read_block_from_snapshot(block_num, block)) {
                std::cout << "Failed: " << block_num << std::endl;
                break;
            } else {
                block_db.upsert(block_num, block);
            }
        }

    } catch (std::exception& ex) {
        std::cout << ex.what() << std::endl;
        retvar = -1;
    }

    return retvar;
}