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

#include "TestHarness.h"
#include <cstdint>
#include "../src/BotLifecyclePlan.h"
#include <string>
#include <vector>

using Bots::BotLifecyclePlan;
using Bots::BotLoginState;
using Bots::BotPlanAction;
using Bots::BotPlanInput;
using Bots::BotPlanStep;

namespace
{
    /// Minimal stand-in for the executor: applies the action the plan asked for
    /// to the observation set, the way BotManager would apply it to a session.
    struct PlanSimulator
    {
        BotPlanInput Input;
        std::uint32_t StateTimeoutMs = BotLifecyclePlan::DefaultStateTimeoutMs;
        std::uint32_t RetryDelayMs = BotLifecyclePlan::DefaultRetryDelayMs;
        std::uint32_t MaxAttempts = 3;
        bool ProvisionSucceeds = true;

        BotPlanStep Tick()
        {
            BotPlanStep const step = BotLifecyclePlan::Advance(Input, StateTimeoutMs, RetryDelayMs, MaxAttempts);
            Input.State = step.State;
            Apply(step.Action);
            return step;
        }

        void Apply(BotPlanAction action)
        {
            switch (action)
            {
                case BotPlanAction::ProvisionAccountAndCharacter:
                    if (ProvisionSucceeds)
                    {
                        Input.CharacterGuidKnown = true;
                        Input.SessionCreated = false;
                        Input.SessionAlive = true;
                        Input.ElapsedMs = 0;
                        Input.State = BotLoginState::WaitingForSessionAuth;
                    }
                    break;
                case BotPlanAction::CreateSessionAndAddToWorld:
                    Input.SessionCreated = true;
                    Input.SessionAlive = true;
                    break;
                case BotPlanAction::RequestCharacterList:
                    Input.CharacterListRequested = true;
                    Input.ElapsedMs = 0;
                    Input.State = BotLoginState::WaitingForCharacterList;
                    break;
                case BotPlanAction::RequestPlayerLogin:
                    Input.PlayerLoginRequested = true;
                    Input.ElapsedMs = 0;
                    Input.State = BotLoginState::WaitingForPlayerLogin;
                    break;
                case BotPlanAction::ContinuePlayerLogin:
                    Input.ElapsedMs = 0;
                    Input.State = BotLoginState::WaitingForEnterWorld;
                    break;
                case BotPlanAction::RetryLater:
                    Input.ElapsedMs = 0;
                    ++Input.Attempt;
                    Input.SessionCreated = false;
                    Input.SessionAlive = true;
                    Input.SessionAuthed = false;
                    Input.CharacterListRequested = false;
                    Input.CharacterListReady = false;
                    Input.PlayerLoginRequested = false;
                    Input.PlayerSetOnSession = false;
                    Input.PlayerInWorld = false;
                    break;
                case BotPlanAction::LogInWorld:
                case BotPlanAction::Wait:
                case BotPlanAction::GiveUp:
                case BotPlanAction::None:
                case BotPlanAction::Disabled:
                default:
                    break;
            }
        }
    };
}

BOTS_TEST(HappyPathReachesInWorldWithTheDocumentedActionOrder)
{
    PlanSimulator sim;
    sim.Input.LoginFeatureEnabled = true;
    sim.Input.SessionAuthed = true;
    sim.Input.CharacterListReady = true;
    sim.Input.PlayerSetOnSession = true;
    sim.Input.PlayerInWorld = true;

    std::vector<BotPlanAction> actions;
    for (int i = 0; i < 16 && sim.Input.State != BotLoginState::InWorld; ++i)
        actions.push_back(sim.Tick().Action);

    BOTS_CHECK_EQUAL(sim.Input.State, BotLoginState::InWorld);

    // The exact order matters: it is the order in which the module calls the
    // core's public login entry points (docs/00-DESIGN.md section 2.3).
    std::vector<BotPlanAction> const expected =
    {
        BotPlanAction::None,                          // Idle -> Provisioning
        BotPlanAction::ProvisionAccountAndCharacter,  // -> WaitingForSessionAuth
        BotPlanAction::CreateSessionAndAddToWorld,
        BotPlanAction::None,                          // authed -> CharacterListRequested
        BotPlanAction::RequestCharacterList,          // -> WaitingForCharacterList
        BotPlanAction::None,                          // list ready -> LoginRequested
        BotPlanAction::RequestPlayerLogin,            // -> WaitingForPlayerLogin
        BotPlanAction::ContinuePlayerLogin,           // -> WaitingForEnterWorld
        BotPlanAction::LogInWorld                     // -> InWorld
    };

    BOTS_CHECK_EQUAL(actions.size(), expected.size());
    for (size_t i = 0; i < expected.size() && i < actions.size(); ++i)
        BOTS_CHECK_EQUAL(static_cast<int>(actions[i]), static_cast<int>(expected[i]));
}

BOTS_TEST(InWorldIsStableAndDoesNotAskForMoreWork)
{
    PlanSimulator sim;
    sim.Input.LoginFeatureEnabled = true;
    sim.Input.State = BotLoginState::InWorld;
    sim.Input.SessionCreated = true;
    sim.Input.PlayerSetOnSession = true;
    sim.Input.PlayerInWorld = true;

    for (int i = 0; i < 5; ++i)
    {
        BotPlanStep const step = sim.Tick();
        BOTS_CHECK_EQUAL(step.State, BotLoginState::InWorld);
        BOTS_CHECK_EQUAL(static_cast<int>(step.Action), static_cast<int>(BotPlanAction::Wait));
    }

    BOTS_CHECK(BotLifecyclePlan::IsTerminal(BotLoginState::InWorld));
}

BOTS_TEST(LoginGateOffMeansNoBotIsEverCreated)
{
    BotPlanInput input;
    input.LoginFeatureEnabled = false;
    input.State = BotLoginState::Idle;

    BotPlanStep const step = BotLifecyclePlan::Advance(input);
    BOTS_CHECK_EQUAL(step.State, BotLoginState::Idle);
    BOTS_CHECK_EQUAL(static_cast<int>(step.Action), static_cast<int>(BotPlanAction::Disabled));
}

BOTS_TEST(LoginGateOffLeavesAnAlreadyLoggedInBotAlone)
{
    // Closing the gate must not tear down bots that are already standing in the
    // world; that would make a config reload destructive.
    BotPlanInput input;
    input.LoginFeatureEnabled = false;
    input.State = BotLoginState::InWorld;

    BotPlanStep const step = BotLifecyclePlan::Advance(input);
    BOTS_CHECK_EQUAL(step.State, BotLoginState::InWorld);
    BOTS_CHECK_EQUAL(static_cast<int>(step.Action), static_cast<int>(BotPlanAction::Wait));
}

BOTS_TEST(SessionDeathTriggersRetryNotACrash)
{
    BotPlanInput input;
    input.LoginFeatureEnabled = true;
    input.State = BotLoginState::WaitingForSessionAuth;
    input.SessionCreated = true;
    input.SessionAlive = false;
    input.Attempt = 0;

    BotPlanStep const step = BotLifecyclePlan::Advance(input);
    BOTS_CHECK_EQUAL(step.State, BotLoginState::RetryPending);
    BOTS_CHECK_EQUAL(static_cast<int>(step.Action), static_cast<int>(BotPlanAction::RetryLater));
}

BOTS_TEST(SessionDeathIsRetriedFromEveryWaitingState)
{
    BotLoginState const waitingStates[] =
    {
        BotLoginState::WaitingForSessionAuth,
        BotLoginState::CharacterListRequested,
        BotLoginState::WaitingForCharacterList,
        BotLoginState::LoginRequested,
        BotLoginState::WaitingForPlayerLogin,
        BotLoginState::WaitingForEnterWorld
    };

    for (BotLoginState state : waitingStates)
    {
        BotPlanInput input;
        input.LoginFeatureEnabled = true;
        input.State = state;
        input.SessionCreated = true;
        input.SessionAlive = false;

        BotPlanStep const step = BotLifecyclePlan::Advance(input);
        if (step.State != BotLoginState::RetryPending)
        {
            BOTS_CHECK(false);
            std::printf("      state %s did not retry on session death\n", BotLifecyclePlan::ToString(state));
        }
    }
}

BOTS_TEST(SessionDeathBeforeCreationIsNotATerminableEvent)
{
    // SessionAlive defaults to true, but an executor that reports "not alive,
    // never created" must be read as "nothing to clean up", not as a dead
    // session, or the very first attempt would fail.
    BotPlanInput input;
    input.LoginFeatureEnabled = true;
    input.State = BotLoginState::WaitingForSessionAuth;
    input.SessionCreated = false;
    input.SessionAlive = false;

    BotPlanStep const step = BotLifecyclePlan::Advance(input);
    BOTS_CHECK_EQUAL(step.State, BotLoginState::WaitingForSessionAuth);
    BOTS_CHECK_EQUAL(static_cast<int>(step.Action), static_cast<int>(BotPlanAction::CreateSessionAndAddToWorld));
}

BOTS_TEST(RetryDelayGatesTheReturnToProvisioning)
{
    BotPlanInput input;
    input.LoginFeatureEnabled = true;
    input.State = BotLoginState::RetryPending;
    input.ElapsedMs = 4999;

    BotPlanStep const early = BotLifecyclePlan::Advance(input, 30000, 5000);
    BOTS_CHECK_EQUAL(early.State, BotLoginState::RetryPending);
    BOTS_CHECK_EQUAL(static_cast<int>(early.Action), static_cast<int>(BotPlanAction::Wait));

    input.ElapsedMs = 5000;
    BotPlanStep const due = BotLifecyclePlan::Advance(input, 30000, 5000);
    BOTS_CHECK_EQUAL(due.State, BotLoginState::Provisioning);
    BOTS_CHECK_EQUAL(static_cast<int>(due.Action), static_cast<int>(BotPlanAction::None));
}

BOTS_TEST(StateTimeoutEscalatesToRetryThenToGiveUp)
{
    BotPlanInput input;
    input.LoginFeatureEnabled = true;
    input.State = BotLoginState::WaitingForSessionAuth;
    input.SessionCreated = true;
    input.SessionAlive = true;
    input.SessionAuthed = false;
    input.ElapsedMs = 30000;

    input.Attempt = 0;
    BOTS_CHECK_EQUAL(BotLifecyclePlan::Advance(input, 30000, 5000, 3).State, BotLoginState::RetryPending);

    input.Attempt = 1;
    BOTS_CHECK_EQUAL(BotLifecyclePlan::Advance(input, 30000, 5000, 3).State, BotLoginState::RetryPending);

    input.Attempt = 2;
    BotPlanStep const last = BotLifecyclePlan::Advance(input, 30000, 5000, 3);
    BOTS_CHECK_EQUAL(last.State, BotLoginState::Failed);
    BOTS_CHECK_EQUAL(static_cast<int>(last.Action), static_cast<int>(BotPlanAction::GiveUp));
}

BOTS_TEST(JustBeforeTimeoutStillWaits)
{
    BotPlanInput input;
    input.LoginFeatureEnabled = true;
    input.State = BotLoginState::WaitingForSessionAuth;
    input.SessionCreated = true;
    input.SessionAuthed = false;
    input.ElapsedMs = 29999;

    BotPlanStep const step = BotLifecyclePlan::Advance(input, 30000, 5000, 3);
    BOTS_CHECK_EQUAL(step.State, BotLoginState::WaitingForSessionAuth);
    BOTS_CHECK_EQUAL(static_cast<int>(step.Action), static_cast<int>(BotPlanAction::Wait));
}

BOTS_TEST(UnknownCharacterGuidAfterAReadyListIsARetry)
{
    BotPlanInput input;
    input.LoginFeatureEnabled = true;
    input.State = BotLoginState::WaitingForCharacterList;
    input.SessionCreated = true;
    input.CharacterListRequested = true;
    input.CharacterListReady = true;
    input.CharacterGuidKnown = false;

    BotPlanStep const step = BotLifecyclePlan::Advance(input);
    BOTS_CHECK_EQUAL(step.State, BotLoginState::RetryPending);
    BOTS_CHECK_EQUAL(static_cast<int>(step.Action), static_cast<int>(BotPlanAction::RetryLater));
}

BOTS_TEST(CharacterListTimeoutIsARetryNotAStall)
{
    BotPlanInput input;
    input.LoginFeatureEnabled = true;
    input.State = BotLoginState::WaitingForCharacterList;
    input.SessionCreated = true;
    input.CharacterListRequested = true;
    input.CharacterListReady = false;
    input.ElapsedMs = 30000;

    BOTS_CHECK_EQUAL(BotLifecyclePlan::Advance(input, 30000, 5000, 3).State, BotLoginState::RetryPending);
}

BOTS_TEST(LoadedCharacterMismatchIsPermanentFailure)
{
    // Runtime self-check from docs/00-DESIGN.md section 9: if the core loaded a
    // different character than the one requested, retrying cannot help.
    BotPlanInput input;
    input.LoginFeatureEnabled = true;
    input.State = BotLoginState::WaitingForEnterWorld;
    input.SessionCreated = true;
    input.PlayerSetOnSession = true;
    input.PlayerInWorld = true;
    input.LoadedCharacterMatchesRequest = false;
    input.Attempt = 0;

    BotPlanStep const step = BotLifecyclePlan::Advance(input);
    BOTS_CHECK_EQUAL(step.State, BotLoginState::Failed);
    BOTS_CHECK_EQUAL(static_cast<int>(step.Action), static_cast<int>(BotPlanAction::GiveUp));
}

BOTS_TEST(FailedIsAbsorbing)
{
    BotPlanInput input;
    input.LoginFeatureEnabled = true;
    input.State = BotLoginState::Failed;

    for (int i = 0; i < 3; ++i)
    {
        BotPlanStep const step = BotLifecyclePlan::Advance(input);
        BOTS_CHECK_EQUAL(step.State, BotLoginState::Failed);
        BOTS_CHECK_EQUAL(static_cast<int>(step.Action), static_cast<int>(BotPlanAction::None));
    }

    BOTS_CHECK(BotLifecyclePlan::IsTerminal(BotLoginState::Failed));
    BOTS_CHECK(!BotLifecyclePlan::IsTerminal(BotLoginState::RetryPending));
    BOTS_CHECK(!BotLifecyclePlan::IsTerminal(BotLoginState::Provisioning));
}

BOTS_TEST(ProvisioningFailureDoesNotAdvance)
{
    PlanSimulator sim;
    sim.Input.LoginFeatureEnabled = true;
    sim.ProvisionSucceeds = false;

    // Idle only decides to start provisioning; the work itself is asked for on
    // the next tick, once the state has actually changed.
    BotPlanStep const idle = sim.Tick();
    BOTS_CHECK_EQUAL(idle.State, BotLoginState::Provisioning);
    BOTS_CHECK_EQUAL(static_cast<int>(idle.Action), static_cast<int>(BotPlanAction::None));

    BotPlanStep const first = sim.Tick();
    BOTS_CHECK_EQUAL(first.State, BotLoginState::Provisioning);
    BOTS_CHECK_EQUAL(static_cast<int>(first.Action), static_cast<int>(BotPlanAction::ProvisionAccountAndCharacter));

    // The executor reported no progress, so the plan must keep asking rather
    // than silently moving on to create a session for a character that does not
    // exist.
    BotPlanStep const second = sim.Tick();
    BOTS_CHECK_EQUAL(second.State, BotLoginState::Provisioning);
    BOTS_CHECK_EQUAL(static_cast<int>(second.Action), static_cast<int>(BotPlanAction::ProvisionAccountAndCharacter));

    BotPlanStep const third = sim.Tick();
    BOTS_CHECK_EQUAL(third.State, BotLoginState::Provisioning);
    BOTS_CHECK(!BotLifecyclePlan::IsTerminal(BotLoginState::Provisioning));
}

BOTS_TEST(IdleOnlyEverDecidesToProvision)
{
    BotPlanInput input;
    input.LoginFeatureEnabled = true;
    input.State = BotLoginState::Idle;

    BotPlanStep const step = BotLifecyclePlan::Advance(input);
    BOTS_CHECK_EQUAL(step.State, BotLoginState::Provisioning);
    BOTS_CHECK_EQUAL(static_cast<int>(step.Action), static_cast<int>(BotPlanAction::None));
}

BOTS_TEST(EveryStateAndActionHasALogName)
{
    for (int s = 0; s <= static_cast<int>(BotLoginState::Failed); ++s)
    {
        char const* name = BotLifecyclePlan::ToString(static_cast<BotLoginState>(s));
        BOTS_CHECK(name != nullptr);
        BOTS_CHECK(std::string(name) != std::string("Unknown"));
    }

    for (int a = 0; a <= static_cast<int>(BotPlanAction::GiveUp); ++a)
    {
        char const* name = BotLifecyclePlan::ToString(static_cast<BotPlanAction>(a));
        BOTS_CHECK(name != nullptr);
        BOTS_CHECK(std::string(name) != std::string("Unknown"));
    }
}

BOTS_TEST(RetryCountIsRespectedAcrossAFullSimulation)
{
    PlanSimulator sim;
    sim.Input.LoginFeatureEnabled = true;
    sim.MaxAttempts = 3;
    sim.ProvisionSucceeds = true;

    // Auth never completes: the bot must exhaust its attempts and stop, rather
    // than spinning forever.
    sim.Input.SessionAuthed = false;

    int giveUps = 0;
    for (int i = 0; i < 400; ++i)
    {
        BotPlanStep const step = sim.Tick();
        if (step.Action == BotPlanAction::GiveUp)
            ++giveUps;

        // The simulator never advances the elapsed clock on its own, so drive the
        // timeout explicitly the way the worker thread would.
        if (step.Action == BotPlanAction::Wait)
            sim.Input.ElapsedMs += 10000;
        if (step.Action == BotPlanAction::RetryLater)
            sim.Input.ElapsedMs += 5000;
    }

    BOTS_CHECK_EQUAL(giveUps, 1);
    BOTS_CHECK_EQUAL(sim.Input.State, BotLoginState::Failed);
    BOTS_CHECK_EQUAL(sim.Input.Attempt, static_cast<std::uint32_t>(2));
}
