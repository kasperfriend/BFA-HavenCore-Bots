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
#include "../src/BotIdentity.h"
#include <set>

using Bots::BotIdentity;
using Bots::NameRuleViolation;

BOTS_TEST(RunFreeBase26MatchesTheWorkedExamples)
{
    // Hand-computed from the run-free rule: words are numbered shortest first,
    // the leading letter spans the full alphabet and every letter after it may
    // be anything except its left neighbour, so each of those positions has 25
    // choices. The 26 one-letter words come first, which is why index 26 is "Z"
    // and index 27 opens the two-letter block at "AB" rather than "AA".
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26(0), std::string("A"));
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26(1), std::string("A"));
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26(2), std::string("B"));
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26(25), std::string("Y"));
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26(26), std::string("Z"));
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26(27), std::string("AB"));
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26(28), std::string("AC"));
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26(50), std::string("AY"));
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26(51), std::string("AZ"));
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26(52), std::string("BA"));
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26(703), std::string("ACB"));

    // The two indices that broke plain base 26.
    BOTS_CHECK(!BotIdentity::HasThreeConsecutiveIdenticalLetters(BotIdentity::RunFreeBase26(540)));
    BOTS_CHECK(!BotIdentity::HasThreeConsecutiveIdenticalLetters(BotIdentity::RunFreeBase26(703)));
}

BOTS_TEST(RunFreeBase26NeverProducesAdjacentEqualLetters)
{
    // This is the property that plain base 26 does not have: index 540 renders
    // "TT" and index 703 renders "AAA", both of which the core rejects at
    // ObjectMgr.cpp:8417-8419.
    for (std::uint32_t index = 1; index <= 200000; ++index)
    {
        std::string const rendered = BotIdentity::RunFreeBase26(index);
        for (size_t i = 1; i < rendered.size(); ++i)
        {
            if (rendered[i] == rendered[i - 1])
            {
                BOTS_CHECK(false);
                std::printf("      adjacent equal letters at index %u: %s\n", index, rendered.c_str());
                return;
            }
        }
    }
}

BOTS_TEST(RunFreeBase26IsABijection)
{
    std::set<std::string> seen;
    for (std::uint32_t index = 1; index <= 200000; ++index)
    {
        std::string const rendered = BotIdentity::RunFreeBase26(index);

        for (char c : rendered)
            BOTS_CHECK(c >= 'A' && c <= 'Z');

        if (!seen.insert(rendered).second)
        {
            BOTS_CHECK(false);
            std::printf("      duplicate rendering at index %u: %s\n", index, rendered.c_str());
            return;
        }

        std::uint32_t const decoded = BotIdentity::RunFreeBase26ToIndex(rendered);
        if (decoded != index)
        {
            BOTS_CHECK(false);
            std::printf("      round trip failed at index %u: %s decoded to %u\n",
                index, rendered.c_str(), decoded);
            return;
        }
    }

    BOTS_CHECK_EQUAL(seen.size(), static_cast<size_t>(200000));
}

BOTS_TEST(RunFreeBase26DecodeRejectsMalformedWords)
{
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26ToIndex(""), static_cast<std::uint32_t>(0));
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26ToIndex("AA"), static_cast<std::uint32_t>(0));
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26ToIndex("a"), static_cast<std::uint32_t>(0));
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26ToIndex("A1"), static_cast<std::uint32_t>(0));
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26ToIndex("A"), static_cast<std::uint32_t>(1));
    BOTS_CHECK_EQUAL(BotIdentity::RunFreeBase26ToIndex("Z"), static_cast<std::uint32_t>(26));
}

BOTS_TEST(CharacterNamesAreAlphabeticAndWithinClientLimit)
{
    // Digits in a character name are rejected outright: CheckPlayerName calls
    // isValidString with numericOrSpace = false and isBasicLatinCharacter accepts
    // letters only. Every generated name must therefore be pure A-Za-z and at
    // most MAX_PLAYER_NAME long.
    for (std::uint32_t index : { 1u, 2u, 9u, 26u, 27u, 702u, 703u, 100000u, 5000000u })
    {
        std::string const name = BotIdentity::CharacterName("Bot", index);
        BOTS_CHECK_EQUAL(static_cast<int>(BotIdentity::ValidateCharacterName(name)),
            static_cast<int>(NameRuleViolation::None));
        BOTS_CHECK(name.size() <= BotIdentity::MaxCharacterNameLength);
    }
}

BOTS_TEST(CharacterNameCarriesThePrefix)
{
    BOTS_CHECK_EQUAL(BotIdentity::CharacterName("Bot", 1), std::string("BotA"));
    BOTS_CHECK_EQUAL(BotIdentity::CharacterName("Bot", 4), std::string("BotD"));
    BOTS_CHECK_EQUAL(BotIdentity::CharacterName("Havencore", 2), std::string("HavencoreB"));
}

BOTS_TEST(PrefixIsCapitalisedRegardlessOfInputCase)
{
    BOTS_CHECK_EQUAL(BotIdentity::CharacterName("bot", 1), std::string("BotA"));
    BOTS_CHECK_EQUAL(BotIdentity::CharacterName("BOT", 1), std::string("BotA"));
    BOTS_CHECK_EQUAL(BotIdentity::NormalizePrefix("hAVEN"), std::string("Haven"));
}

BOTS_TEST(PrefixValidationRejectsUnusablePrefixes)
{
    BOTS_CHECK(BotIdentity::IsValidPrefix("Bot"));
    BOTS_CHECK(BotIdentity::IsValidPrefix("Haven"));
    BOTS_CHECK(!BotIdentity::IsValidPrefix(""));
    BOTS_CHECK(!BotIdentity::IsValidPrefix("bot"));   // must start capital
    BOTS_CHECK(!BotIdentity::IsValidPrefix("B0t"));   // digit
    BOTS_CHECK(!BotIdentity::IsValidPrefix("Bo t"));  // space
    BOTS_CHECK(!BotIdentity::IsValidPrefix("Bot-"));  // punctuation
    BOTS_CHECK(!BotIdentity::IsValidPrefix("Bottt")); // three consecutive identical letters
    BOTS_CHECK(!BotIdentity::IsValidPrefix(std::string(BotIdentity::MaxCharacterNameLength, 'A')));
    // Longest prefix that still leaves room for one index letter, and is itself
    // run-free and capitalised.
    BOTS_CHECK(BotIdentity::IsValidPrefix("AbAbAbAbAbA"));
    BOTS_CHECK(!BotIdentity::IsValidPrefix("AbAbAbAbAbAB"));
}

BOTS_TEST(ThreeConsecutiveRuleMatchesTheCoreCheck)
{
    BOTS_CHECK(BotIdentity::HasThreeConsecutiveIdenticalLetters("Bottt"));
    BOTS_CHECK(BotIdentity::HasThreeConsecutiveIdenticalLetters("aaab"));
    BOTS_CHECK(BotIdentity::HasThreeConsecutiveIdenticalLetters("baaa"));
    BOTS_CHECK(BotIdentity::HasThreeConsecutiveIdenticalLetters("AAA"));
    BOTS_CHECK(!BotIdentity::HasThreeConsecutiveIdenticalLetters("BotA"));
    BOTS_CHECK(!BotIdentity::HasThreeConsecutiveIdenticalLetters("aabb"));
    BOTS_CHECK(!BotIdentity::HasThreeConsecutiveIdenticalLetters("ab"));
    BOTS_CHECK(!BotIdentity::HasThreeConsecutiveIdenticalLetters(""));
}

BOTS_TEST(ValidateCharacterNameReportsEachRuleSeparately)
{
    BOTS_CHECK_EQUAL(static_cast<int>(BotIdentity::ValidateCharacterName("")),
        static_cast<int>(NameRuleViolation::Empty));
    BOTS_CHECK_EQUAL(static_cast<int>(BotIdentity::ValidateCharacterName("A")),
        static_cast<int>(NameRuleViolation::TooShort));
    BOTS_CHECK_EQUAL(static_cast<int>(BotIdentity::ValidateCharacterName("Abc", 4)),
        static_cast<int>(NameRuleViolation::TooShort));
    BOTS_CHECK_EQUAL(static_cast<int>(BotIdentity::ValidateCharacterName("Abcdefghijklm")),
        static_cast<int>(NameRuleViolation::TooLong));
    BOTS_CHECK_EQUAL(static_cast<int>(BotIdentity::ValidateCharacterName("Bot00001")),
        static_cast<int>(NameRuleViolation::NotAlphabetic));
    BOTS_CHECK_EQUAL(static_cast<int>(BotIdentity::ValidateCharacterName("Bottt")),
        static_cast<int>(NameRuleViolation::ThreeConsecutiveIdentical));
    BOTS_CHECK_EQUAL(static_cast<int>(BotIdentity::ValidateCharacterName("BotA")),
        static_cast<int>(NameRuleViolation::None));
}

BOTS_TEST(AccountNamesAreDigitBasedLowercaseAndUnique)
{
    BOTS_CHECK_EQUAL(BotIdentity::AccountName("Bot", 1), std::string("bot00001"));
    BOTS_CHECK_EQUAL(BotIdentity::AccountName("Bot", 12345), std::string("bot12345"));
    BOTS_CHECK_EQUAL(BotIdentity::AccountName("HAVEN", 7), std::string("haven00007"));

    std::set<std::string> seen;
    for (std::uint32_t index = 1; index <= 5000; ++index)
        BOTS_CHECK(seen.insert(BotIdentity::AccountName("Bot", index)).second);
}

BOTS_TEST(ParentBattlenetEmailIsDerivedFromPrefix)
{
    BOTS_CHECK_EQUAL(BotIdentity::ParentBattlenetEmail("Bot"), std::string("bot@bots.invalid"));
    BOTS_CHECK_EQUAL(BotIdentity::ParentBattlenetEmail("Haven"), std::string("haven@bots.invalid"));
}

BOTS_TEST(EveryValidPrefixIndexPairUpToTheNameLimitIsValid)
{
    // The strongest guarantee the naming scheme can make without a live server:
    // for a usable prefix, a large contiguous block of indices all produce names
    // the core would accept.
    for (std::string const& prefix : { std::string("Bot"), std::string("Haven"), std::string("Ab") })
    {
        BOTS_CHECK(BotIdentity::IsValidPrefix(prefix));

        for (std::uint32_t index = 1; index <= 20000; ++index)
        {
            std::string const name = BotIdentity::CharacterName(prefix, index);
            if (BotIdentity::ValidateCharacterName(name) != NameRuleViolation::None)
            {
                BOTS_CHECK(false);
                std::printf("      offending name: %s (prefix %s, index %u)\n", name.c_str(), prefix.c_str(), index);
                return;
            }
        }
    }
}

BOTS_TEST(NamingCannotRescueABadPrefix)
{
    // A one-letter prefix leaves the run-free suffix free to complete a run of
    // three, and the scheme does not pretend otherwise. BotManager validates the
    // prefix at config load rather than discovering the problem on the
    // six-hundredth bot.
    BOTS_CHECK(BotIdentity::IsValidPrefix("X"));
    BOTS_CHECK_EQUAL(static_cast<int>(BotIdentity::ValidateCharacterName("XXX")),
        static_cast<int>(NameRuleViolation::ThreeConsecutiveIdentical));
    BOTS_CHECK_EQUAL(static_cast<int>(BotIdentity::ValidateCharacterName("X")),
        static_cast<int>(NameRuleViolation::TooShort));
    BOTS_CHECK_EQUAL(static_cast<int>(BotIdentity::ValidateCharacterName("XAA")),
        static_cast<int>(NameRuleViolation::None));

    // The historical failures this suite was written for: plain base 26 renders
    // "TT" for 540 and "AAA" for 703.
    BOTS_CHECK(!BotIdentity::RunFreeBase26(540).empty());
    BOTS_CHECK(!BotIdentity::HasThreeConsecutiveIdenticalLetters("Bot" + BotIdentity::RunFreeBase26(540)));
    BOTS_CHECK(!BotIdentity::HasThreeConsecutiveIdenticalLetters("Bot" + BotIdentity::RunFreeBase26(703)));
}
