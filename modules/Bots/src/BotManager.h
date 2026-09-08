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

#ifndef Bots_BotManager_h__
#define Bots_BotManager_h__

#include "BotTypes.h"
#include "BotConfig.h"
#include "BotLifecyclePlan.h"
#include "BotSocket.h"
#include "ObjectGuid.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class WorldSession;
class WorldPacket;
class Player;

namespace Bots
{
    class BotSession;
    class BotProvisioner;

    /// One bot's mutable lifecycle state, driven by BotLifecyclePlan. The flags
    /// mirror BotPlanInput; the manager fills them from what it can observe and
    /// applies the plan's action contract (the same contract the standalone test
    /// simulator asserts).
    struct BotRecord
    {
        uint32 Index = 0;
        std::string AccountName;
        std::string CharacterName;
        uint32 AccountId = 0;
        ObjectGuid CharacterGuid;

        BotLoginState State = BotLoginState::Idle;
        uint32 Attempt = 0;
        uint32 StateElapsedMs = 0;

        bool CharacterGuidKnown = false;
        bool SessionCreated = false;
        bool SessionAuthed = false;
        bool CharacterListRequested = false;
        bool PlayerLoginRequested = false;

        // Accumulates tick time so KeepAlive runs at Bots.KeepAliveIntervalMs
        // rather than every tick (a no-op reset is cheap, but there is no reason
        // to do it a thousand times a second either).
        uint32 KeepAliveAccumMs = 0;

        std::shared_ptr<BotSession> Session;
    };

    /// Owns the roster and the worker thread, provisions bots, builds their
    /// sessions and drives the login plan. A process-wide singleton created by
    /// the module's script registration.
    ///
    /// Threading: the worker thread owns `_bots` and every mutation of a
    /// BotRecord. The world thread reaches the manager only through
    /// `OnPacketSend` (via BotScripts), which looks a bot up by account id under
    /// `_registryMutex` and touches only the thread-safe BotSession operations.
    class BotManager
    {
    public:
        static BotManager* instance();

        /// Reads config, opens the loopback socket hub and prints the gate table.
        /// Called once from the module's OnStartup. Safe to call when disabled:
        /// it then does nothing and StartWorker is a no-op.
        void Initialize();

        /// Starts the worker thread. No-op if the module is disabled.
        void StartWorker();
        /// Stops the worker thread. Called from the module's OnShutdown.
        void StopWorker();

        bool IsEnabled() const { return _config.Enable; }
        BotConfigValues const& GetConfig() const { return _config; }

        /// Chat-command entry points. Each enqueues work for the worker thread
        /// rather than provisioning inline, so the command returns immediately.
        void RequestLogin(uint32 count);
        void RequestLogoutAll();
        std::string GetStatusReport() const;

        /// World-thread hook, from ScriptMgr::OnPacketSend. Routes the two
        /// server->client packets a standing bot must react to: the character
        /// list (which marks char-enum complete) and the time-sync request.
        void OnPacketSend(WorldSession* session, WorldPacket const& packet);

    private:
        BotManager();
        ~BotManager();
        BotManager(BotManager const&) = delete;
        BotManager& operator=(BotManager const&) = delete;

        void WorkerLoop();
        void TickBot(BotRecord& bot, uint32 diffMs);
        void ApplyAction(BotRecord& bot, BotPlanAction action);

        /// Per-tick upkeep for a bot that already has a session: drain both
        /// sockets' outbound queues so the core's SendPacket calls do not leak,
        /// and re-arm the idle timer so WorldSession::Update never sees the
        /// session as idle and closes the realm socket.
        void Maintenance(BotRecord& bot, uint32 diffMs);

        // Action implementations, all on the worker thread.
        void DoProvision(BotRecord& bot);
        void DoCreateSession(BotRecord& bot);

        // Registry lookup for the world-thread hook. Returns null if the account
        // is not a bot or its session is not yet attached.
        BotSession* FindSessionByAccount(uint32 accountId) const;
        void RebuildRegistry();

        BotConfigValues _config;
        std::unique_ptr<BotSocketHub> _socketHub;
        std::unique_ptr<BotProvisioner> _provisioner;

        // Worker-thread only, except where noted.
        // One mutex guards the roster map, the account->session registry, the
        // pending-login count and the next index. The command thread
        // (RequestLogin/RequestLogoutAll/GetStatusReport), the worker thread and
        // the world thread (OnPacketSend -> FindSessionByAccount) all take it,
        // but only ever for short bookkeeping — never across a blocking DB call.
        // The worker ticks against a snapshot of shared_ptr<BotRecord>, and each
        // record has a single writer (the worker), so ticking needs no lock.
        mutable std::mutex _mutex;
        std::unordered_map<uint32, std::shared_ptr<BotRecord>> _bots;
        std::unordered_map<uint32, BotSession*> _registry;
        uint32 _pendingLogin = 0;
        std::atomic<bool> _pendingLogoutAll{ false };
        uint32 _nextIndex = 1;

        std::unique_ptr<std::thread> _worker;
        std::atomic<bool> _running{ false };
    };
}

#define sBotManager Bots::BotManager::instance()

#endif // Bots_BotManager_h__
