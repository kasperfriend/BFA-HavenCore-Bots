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

#include "BotIdentity.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace Bots
{
    namespace
    {
        char const RunFreeAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        constexpr uint32 RunFreeAlphabetSize = 26;
        // Every letter after the leading one may be anything except the letter
        // immediately to its left, which leaves 25 choices per position.
        constexpr uint32 RunFreeInnerRadix = RunFreeAlphabetSize - 1;

        // Number of run-free words of exactly this many letters: a free leading
        // letter times one constrained letter per remaining position.
        uint64 RunFreeWordCount(uint32 length)
        {
            uint64 count = RunFreeAlphabetSize;
            for (uint32 i = 1; i < length; ++i)
                count *= RunFreeInnerRadix;

            return count;
        }

        bool IsAsciiLetter(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        }

        std::string LowercaseAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }
    }

    std::string BotIdentity::RunFreeBase26(uint32 index)
    {
        if (index == 0)
            return std::string(1, RunFreeAlphabet[0]);

        // Words are numbered in positional order: every 1-letter word, then
        // every 2-letter word, and so on. Locate the length block holding the
        // index, then convert the offset inside that block.
        uint32 length = 1;
        uint64 offset = index - 1;

        while (offset >= RunFreeWordCount(length))
        {
            offset -= RunFreeWordCount(length);
            ++length;
        }

        // The leading letter is unconstrained, so it divides the block into 25
        // equal sub-blocks of RunFreeWordCount(length) / 26 each.
        uint64 const subBlock = RunFreeWordCount(length) / RunFreeAlphabetSize;
        std::string word;
        word.reserve(length);
        word.push_back(RunFreeAlphabet[offset / subBlock]);
        offset %= subBlock;

        // Each remaining letter has radix 25: it may be anything except the
        // letter already placed to its left. That is the whole point of the
        // scheme, since a name may not repeat a letter three times in a row.
        for (uint32 i = 1; i < length; ++i)
        {
            uint64 const divisor = RunFreeWordCount(length - i) / RunFreeAlphabetSize;
            uint32 const digit = static_cast<uint32>(offset / divisor);
            offset %= divisor;

            uint32 const excluded = static_cast<uint32>(word.back() - 'A');
            word.push_back(RunFreeAlphabet[digit < excluded ? digit : digit + 1]);
        }

        return word;
    }

    uint32 BotIdentity::RunFreeBase26ToIndex(std::string const& word)
    {
        if (word.empty())
            return 0;

        for (char c : word)
            if (c < 'A' || c > 'Z')
                return 0;

        // Inverse of RunFreeBase26: skip every shorter length block, then read
        // the word as positional digits. The leading letter is unconstrained and
        // every later letter skips the one to its left.
        uint64 offset = 0;
        for (uint32 length = 1; length < word.size(); ++length)
            offset += RunFreeWordCount(length);

        uint64 const subBlock = RunFreeWordCount(static_cast<uint32>(word.size())) / RunFreeAlphabetSize;
        offset += static_cast<uint64>(word[0] - 'A') * subBlock;

        for (size_t i = 1; i < word.size(); ++i)
        {
            uint32 const value = static_cast<uint32>(word[i] - 'A');
            uint32 const left = static_cast<uint32>(word[i - 1] - 'A');

            // Adjacent equal letters are what the encoder refuses to emit, so a
            // word containing one is not a name this module produced.
            if (value == left)
                return 0;

            uint64 const divisor = RunFreeWordCount(static_cast<uint32>(word.size() - i)) / RunFreeAlphabetSize;
            uint32 const digit = value > left ? value - 1 : value;
            offset += digit * divisor;
        }

        return static_cast<uint32>(offset + 1);
    }

    std::string BotIdentity::CharacterName(std::string const& prefix, uint32 index)
    {
        return NormalizePrefix(prefix) + RunFreeBase26(index);
    }

    std::string BotIdentity::AccountName(std::string const& prefix, uint32 index)
    {
        // Fixed width keeps account names sortable in the auth database and
        // cannot collide, because the index is part of the name.
        char suffix[16];
        snprintf(suffix, sizeof(suffix), "%05u", static_cast<unsigned>(index));
        return LowercaseAscii(prefix) + suffix;
    }

    std::string BotIdentity::ParentBattlenetEmail(std::string const& prefix)
    {
        return LowercaseAscii(prefix) + "@bots.invalid";
    }

    bool BotIdentity::HasThreeConsecutiveIdenticalLetters(std::string const& name)
    {
        std::string const lower = LowercaseAscii(name);
        for (size_t i = 2; i < lower.size(); ++i)
            if (lower[i] == lower[i - 1] && lower[i] == lower[i - 2])
                return true;

        return false;
    }

    NameRuleViolation BotIdentity::ValidateCharacterName(std::string const& name, uint32 minLength)
    {
        if (name.empty())
            return NameRuleViolation::Empty;

        if (name.size() > MaxCharacterNameLength)
            return NameRuleViolation::TooLong;

        if (name.size() < minLength)
            return NameRuleViolation::TooShort;

        for (char c : name)
            if (!IsAsciiLetter(c))
                return NameRuleViolation::NotAlphabetic;

        if (HasThreeConsecutiveIdenticalLetters(name))
            return NameRuleViolation::ThreeConsecutiveIdentical;

        return NameRuleViolation::None;
    }

    bool BotIdentity::IsValidPrefix(std::string const& prefix)
    {
        if (prefix.empty())
            return false;

        for (char c : prefix)
            if (!IsAsciiLetter(c))
                return false;

        // One letter of headroom for the smallest index rendering ("A"), so a
        // prefix of MaxCharacterNameLength letters can never produce a name.
        if (prefix.size() >= MaxCharacterNameLength)
            return false;

        if (!std::isupper(static_cast<unsigned char>(prefix.front())))
            return false;

        return !HasThreeConsecutiveIdenticalLetters(prefix);
    }

    std::string BotIdentity::NormalizePrefix(std::string const& prefix)
    {
        std::string normalized = LowercaseAscii(prefix);

        // Only alphabetic prefixes reach this point in the normal path; anything
        // else is passed through untouched so the caller's validation reports it
        // rather than this function silently rewriting it.
        if (!normalized.empty() && IsAsciiLetter(normalized.front()))
            normalized.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(normalized.front())));

        return normalized;
    }
}

