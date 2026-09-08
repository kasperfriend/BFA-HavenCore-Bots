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

#ifndef Bots_DatabaseBotProvisioner_h__
#define Bots_DatabaseBotProvisioner_h__

#include "BotProvisioner.h"

namespace Bots
{
    /// Provisions bots straight into the auth and character databases.
    ///
    /// The injected-session login path (docs/00-DESIGN.md §2.3) never runs
    /// `WorldSocket::HandleAuthSessionCallback`, so the `LENGTH(session_key_bnet)
    /// = 64` gate in `LOGIN_SEL_ACCOUNT_INFO_BY_NAME` (LoginDatabase.cpp:46) does
    /// not block a bot. The rows are written well-formed anyway — a 64-hex
    /// `session_key_bnet`, a `battlenet_accounts` parent for the `LEFT JOIN`, and
    /// `os = 'Wn64'` so `InitWarden` stays a no-op — so the same accounts also
    /// work if the wire-protocol backend on the roadmap is ever pointed at them.
    class DatabaseBotProvisioner : public BotProvisioner
    {
    public:
        bool EnsureParentAccount(std::string const& email, uint32& parentAccountId, std::string& failure) override;
        bool EnsureAccount(std::string const& accountName, uint32 parentAccountId,
            BotConfigValues const& config, uint32& accountId, std::string& failure) override;
        bool EnsureCharacter(uint32 accountId, std::string const& characterName,
            BotConfigValues const& config, ObjectGuid& characterGuid, std::string& failure) override;
    };
}

#endif // DatabaseBotProvisioner_h__
