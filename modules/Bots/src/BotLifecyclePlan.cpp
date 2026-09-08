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

#include "BotLifecyclePlan.h"

namespace Bots
{
    namespace
    {
        BotPlanStep MakeStep(BotLoginState state, BotPlanAction action)
        {
            BotPlanStep step;
            step.State = state;
            step.Action = action;
            return step;
        }

        BotPlanStep Wait(BotLoginState state)
        {
            return MakeStep(state, BotPlanAction::Wait);
        }

        BotPlanStep Retry(uint32 attempt, uint32 maxAttempts)
        {
            if (attempt + 1 >= maxAttempts)
                return MakeStep(BotLoginState::Failed, BotPlanAction::GiveUp);

            return MakeStep(BotLoginState::RetryPending, BotPlanAction::RetryLater);
        }

        bool TimedOut(BotPlanInput const& input, uint32 stateTimeoutMs)
        {
            return input.ElapsedMs >= stateTimeoutMs;
        }
    }

    BotPlanStep BotLifecyclePlan::Advance(BotPlanInput const& input, uint32 stateTimeoutMs, uint32 retryDelayMs, uint32 maxAttempts)
    {
        // The gate is checked first and unconditionally: with the login feature
        // off there is no state a bot may occupy other than Idle, so a bot that
        // was already in the world when the gate closed is left alone rather
        // than being torn down by a config change.
        if (!input.LoginFeatureEnabled)
        {
            if (input.State == BotLoginState::Idle)
                return MakeStep(BotLoginState::Idle, BotPlanAction::Disabled);

            return Wait(input.State);
        }

        switch (input.State)
        {
            case BotLoginState::Idle:
                return MakeStep(BotLoginState::Provisioning, BotPlanAction::None);

            case BotLoginState::Provisioning:
                return MakeStep(BotLoginState::Provisioning, BotPlanAction::ProvisionAccountAndCharacter);

            case BotLoginState::WaitingForSessionAuth:
            {
                // A session that existed and is now gone was destroyed by the
                // world thread; recreate it on a fresh attempt rather than
                // dereferencing anything.
                if (input.SessionCreated && !input.SessionAlive)
                    return Retry(input.Attempt, maxAttempts);

                if (!input.SessionCreated)
                    return MakeStep(BotLoginState::WaitingForSessionAuth, BotPlanAction::CreateSessionAndAddToWorld);

                if (!input.SessionAuthed)
                {
                    if (TimedOut(input, stateTimeoutMs))
                        return Retry(input.Attempt, maxAttempts);

                    return Wait(BotLoginState::WaitingForSessionAuth);
                }

                return MakeStep(BotLoginState::CharacterListRequested, BotPlanAction::None);
            }

            case BotLoginState::CharacterListRequested:
            {
                if (input.SessionCreated && !input.SessionAlive)
                    return Retry(input.Attempt, maxAttempts);

                if (!input.CharacterListRequested)
                    return MakeStep(BotLoginState::CharacterListRequested, BotPlanAction::RequestCharacterList);

                return MakeStep(BotLoginState::WaitingForCharacterList, BotPlanAction::None);
            }

            case BotLoginState::WaitingForCharacterList:
            {
                if (input.SessionCreated && !input.SessionAlive)
                    return Retry(input.Attempt, maxAttempts);

                // HandleCharEnum fills the session's private _legitCharacters set
                // from an asynchronous query (CharacterHandler.cpp:328,361).
                // Nothing outside the core can observe that set, so the executor
                // reports readiness and a timeout here is a retry, not a bug.
                if (!input.CharacterListReady)
                {
                    if (TimedOut(input, stateTimeoutMs))
                        return Retry(input.Attempt, maxAttempts);

                    return Wait(BotLoginState::WaitingForCharacterList);
                }

                if (!input.CharacterGuidKnown)
                    return Retry(input.Attempt, maxAttempts);

                return MakeStep(BotLoginState::LoginRequested, BotPlanAction::None);
            }

            case BotLoginState::LoginRequested:
            {
                if (input.SessionCreated && !input.SessionAlive)
                    return Retry(input.Attempt, maxAttempts);

                if (!input.PlayerLoginRequested)
                    return MakeStep(BotLoginState::LoginRequested, BotPlanAction::RequestPlayerLogin);

                return MakeStep(BotLoginState::WaitingForPlayerLogin, BotPlanAction::None);
            }

            case BotLoginState::WaitingForPlayerLogin:
            {
                if (input.SessionCreated && !input.SessionAlive)
                    return Retry(input.Attempt, maxAttempts);

                // The module stands in for World::ProcessLinkInstanceSocket here
                // (World.cpp:354): with no real instance socket to authenticate,
                // nothing else will ever call HandleContinuePlayerLogin.
                return MakeStep(BotLoginState::WaitingForPlayerLogin, BotPlanAction::ContinuePlayerLogin);
            }

            case BotLoginState::WaitingForEnterWorld:
            {
                if (input.SessionCreated && !input.SessionAlive)
                    return Retry(input.Attempt, maxAttempts);

                if (!input.PlayerSetOnSession || !input.PlayerInWorld)
                {
                    if (TimedOut(input, stateTimeoutMs))
                        return Retry(input.Attempt, maxAttempts);

                    return Wait(BotLoginState::WaitingForEnterWorld);
                }

                // Runtime self-check: the character the core loaded must be the
                // one that was requested. A mismatch means the roster and the
                // auth database disagree, and retrying would only repeat it.
                if (!input.LoadedCharacterMatchesRequest)
                    return MakeStep(BotLoginState::Failed, BotPlanAction::GiveUp);

                return MakeStep(BotLoginState::InWorld, BotPlanAction::LogInWorld);
            }

            case BotLoginState::InWorld:
                return Wait(BotLoginState::InWorld);

            case BotLoginState::RetryPending:
            {
                if (input.ElapsedMs < retryDelayMs)
                    return Wait(BotLoginState::RetryPending);

                return MakeStep(BotLoginState::Provisioning, BotPlanAction::None);
            }

            case BotLoginState::Failed:
                return MakeStep(BotLoginState::Failed, BotPlanAction::None);

            default:
                return MakeStep(BotLoginState::Failed, BotPlanAction::GiveUp);
        }
    }

    char const* BotLifecyclePlan::ToString(BotLoginState state)
    {
        switch (state)
        {
            case BotLoginState::Idle:                     return "Idle";
            case BotLoginState::Provisioning:             return "Provisioning";
            case BotLoginState::WaitingForSessionAuth:    return "WaitingForSessionAuth";
            case BotLoginState::CharacterListRequested:   return "CharacterListRequested";
            case BotLoginState::WaitingForCharacterList:  return "WaitingForCharacterList";
            case BotLoginState::LoginRequested:           return "LoginRequested";
            case BotLoginState::WaitingForPlayerLogin:    return "WaitingForPlayerLogin";
            case BotLoginState::WaitingForEnterWorld:     return "WaitingForEnterWorld";
            case BotLoginState::InWorld:                  return "InWorld";
            case BotLoginState::RetryPending:             return "RetryPending";
            case BotLoginState::Failed:                   return "Failed";
            default:                                      return "Unknown";
        }
    }

    char const* BotLifecyclePlan::ToString(BotPlanAction action)
    {
        switch (action)
        {
            case BotPlanAction::None:                          return "None";
            case BotPlanAction::Disabled:                      return "Disabled";
            case BotPlanAction::ProvisionAccountAndCharacter:  return "ProvisionAccountAndCharacter";
            case BotPlanAction::CreateSessionAndAddToWorld:    return "CreateSessionAndAddToWorld";
            case BotPlanAction::RequestCharacterList:          return "RequestCharacterList";
            case BotPlanAction::RequestPlayerLogin:            return "RequestPlayerLogin";
            case BotPlanAction::ContinuePlayerLogin:           return "ContinuePlayerLogin";
            case BotPlanAction::Wait:                          return "Wait";
            case BotPlanAction::LogInWorld:                    return "LogInWorld";
            case BotPlanAction::RetryLater:                    return "RetryLater";
            case BotPlanAction::GiveUp:                        return "GiveUp";
            default:                                           return "Unknown";
        }
    }

    bool BotLifecyclePlan::IsTerminal(BotLoginState state)
    {
        return state == BotLoginState::InWorld || state == BotLoginState::Failed;
    }
}
