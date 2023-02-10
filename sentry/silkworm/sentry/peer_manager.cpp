/*
   Copyright 2022 The Silkworm Authors

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

#include "peer_manager.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/system_error.hpp>

#include <silkworm/common/log.hpp>
#include <silkworm/sentry/common/awaitable_wait_for_all.hpp>
#include <silkworm/sentry/common/random.hpp>

namespace silkworm::sentry {

using namespace boost::asio;

awaitable<void> PeerManager::start(rlpx::Server& server, rlpx::Client& client) {
    using namespace common::awaitable_wait_for_all;

    auto start =
        start_in_strand(server.peer_channel()) &&
        start_in_strand(client.peer_channel()) &&
        peer_tasks_.wait();
    co_await co_spawn(strand_, std::move(start), use_awaitable);
}

awaitable<void> PeerManager::start_in_strand(common::Channel<std::shared_ptr<rlpx::Peer>>& peer_channel) {
    // loop until receive() throws a cancelled exception
    while (true) {
        auto peer = co_await peer_channel.receive();

        if (peers_.size() >= max_peers_) {
            peer_tasks_.spawn(strand_, drop_peer(peer, DisconnectReason::TooManyPeers));
            continue;
        }

        peers_.push_back(peer);
        on_peer_added(peer);
        peer_tasks_.spawn(strand_, start_peer(peer));
    }
}

boost::asio::awaitable<void> PeerManager::start_peer(const std::shared_ptr<rlpx::Peer>& peer) {
    try {
        co_await rlpx::Peer::start(peer);
    } catch (const boost::system::system_error& ex) {
        if (ex.code() == boost::system::errc::operation_canceled) {
            log::Debug() << "PeerManager::start_peer Peer::start cancelled";
        } else {
            log::Error() << "PeerManager::start_peer Peer::start system_error: " << ex.what();
        }
    } catch (const std::exception& ex) {
        log::Error() << "PeerManager::start_peer Peer::start exception: " << ex.what();
    }

    peers_.remove(peer);
    on_peer_removed(peer);
}

boost::asio::awaitable<void> PeerManager::drop_peer(
    const std::shared_ptr<rlpx::Peer>& peer,
    DisconnectReason reason) {
    try {
        co_await rlpx::Peer::drop(peer, reason);
    } catch (const boost::system::system_error& ex) {
        if (ex.code() == boost::system::errc::operation_canceled) {
            log::Debug() << "PeerManager::drop_peer Peer::drop cancelled";
        } else {
            log::Error() << "PeerManager::drop_peer Peer::drop system_error: " << ex.what();
        }
    } catch (const std::exception& ex) {
        log::Error() << "PeerManager::drop_peer Peer::drop exception: " << ex.what();
    }
}

size_t PeerManager::max_peer_tasks(size_t max_peers) {
    static const size_t kMaxSimultaneousDropPeerTasks = 10;
    return max_peers + kMaxSimultaneousDropPeerTasks;
}

awaitable<void> PeerManager::enumerate_peers(EnumeratePeersCallback callback) {
    co_await co_spawn(strand_, enumerate_peers_in_strand(callback), use_awaitable);
}

awaitable<void> PeerManager::enumerate_random_peers(size_t max_count, EnumeratePeersCallback callback) {
    co_await co_spawn(strand_, enumerate_random_peers_in_strand(max_count, callback), use_awaitable);
}

awaitable<void> PeerManager::enumerate_peers_in_strand(EnumeratePeersCallback callback) {
    for (auto& peer : peers_) {
        callback(peer);
    }
    co_return;
}

awaitable<void> PeerManager::enumerate_random_peers_in_strand(size_t max_count, EnumeratePeersCallback callback) {
    for (auto peer_ptr : common::random_list_items(peers_, max_count)) {
        callback(*peer_ptr);
    }
    co_return;
}

void PeerManager::add_observer(std::weak_ptr<PeerManagerObserver> observer) {
    std::scoped_lock lock(observers_mutex_);
    observers_.push_back(std::move(observer));
}

[[nodiscard]] std::list<std::shared_ptr<PeerManagerObserver>> PeerManager::observers() {
    std::scoped_lock lock(observers_mutex_);
    std::list<std::shared_ptr<PeerManagerObserver>> observers;
    for (auto& weak_observer : observers_) {
        auto observer = weak_observer.lock();
        if (observer) {
            observers.push_back(observer);
        }
    }
    return observers;
}

void PeerManager::on_peer_added(std::shared_ptr<rlpx::Peer> peer) {
    for (auto& observer : observers()) {
        observer->on_peer_added(std::move(peer));
    }
}

void PeerManager::on_peer_removed(std::shared_ptr<rlpx::Peer> peer) {
    for (auto& observer : observers()) {
        observer->on_peer_removed(std::move(peer));
    }
}

}  // namespace silkworm::sentry