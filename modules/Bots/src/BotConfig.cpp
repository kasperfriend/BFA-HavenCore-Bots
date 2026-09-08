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

#include "BotConfig.h"
#include <array>
#include <cstdio>

namespace Bots
{
    namespace
    {
        char const FeatureKeyPrefix[] = "Bots.Features.";

        // Order must match enum class BotFeature exactly; the array is indexed by
        // the enumerator in GetFeatureInfo.
        std::array<BotFeatureInfo, static_cast<size_t>(BotFeature::Count)> const FeatureTable =
        { {
            { BotFeature::Login,             "Login",             "Login",             true  },
            { BotFeature::Movement,          "Movement",          "Movement",          false },
            { BotFeature::Combat,            "Combat",            "Combat",            false },
            { BotFeature::Chat,              "Chat",              "Chat",              false },
            { BotFeature::Group,             "Group",             "Group",             false },
            { BotFeature::Quests,            "Quests",            "Quests",            false },
            { BotFeature::Loot,              "Loot",              "Loot",              false },
            { BotFeature::Inventory,         "Inventory",         "Inventory",         false },
            { BotFeature::Trade,             "Trade",             "Trade",             false },
            { BotFeature::Mail,              "Mail",              "Mail",              false },
            { BotFeature::AuctionHouse,      "AuctionHouse",      "AuctionHouse",      false },
            { BotFeature::Guild,             "Guild",             "Guild",             false },
            { BotFeature::Battlegrounds,     "Battlegrounds",     "Battlegrounds",     false },
            { BotFeature::DungeonFinder,     "DungeonFinder",     "DungeonFinder",     false },
            { BotFeature::SkillsTalents,     "SkillsTalents",     "SkillsTalents",     false },
            { BotFeature::ItemsEquipment,    "ItemsEquipment",    "ItemsEquipment",    false },
            { BotFeature::FollowMaster,      "FollowMaster",      "FollowMaster",      false },
            { BotFeature::RespondToInvites,  "RespondToInvites",  "RespondToInvites",  false }
        } };

        uint32 GetPositive(BotConfig::Getter const& getter, std::string const& key, uint32 def)
        {
            uint32 const value = getter.GetUInt(key, def);
            return value > 0 ? value : def;
        }
    }

    BotFeatureInfo const& BotConfig::GetFeatureInfo(BotFeature feature)
    {
        size_t const index = static_cast<size_t>(feature);
        return FeatureTable[index < FeatureTable.size() ? index : 0];
    }

    std::vector<BotFeatureInfo> const& BotConfig::GetAllFeatures()
    {
        // std::array and std::vector are not interchangeable here, so the table is
        // copied once into a static vector that callers can iterate generically.
        static std::vector<BotFeatureInfo> const all(FeatureTable.begin(), FeatureTable.end());
        return all;
    }

    std::string BotConfig::GetFeatureKey(BotFeature feature)
    {
        return std::string(FeatureKeyPrefix) + GetFeatureInfo(feature).KeySuffix;
    }

    BotConfigValues BotConfig::Load(BotConfig::Getter const& getter)
    {
        BotConfigValues config;

        config.Enable = getter.GetBool("Bots.Enable", false);

        for (BotFeatureInfo const& info : FeatureTable)
            config.FeatureEnabled[static_cast<size_t>(info.Feature)] =
                getter.GetBool(std::string(FeatureKeyPrefix) + info.KeySuffix, info.DefaultEnabled);

        config.NamePrefix = getter.GetString("Bots.NamePrefix", "Bot");

        config.LoginDelayMs = getter.GetUInt("Bots.LoginDelayMs", 2000);
        config.LoginIntervalMs = GetPositive(getter, "Bots.LoginIntervalMs", 1000);
        config.CharacterListDelayMs = GetPositive(getter, "Bots.CharacterListDelayMs", 1000);
        config.KeepAliveIntervalMs = GetPositive(getter, "Bots.KeepAliveIntervalMs", 30000);

        config.MaxBots = getter.GetUInt("Bots.MaxBots", 0);
        config.MaxLoginAttempts = getter.GetUInt("Bots.MaxLoginAttempts", 3);

        int32 const stateTimeout = getter.GetInt("Bots.LoginTimeoutMs", static_cast<int32>(BotLifecyclePlan::DefaultStateTimeoutMs));
        config.StateTimeoutMs = stateTimeout > 0 ? static_cast<uint32>(stateTimeout) : BotLifecyclePlan::DefaultStateTimeoutMs;

        int32 const retryDelay = getter.GetInt("Bots.LoginRetryDelayMs", static_cast<int32>(BotLifecyclePlan::DefaultRetryDelayMs));
        config.RetryDelayMs = retryDelay > 0 ? static_cast<uint32>(retryDelay) : BotLifecyclePlan::DefaultRetryDelayMs;

        config.Race = static_cast<uint8>(getter.GetUInt("Bots.DefaultRace", 1));
        config.Class = static_cast<uint8>(getter.GetUInt("Bots.DefaultClass", 1));
        config.Gender = static_cast<uint8>(getter.GetUInt("Bots.DefaultGender", 0));
        config.Skin = static_cast<uint8>(getter.GetUInt("Bots.DefaultSkin", 0));
        config.Face = static_cast<uint8>(getter.GetUInt("Bots.DefaultFace", 0));
        config.HairStyle = static_cast<uint8>(getter.GetUInt("Bots.DefaultHairStyle", 0));
        config.HairColor = static_cast<uint8>(getter.GetUInt("Bots.DefaultHairColor", 0));
        config.FacialHairStyle = static_cast<uint8>(getter.GetUInt("Bots.DefaultFacialHairStyle", 0));
        config.AccountExpansion = static_cast<uint8>(getter.GetUInt("Bots.AccountExpansion", 7));
        config.AccountOs = getter.GetString("Bots.AccountOs", "Wn64");

        return config;
    }

    bool BotConfig::IsFeatureEnabled(BotConfigValues const& config, BotFeature feature)
    {
        size_t const index = static_cast<size_t>(feature);
        if (index >= static_cast<size_t>(BotFeature::Count))
            return false;

        return config.FeatureEnabled[index];
    }

    std::string BotConfig::FormatGateReport(BotConfigValues const& config)
    {
        std::string report = std::string("Bots.Enable = ") + (config.Enable ? "ON" : "OFF") + "\n";

        for (BotFeatureInfo const& info : FeatureTable)
        {
            char line[96];
            snprintf(line, sizeof(line), "  Bots.Features.%-18s = %s\n", info.KeySuffix,
                IsFeatureEnabled(config, info.Feature) ? "ON" : "off");
            report += line;
        }

        return report;
    }
}
