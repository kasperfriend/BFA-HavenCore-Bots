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

#include "BotScripts.h"

/// Invoked by the generated static ScriptLoader (AddScripts()), which the build
/// wires up because src/server/scripts/CMakeLists.txt appends "Bots" to
/// STATIC_SCRIPT_MODULES.
void AddBotsScripts()
{
    Bots::RegisterBotsScripts();
}
