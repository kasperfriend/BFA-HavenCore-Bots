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

#ifndef Bots_BotLifecyclePlan_h__
#define Bots_BotLifecyclePlan_h__

#include "BotTypes.h"
#include <string>

namespace Bots
{
    /// Stages of bringing one bot from "does not exist" to "standing in the
    /// world". The order mirrors modules/Bots/docs/00-DESIGN.md section 2.3.
    enum class BotLoginState : uint8
    {
        Idle = 0,
        Provisioning,
        WaitingForSessionAuth,
        CharacterListRequested,
        WaitingForCharacterList,
        LoginRequested,
        WaitingForPlayerLogin,
        WaitingForEnterWorld,
        InWorld,
        RetryPending,
        Failed
    };

    /// What the plan wants the executor to do next. Exactly one action is
    /// returned per Advance() call, and every action maps to one public core
    /// entry point or to one module-internal step.
    enum class BotPlanAction : uint8
    {
        None = 0,
        Disabled,
        ProvisionAccountAndCharacter,
        CreateSessionAndAddToWorld,
        RequestCharacterList,
        RequestPlayerLogin,
        ContinuePlayerLogin,
        Wait,
        LogInWorld,
        RetryLater,
        GiveUp
    };

    /// Everything the executor observes about one bot, flattened into plain
    /// data so the transition table can be tested without a server.
    struct BotPlanInput
    {
        bool LoginFeatureEnabled = false;
        BotLoginState State = BotLoginState::Idle;

        /// True from the moment the WorldSession object exists.
        bool SessionCreated = false;
        /// False once the session's liveness guard reports destruction. The
        /// world thread owns the session lifetime (World.cpp:3120-3128), so this
        /// can flip between any two ticks.
        bool SessionAlive = true;
        bool SessionAuthed = false;
        bool CharacterListRequested = false;
        bool CharacterListReady = false;
        bool CharacterGuidKnown = false;
        bool PlayerLoginRequested = false;
        bool PlayerSetOnSession = false;
        bool PlayerInWorld = false;

        /// True when the character the core actually loaded is the one the
        /// module asked for. A mismatch is a hard failure, not a retry.
        bool LoadedCharacterMatchesRequest = true;

        uint32 ElapsedMs = 0;
        uint32 Attempt = 0;
    };

    struct BotPlanStep
    {
        BotLoginState State = BotLoginState::Idle;
        BotPlanAction Action = BotPlanAction::None;
    };

    /// The login plan: a pure transition table over BotPlanInput.
    ///
    /// Kept free of core includes on purpose. modules/Bots/tests builds this
    /// file with nothing but a C++17 compiler and asserts the whole table,
    /// including every failure edge, which is the part that would otherwise
    /// only show up as a hung bot on a live server.
    class BotLifecyclePlan
    {
    public:
        /// Per-state budget before a state is abandoned. One value rather than a
        /// table because every waiting state here is bounded by the same thing:
        /// the core's asynchronous query callbacks, which either run on the next
        /// world tick or never.
        static constexpr uint32 DefaultStateTimeoutMs = 30000;

        /// How long to stay in RetryPending before re-entering Provisioning.
        static constexpr uint32 DefaultRetryDelayMs = 5000;

        static BotPlanStep Advance(BotPlanInput const& input,
            uint32 stateTimeoutMs = DefaultStateTimeoutMs,
            uint32 retryDelayMs = DefaultRetryDelayMs,
            uint32 maxAttempts = 3);

        static char const* ToString(BotLoginState state);
        static char const* ToString(BotPlanAction action);

        /// True for states that will not progress without another Advance().
        static bool IsTerminal(BotLoginState state);
    };
}

#endif // Bots_BotLifecyclePlan_h__
