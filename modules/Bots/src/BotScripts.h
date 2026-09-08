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

#ifndef Bots_BotScripts_h__
#define Bots_BotScripts_h__

#include "BotTypes.h"

namespace Bots
{
    /// RBAC permission that gates the `.bot` command. The core's own ids top out
    /// at 2010, so the module reserves 2100 and ships the matching
    /// `rbac_permissions` / `rbac_linked_permissions` rows in sql/auth/. Keeping
    /// it a named constant means the C++ and the SQL cannot drift apart silently.
    constexpr uint32 RBAC_PERM_BOTS_COMMAND = 2100;
}

namespace Bots
{
    /// Constructs and registers the module's script objects. Declared here and
    /// defined in BotScripts.cpp (where the script classes live) so the loader
    /// translation unit does not need their definitions.
    void RegisterBotsScripts();
}

/// Called by the generated static ScriptLoader.
void AddBotsScripts();

#endif // Bots_BotScripts_h__
