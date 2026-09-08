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

#include "BotManager.h"
#include "BotSession.h"
#include "BotIdentity.h"
#include "BotProvisioner.h"
#include "DatabaseBotProvisioner.h"
#include "Define.h"
#include "Config.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldSocket.h"
#include "WorldPacket.h"
#include "Player.h"
#include "Opcodes.h"
#include "Log.h"
#include <chrono>
#include <cstring>
#include <utility>

namespace Bots
{
    namespace
    {
        /// Adapts the core's Config singleton to the core-free BotConfig::Getter
        /// so the gate table stays unit-testable.
        class WorldConfigGetter : public BotConfig::Getter
        {
        public:
            bool GetBool(std::string const& key, bool def) const override { return sConfigMgr->GetBoolDefault(key, def); }
            int32 GetInt(std::string const& key, int32 def) const override { return sConfigMgr->GetIntDefault(key, def); }
            uint32 GetUInt(std::string const& key, uint32 def) const override
            {
                int32 value = sConfigMgr->GetIntDefault(key, static_cast<int32>(def));
                return value > 0 ? static_cast<uint32>(value) : def;
            }
            std::string GetString(std::string const& key, std::string const& def) const override { return sConfigMgr->GetStringDefault(key, def); }
        };

        // Read straight from the raw payload so the const WorldPacket handed to
        // OnPacketSend is not advanced. SMSG_TIME_SYNC_REQUEST is exactly one
        // uint32 SequenceIndex (MiscPackets.cpp:128).
        uint32 ReadSequenceIndex(WorldPacket const& packet)
        {
            if (packet.size() < sizeof(uint32))
                return 0;

            uint32 index = 0;
            memcpy(&index, packet.contents(), sizeof(index));
            return index;
        }
    }

    BotManager* BotManager::instance()
    {
        static BotManager mgr;
        return &mgr;
    }

    BotManager::BotManager() = default;

    BotManager::~BotManager()
    {
        StopWorker();
    }

    void BotManager::Initialize()
    {
        WorldConfigGetter getter;
        _config = BotConfig::Load(getter);

        if (!_config.Enable)
        {
            TC_LOG_INFO("bots", "Bots module disabled (Bots.Enable = 0). Nothing will be provisioned or logged in.");
            return;
        }

        if (!BotIdentity::IsValidPrefix(_config.NamePrefix))
        {
            // A bad prefix is a config error, not something to discover on the
            // six-hundredth bot when a name renders "XXX". Refuse to start.
            TC_LOG_ERROR("bots", "Bots.NamePrefix '%s' is not a valid bot prefix (alphabetic, capitalised, no repeated "
                "letter, 2..%u characters). The module will stay disabled.",
                _config.NamePrefix.c_str(), BotIdentity::MaxCharacterNameLength - 1);
            _config.Enable = false;
            return;
        }

        _provisioner = std::make_unique<DatabaseBotProvisioner>();
        _socketHub = std::make_unique<BotSocketHub>();

        if (!_socketHub->Open())
        {
            TC_LOG_ERROR("bots", "Could not open the loopback socket hub; the module will stay disabled.");
            _config.Enable = false;
            return;
        }

        TC_LOG_INFO("bots", "Bots module enabled. Feature gates:\n%s", BotConfig::FormatGateReport(_config).c_str());
    }

    void BotManager::StartWorker()
    {
        if (!_config.Enable || _worker)
            return;

        _running = true;
        _worker = std::make_unique<std::thread>(&BotManager::WorkerLoop, this);
        TC_LOG_INFO("bots", "Worker thread started (tick every %u ms).", _config.LoginIntervalMs);
    }

    void BotManager::StopWorker()
    {
        _running = false;
        if (_worker && _worker->joinable())
            _worker->join();

        _worker.reset();
    }

    void BotManager::RequestLogin(uint32 count)
    {
        if (!_config.Enable)
        {
            TC_LOG_WARN("bots", ".bot login ignored: the module is disabled.");
            return;
        }

        std::lock_guard<std::mutex> lock(_mutex);
        _pendingLogin += count;
    }

    void BotManager::RequestLogoutAll()
    {
        _pendingLogoutAll = true;
    }

    std::string BotManager::GetStatusReport() const
    {
        std::lock_guard<std::mutex> lock(_mutex);

        std::string report = "Bots: " + std::to_string(_bots.size()) + " managed, " +
            std::to_string(_pendingLogin) + " pending login\n";

        for (auto const& [accountId, bot] : _bots)
            report += "  " + bot->CharacterName + " (account " + bot->AccountName + ", id " +
                std::to_string(accountId) + "): " + BotLifecyclePlan::ToString(bot->State) +
                ", attempt " + std::to_string(bot->Attempt) + "\n";

        return report;
    }

    void BotManager::OnPacketSend(WorldSession* session, WorldPacket const& packet)
    {
        if (!session)
            return;

        uint32 const opcode = packet.GetOpcode();
        if (opcode != SMSG_ENUM_CHARACTERS_RESULT && opcode != SMSG_TIME_SYNC_REQUEST)
            return;

        BotSession* bot = FindSessionByAccount(session->GetAccountId());
        if (!bot)
            return;

        if (opcode == SMSG_ENUM_CHARACTERS_RESULT)
        {
            // HandleCharEnum fills the private _legitCharacters set and then sends
            // this packet (CharacterHandler.cpp:329,395), so seeing it is the
            // module's only public proof that the character list is ready.
            bot->OnCharacterListSent();
        }
        else
        {
            bot->OnTimeSyncRequest(ReadSequenceIndex(packet));
        }
    }

    BotSession* BotManager::FindSessionByAccount(uint32 accountId) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto itr = _registry.find(accountId);
        return itr != _registry.end() ? itr->second : nullptr;
    }

    void BotManager::WorkerLoop()
    {
        using namespace std::chrono;
        steady_clock::time_point last = steady_clock::now();

        while (_running)
        {
            std::this_thread::sleep_for(milliseconds(_config.LoginIntervalMs));

            steady_clock::time_point now = steady_clock::now();
            uint32 const diffMs = static_cast<uint32>(duration_cast<milliseconds>(now - last).count());
            last = now;

            if (_pendingLogoutAll.exchange(false))
            {
                std::lock_guard<std::mutex> lock(_mutex);
                for (auto& [accountId, bot] : _bots)
                    if (bot->Session && bot->Session->GetWorldSession())
                        bot->Session->GetWorldSession()->KickPlayer();

                _bots.clear();
                _registry.clear();
                _pendingLogin = 0;
                _nextIndex = 1;
                continue;
            }

            // Spawn any bots requested since the last tick.
            {
                std::lock_guard<std::mutex> lock(_mutex);
                while (_pendingLogin > 0 && (_config.MaxBots == 0 || _bots.size() < _config.MaxBots))
                {
                    --_pendingLogin;
                    auto bot = std::make_shared<BotRecord>();
                    bot->Index = _nextIndex++;
                    bot->AccountName = BotIdentity::AccountName(_config.NamePrefix, bot->Index);
                    bot->CharacterName = BotIdentity::CharacterName(_config.NamePrefix, bot->Index);
                    _bots[bot->Index] = bot;
                }
            }

            // Tick against a snapshot; each record has a single writer (this
            // thread), so no lock is held across the blocking work below.
            std::vector<std::shared_ptr<BotRecord>> snapshot;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                snapshot.reserve(_bots.size());
                for (auto& [index, bot] : _bots)
                    snapshot.push_back(bot);
            }

            for (auto& bot : snapshot)
                TickBot(*bot, diffMs);
        }
    }

    void BotManager::TickBot(BotRecord& bot, uint32 diffMs)
    {
        bot.StateElapsedMs += diffMs;

        BotPlanInput input;
        input.LoginFeatureEnabled = BotConfig::IsFeatureEnabled(_config, BotFeature::Login);
        input.State = bot.State;
        input.SessionCreated = bot.SessionCreated;
        input.SessionAlive = bot.Session != nullptr;
        input.SessionAuthed = bot.SessionAuthed;
        input.CharacterListRequested = bot.CharacterListRequested;
        input.CharacterListReady = bot.Session ? bot.Session->IsCharacterListReady() : false;
        input.CharacterGuidKnown = bot.CharacterGuidKnown;
        input.PlayerLoginRequested = bot.PlayerLoginRequested;
        input.PlayerSetOnSession = bot.Session ? bot.Session->HasPlayer() : false;
        input.PlayerInWorld = bot.Session ? bot.Session->IsPlayerInWorld() : false;
        input.LoadedCharacterMatchesRequest = bot.Session ? bot.Session->LoadedCharacterMatchesRequest() : true;
        input.ElapsedMs = bot.StateElapsedMs;
        input.Attempt = bot.Attempt;

        BotPlanStep const step = BotLifecyclePlan::Advance(input, _config.StateTimeoutMs, _config.RetryDelayMs, _config.MaxLoginAttempts);
        bot.State = step.State;
        ApplyAction(bot, step.Action);
    }

    void BotManager::ApplyAction(BotRecord& bot, BotPlanAction action)
    {
        // Mirrors the action contract the standalone test simulator asserts, but
        // performs the real work instead of only setting flags.
        switch (action)
        {
            case BotPlanAction::ProvisionAccountAndCharacter:
                DoProvision(bot);
                break;
            case BotPlanAction::CreateSessionAndAddToWorld:
                DoCreateSession(bot);
                break;
            case BotPlanAction::RequestCharacterList:
                if (bot.Session)
                    bot.Session->InjectCharacterEnum();
                bot.CharacterListRequested = true;
                bot.StateElapsedMs = 0;
                bot.State = BotLoginState::WaitingForCharacterList;
                break;
            case BotPlanAction::RequestPlayerLogin:
                if (bot.Session)
                    bot.Session->InjectPlayerLogin();
                bot.PlayerLoginRequested = true;
                bot.StateElapsedMs = 0;
                bot.State = BotLoginState::WaitingForPlayerLogin;
                break;
            case BotPlanAction::ContinuePlayerLogin:
                if (bot.Session)
                    bot.Session->LinkInstanceAndContinueLogin();
                bot.StateElapsedMs = 0;
                bot.State = BotLoginState::WaitingForEnterWorld;
                break;
            case BotPlanAction::LogInWorld:
                TC_LOG_INFO("bots", "Bot %s (account %s, character %s) is IN WORLD.",
                    bot.CharacterName.c_str(), bot.AccountName.c_str(), bot.CharacterGuid.ToString().c_str());
                break;
            case BotPlanAction::RetryLater:
                ++bot.Attempt;
                bot.Session.reset();
                bot.SessionCreated = false;
                bot.SessionAuthed = false;
                bot.CharacterListRequested = false;
                bot.PlayerLoginRequested = false;
                bot.StateElapsedMs = 0;
                break;
            case BotPlanAction::GiveUp:
                TC_LOG_ERROR("bots", "Bot %s gave up after %u attempts (last state %s).",
                    bot.CharacterName.c_str(), bot.Attempt, BotLifecyclePlan::ToString(bot.State));
                break;
            case BotPlanAction::None:
            case BotPlanAction::Disabled:
            case BotPlanAction::Wait:
            default:
                break;
        }
    }

    void BotManager::DoProvision(BotRecord& bot)
    {
        std::string failure;
        uint32 parentAccountId = 0;
        std::string const parentEmail = BotIdentity::ParentBattlenetEmail(_config.NamePrefix);

        if (!_provisioner->EnsureParentAccount(parentEmail, parentAccountId, failure) ||
            !_provisioner->EnsureAccount(bot.AccountName, parentAccountId, _config, bot.AccountId, failure) ||
            !_provisioner->EnsureCharacter(bot.AccountId, bot.CharacterName, _config, bot.CharacterGuid, failure))
        {
            TC_LOG_ERROR("bots", "Provisioning %s failed: %s", bot.CharacterName.c_str(), failure.c_str());
            ++bot.Attempt;
            bot.StateElapsedMs = 0;
            bot.State = BotLoginState::RetryPending;
            return;
        }

        bot.CharacterGuidKnown = true;
        bot.SessionCreated = false;
        bot.StateElapsedMs = 0;
        bot.State = BotLoginState::WaitingForSessionAuth;
    }

    void BotManager::DoCreateSession(BotRecord& bot)
    {
        auto [realmSocket, realmPeer] = _socketHub->MakeStub();
        auto [instanceSocket, instancePeer] = _socketHub->MakeStub();

        if (!realmSocket || !instanceSocket)
        {
            TC_LOG_ERROR("bots", "Could not build loopback sockets for %s.", bot.CharacterName.c_str());
            ++bot.Attempt;
            bot.StateElapsedMs = 0;
            bot.State = BotLoginState::RetryPending;
            return;
        }

        bot.Session = std::make_shared<BotSession>(bot.AccountId, bot.AccountName, bot.CharacterGuid, bot.CharacterName);
        bot.Session->SetSockets(realmSocket, std::move(realmPeer), instanceSocket, std::move(instancePeer));

        WorldSession* session = new WorldSession(bot.AccountId, std::string(bot.AccountName), 0u, realmSocket,
            SEC_PLAYER, _config.AccountExpansion, 0, _config.AccountOs, LOCALE_enUS, 0u, false,
            AT_AUTH_FLAG_NONE, std::string());

        realmSocket->SetWorldSession(session);
        bot.Session->SetWorldSession(session);

        // Load RBAC synchronously so HasPermission works the moment the world
        // thread runs the login handlers.
        session->LoadPermissions();

        sWorld->AddSession(session);

        {
            std::lock_guard<std::mutex> lock(_mutex);
            _registry[bot.AccountId] = bot.Session.get();
        }

        bot.SessionCreated = true;
        bot.SessionAuthed = true;
    }
}
