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

#ifndef Bots_BotTypes_h__
#define Bots_BotTypes_h__

#include <cstdint>

namespace Bots
{
    // The core-free half of this module cannot include "Define.h", which is where
    // the global uint32/uint8 typedefs come from (src/common/Define.h:151).
    // These aliases live inside namespace Bots, so they read the same as core
    // code at every use site and cannot collide with the global typedefs when a
    // core-dependent translation unit includes both.
    using uint8 = std::uint8_t;
    using uint16 = std::uint16_t;
    using uint32 = std::uint32_t;
    using uint64 = std::uint64_t;
    using int32 = std::int32_t;
}

#endif // Bots_BotTypes_h__
