/*
 * 2026 BFA-HavenCore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef Bots_BotSession_h__
#define Bots_BotSession_h__

#include "BotTypes.h"
#include "ObjectGuid.h"
#include <boost/asio/ip/tcp.hpp>
#include <atomic>
#include <memory>
#include <string>

class WorldSession;
class WorldSocket;
class Player;

namespace Bots
{
    /// The module's handle on one bot's core `WorldSession` and its two loopback
    /// sockets. It is deliberately thin: the login state machine lives in
    /// `BotManager`, and this class only exposes the thread-safe operations the
    /// manager performs on a live session.
    ///
    /// This is *not* a `WorldSession` subclass — the core stores a concrete
    /// session type (WorldSession.h:2149) and there is nothing useful to
    /// override. `_session` is a non-owning pointer: `World::AddSession` takes
    /// ownership and the world thread may `delete` it (World.cpp UpdateSessions),
    /// so every method here re-checks liveness through the manager before use.
    ///
    /// Threading: `InjectCharacterEnum`, `InjectPlayerLogin`,
    /// `LinkInstanceAndContinueLogin`, `DrainSockets` and `KeepAlive` run on the
    /// module worker thread. `OnCharacterListSent` and `OnTimeSyncRequest` run on
    /// the core world thread from the `ScriptMgr::OnPacketSend` hook. They meet
    /// only through `_characterListReady` (atomic) and `WorldSession::QueuePacket`
    /// (a `LockedQueue`).
    class BotSession
    {
    public:
        BotSession(uint32 accountId, std::string accountName, ObjectGuid characterGuid, std::string characterName);
        ~BotSession();

        BotSession(BotSession const&) = delete;
        BotSession& operator=(BotSession const&) = delete;

        uint32 GetAccountId() const { return _accountId; }
        std::string const& GetAccountName() const { return _accountName; }
        ObjectGuid GetCharacterGuid() const { return _characterGuid; }
        std::string const& GetCharacterName() const { return _characterName; }

        /// Takes the realm and instance sockets. Called once, on the module
        /// thread, before the `WorldSession` is constructed. The peers are the
        /// other ends of the loopback stubs and must be retained for as long as
        /// the sockets live (see BotSocket.h).
        void SetSockets(std::shared_ptr<WorldSocket> realmSocket, std::shared_ptr<boost::asio::ip::tcp::socket> realmPeer,
            std::shared_ptr<WorldSocket> instanceSocket, std::shared_ptr<boost::asio::ip::tcp::socket> instancePeer);

        /// Adopts the core session pointer immediately after construction and
        /// before `World::AddSession`.
        void SetWorldSession(WorldSession* session) { _session = session; }
        WorldSession* GetWorldSession() const { return _session; }

        /// World-thread signals, from ScriptMgr::OnPacketSend.
        void OnCharacterListSent() { _characterListReady = true; }
        void OnTimeSyncRequest(uint32 sequenceIndex);

        /// Module-thread operations. Each is a no-op if the session is gone.
        void InjectCharacterEnum();
        void InjectPlayerLogin();
        void LinkInstanceAndContinueLogin();
        void DrainSockets();
        void KeepAlive();

        bool IsCharacterListReady() const { return _characterListReady; }
        bool IsInstanceLinked() const { return _instanceLinked; }

        /// Best-effort liveness. The module holds the realm socket by
        /// shared_ptr, so it outlives the WorldSession, and Socket::IsOpen reads
        /// an std::atomic<bool> — checking it never dereferences the (possibly
        /// deleted) session. The core closes the realm socket before it deletes a
        /// session, so a closed socket means "stop touching _session". Under
        /// normal operation the module keeps it open (KeepAlive) and the world
        /// thread never deletes the session at all.
        bool IsRealmSocketOpen() const;

        /// Observable login progress, read on the module thread. `GetPlayer()` is
        /// public (WorldSession.h:1107) and written once by the world thread when
        /// the character finishes loading, so polling it is benign.
        bool HasPlayer() const;
        bool IsPlayerInWorld() const;

        /// Runtime self-check (docs/00-DESIGN.md §9): the character the core
        /// actually loaded must be the one this bot asked for.
        bool LoadedCharacterMatchesRequest() const;

    private:
        uint32 _accountId;
        std::string _accountName;
        ObjectGuid _characterGuid;
        std::string _characterName;

        WorldSession* _session = nullptr;
        std::shared_ptr<WorldSocket> _realmSocket;
        std::shared_ptr<boost::asio::ip::tcp::socket> _realmPeer;
        std::shared_ptr<WorldSocket> _instanceSocket;
        std::shared_ptr<boost::asio::ip::tcp::socket> _instancePeer;

        // Written by the world thread (OnPacketSend), read by the module thread.
        std::atomic<bool> _characterListReady{ false };
        // Module-thread only: set once the instance socket has been linked so
        // LinkInstanceAndContinueLogin does not run twice.
        bool _instanceLinked = false;
    };
}

#endif // Bots_BotSession_h__
