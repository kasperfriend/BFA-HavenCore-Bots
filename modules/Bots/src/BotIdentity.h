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

#ifndef Bots_BotIdentity_h__
#define Bots_BotIdentity_h__

#include "BotTypes.h"
#include <string>

namespace Bots
{
    enum class NameRuleViolation : uint8
    {
        None = 0,
        Empty,
        TooLong,
        TooShort,
        NotAlphabetic,
        ThreeConsecutiveIdentical,
        PrefixNotAlphabetic
    };

    /// Naming for bot accounts and bot characters.
    ///
    /// This header is deliberately free of core includes so modules/Bots/tests
    /// can build it with nothing but a C++17 compiler.
    class BotIdentity
    {
    public:
        /// Longest character name the client accepts. Mirrors MAX_PLAYER_NAME
        /// (src/server/game/Globals/ObjectMgr.h:886); duplicated rather than
        /// included to keep this translation unit core-free.
        static constexpr uint32 MaxCharacterNameLength = 12;

        /// Shortest character name the core accepts by default. Mirrors the
        /// MinPlayerName config default (src/server/game/World/World.cpp:795).
        static constexpr uint32 DefaultMinCharacterNameLength = 2;

        /// Renders an index as capital letters with no two adjacent letters
        /// equal, so no generated name can ever trip the core's
        /// three-consecutive-identical rule.
        ///
        /// Digits are not usable at all: CheckPlayerName calls isValidString
        /// with numericOrSpace = false
        /// (src/server/game/Globals/ObjectMgr.cpp:8413) and
        /// isBasicLatinCharacter accepts A-Z/a-z only
        /// (src/common/Utilities/Util.h:126-133). A plain base-26 rendering is
        /// also not enough — it produces "AAA" for index 703 and "TT" for 540.
        ///
        /// Encoding is a bijection onto run-free words, radix 25 at every
        /// position: a letter may be anything except the one immediately to its
        /// left. That is what makes index 52 render as "CB" rather than "BB" and
        /// index 703 as "ADC" rather than "AAA". Index 0 renders as "A" because
        /// the bijection's domain starts at 1. Capacity for a k-letter suffix is
        /// 25^k, so the 9 letters a 3-letter prefix leaves are worth 25^9 names.
        static std::string RunFreeBase26(uint32 index);

        /// Inverse of RunFreeBase26. Returns 0 for an empty or malformed word.
        static uint32 RunFreeBase26ToIndex(std::string const& word);

        /// Character name for a bot index, e.g. prefix "Bot" + index 3 -> "BotD".
        /// The prefix is required to be run-free itself (see IsValidPrefix), so
        /// concatenating a run-free suffix cannot create a run across the join.
        static std::string CharacterName(std::string const& prefix, uint32 index);

        /// Account name / realm join ticket for a bot index. Accounts are not
        /// subject to CheckPlayerName, so digits are fine and read better in
        /// the auth database.
        static std::string AccountName(std::string const& prefix, uint32 index);

        /// Battlenet account e-mail that parents every bot game account. The
        /// core left-joins battlenet_accounts for locale, lock state and e-mail
        /// in LOGIN_SEL_ACCOUNT_INFO_BY_NAME
        /// (src/server/database/Database/Implementation/LoginDatabase.cpp:42-46),
        /// so exactly one such row is needed.
        static std::string ParentBattlenetEmail(std::string const& prefix);

        /// The single rule the core enforces that a generated name can actually
        /// trip: no letter repeated three times in a row
        /// (src/server/game/Globals/ObjectMgr.cpp:8417-8419).
        static bool HasThreeConsecutiveIdenticalLetters(std::string const& name);

        /// Full check of a candidate character name against the rules the core
        /// applies in CheckPlayerName, minus the two that need live data
        /// (profanity/name filters and the realm language mask).
        static NameRuleViolation ValidateCharacterName(std::string const& name,
            uint32 minLength = DefaultMinCharacterNameLength);

        /// A prefix is usable when it is non-empty, alphabetic, first letter
        /// capitalised (the core capitalises it anyway at
        /// src/server/game/Handlers/CharacterHandler.cpp:596-598) and leaves
        /// room for at least one index letter.
        static bool IsValidPrefix(std::string const& prefix);

        /// Capitalises the first letter and lowercases the rest, matching what
        /// the core does to a client-supplied name.
        static std::string NormalizePrefix(std::string const& prefix);
    };
}

#endif // Bots_BotIdentity_h__
