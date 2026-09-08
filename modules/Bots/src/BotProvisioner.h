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

#ifndef Bots_BotProvisioner_h__
#define Bots_BotProvisioner_h__

#include "BotTypes.h"
#include "ObjectGuid.h"
#include <string>

namespace Bots
{
    struct BotConfigValues;

    /// What provisioning produced for one bot. `Success` is false when the bot
    /// could not be given an account or a character, in which case the login
    /// plan retries rather than proceeding.
    struct BotProvisionResult
    {
        bool Success = false;
        uint32 AccountId = 0;
        ObjectGuid CharacterGuid;
        std::string CharacterName;
        std::string Failure;
    };

    /// Creates the auth and character rows one bot needs, idempotently: running
    /// it twice for the same index yields the same account and character rather
    /// than a duplicate. All methods run on the module worker thread, before the
    /// bot's login session is added to the world, so they cannot race the world
    /// thread — the same position the core's own CLI character tools occupy.
    class BotProvisioner
    {
    public:
        virtual ~BotProvisioner() = default;

        /// Ensures the shared `battlenet_accounts` parent row exists, so the
        /// `LEFT JOIN` in `LOGIN_SEL_ACCOUNT_INFO_BY_NAME` resolves. Safe to call
        /// on every provision; it does nothing once the row is present.
        virtual bool EnsureParentAccount(std::string const& email, uint32& parentAccountId, std::string& failure) = 0;

        /// Ensures one `auth.account` row exists for the bot and returns its id.
        virtual bool EnsureAccount(std::string const& accountName, uint32 parentAccountId,
            BotConfigValues const& config, uint32& accountId, std::string& failure) = 0;

        /// Ensures the bot's character exists and returns its GUID. Creates it
        /// server-side (Player::Create + SaveToDB) the first time.
        virtual bool EnsureCharacter(uint32 accountId, std::string const& characterName,
            BotConfigValues const& config, ObjectGuid& characterGuid, std::string& failure) = 0;
    };
}

#endif // Bots_BotProvisioner_h__
