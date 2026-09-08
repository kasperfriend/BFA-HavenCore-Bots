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

#include "BotSession.h"
#include "Define.h"
#include "WorldSession.h"
#include "WorldSocket.h"
#include "WorldPacket.h"
#include "Player.h"
#include "Opcodes.h"
#include "Log.h"
#include <chrono>

namespace Bots
{
    namespace
    {
        uint32 NowMs()
        {
            return static_cast<uint32>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
        }
    }

    BotSession::BotSession(uint32 accountId, std::string accountName, ObjectGuid characterGuid, std::string characterName) :
        _accountId(accountId), _accountName(std::move(accountName)),
        _characterGuid(characterGuid), _characterName(std::move(characterName))
    {
    }

    BotSession::~BotSession() = default;

    void BotSession::SetSockets(std::shared_ptr<WorldSocket> realmSocket, std::shared_ptr<boost::asio::ip::tcp::socket> realmPeer,
        std::shared_ptr<WorldSocket> instanceSocket, std::shared_ptr<boost::asio::ip::tcp::socket> instancePeer)
    {
        _realmSocket = std::move(realmSocket);
        _realmPeer = std::move(realmPeer);
        _instanceSocket = std::move(instanceSocket);
        _instancePeer = std::move(instancePeer);
    }

    void BotSession::OnTimeSyncRequest(uint32 sequenceIndex)
    {
        if (!_session)
            return;

        // CMSG_TIME_SYNC_RESPONSE is "uint32 SequenceIndex; uint32 ClientTime" —
        // the read order in MiscPackets.cpp:135-139, which differs from the
        // member declaration order. QueuePacket is thread-safe, so answering on
        // the world thread (where OnPacketSend fires) is correct.
        WorldPacket* response = new WorldPacket(CMSG_TIME_SYNC_RESPONSE, 8);
        *response << sequenceIndex << NowMs();
        _session->QueuePacket(response);
    }

    void BotSession::InjectCharacterEnum()
    {
        if (!_session)
            return;

        // CMSG_ENUM_CHARACTERS carries no body (EnumCharacters::Read is empty),
        // and it is PROCESS_THREADUNSAFE, so the world thread runs
        // HandleCharEnumOpcode out of _recvQueue on its next UpdateSessions.
        _session->QueuePacket(new WorldPacket(CMSG_ENUM_CHARACTERS, 0));
    }

    void BotSession::InjectPlayerLogin()
    {
        if (!_session)
            return;

        // Body is ObjectGuid then float FarClip (CharacterPackets.cpp:418-422).
        // operator<< writes the mask+packed form that PlayerLogin::Read's
        // operator>> expects. FarClip only sets a view distance; the default the
        // client sends is used here.
        WorldPacket* login = new WorldPacket(CMSG_PLAYER_LOGIN, 24);
        *login << _characterGuid << float(1000.0f);
        _session->QueuePacket(login);
    }

    void BotSession::LinkInstanceAndContinueLogin()
    {
        if (!_session || _instanceLinked || !_instanceSocket)
            return;

        // The module stands in for World::ProcessLinkInstanceSocket
        // (World.cpp:353-354): with no real instance handshake, nothing else
        // links the instance socket or continues the login. PlayerDisconnected()
        // needs both sockets open, and HandleContinuePlayerLogin builds the
        // LoginQueryHolder that the world thread then resolves into the Player.
        _instanceSocket->SetWorldSession(_session);
        _session->AddInstanceConnection(_instanceSocket);
        _session->HandleContinuePlayerLogin();
        _instanceLinked = true;
    }

    void BotSession::DrainSockets()
    {
        // WorldSocket::Update drains _bufferQueue (an MPSCQueue: the world and
        // map threads enqueue via SendPacket, this single module thread dequeues)
        // and writes the bytes into the loopback stub, which nobody reads. Without
        // this every outbound packet would leak a heap EncryptablePacket forever.
        if (_realmSocket && _realmSocket->IsOpen())
            _realmSocket->Update();

        if (_instanceSocket && _instanceSocket->IsOpen())
            _instanceSocket->Update();
    }

    void BotSession::KeepAlive()
    {
        if (!_session)
            return;

        // A bot has no inbound traffic to re-arm the idle timer, so without this
        // WorldSession::Update would eventually see IsConnectionIdle() and close
        // the realm socket. m_timeOutTime is std::atomic<int32> and
        // ResetTimeOutTime only reads config, so this is safe off the world
        // thread — the core itself calls it off-thread.
        _session->ResetTimeOutTime();
    }

    bool BotSession::IsRealmSocketOpen() const
    {
        return _realmSocket && _realmSocket->IsOpen();
    }

    bool BotSession::HasPlayer() const
    {
        return _session && _session->GetPlayer() != nullptr;
    }

    bool BotSession::IsPlayerInWorld() const
    {
        if (!_session)
            return false;

        Player const* player = _session->GetPlayer();
        return player && player->IsInWorld();
    }

    bool BotSession::LoadedCharacterMatchesRequest() const
    {
        if (!_session)
            return false;

        Player const* player = _session->GetPlayer();
        return player && player->GetGUID() == _characterGuid;
    }
}
