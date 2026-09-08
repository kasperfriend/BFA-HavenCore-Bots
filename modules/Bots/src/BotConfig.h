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

#ifndef Bots_BotConfig_h__
#define Bots_BotConfig_h__

#include "BotLifecyclePlan.h"
#include "BotTypes.h"
#include <string>
#include <vector>

namespace Bots
{
    /// Every capability a ported bot would eventually have. Iteration 1 enables
    /// exactly one of them. The list is the single source of truth for both the
    /// config keys and the `.bot gates` report, so a new capability cannot be
    /// added without also declaring its default here.
    enum class BotFeature : uint8
    {
        Login = 0,
        Movement,
        Combat,
        Chat,
        Group,
        Quests,
        Loot,
        Inventory,
        Trade,
        Mail,
        AuctionHouse,
        Guild,
        Battlegrounds,
        DungeonFinder,
        SkillsTalents,
        ItemsEquipment,
        FollowMaster,
        RespondToInvites,
        Count
    };

    struct BotFeatureInfo
    {
        BotFeature Feature;
        char const* Name;
        /// Key under Bots.Features. in worldserver.conf.
        char const* KeySuffix;
        /// Shipped default. Only Login is true.
        bool DefaultEnabled;
    };

    struct BotConfigValues
    {
        /// Indexed by BotFeature. Populated by Load; IsFeatureEnabled reads it.
        bool FeatureEnabled[static_cast<size_t>(BotFeature::Count)] = {};

        bool Enable = false;
        std::string NamePrefix = "Bot";
        uint32 LoginDelayMs = 2000;
        uint32 LoginIntervalMs = 1000;
        uint32 MaxBots = 0;
        uint32 StateTimeoutMs = BotLifecyclePlan::DefaultStateTimeoutMs;
        uint32 RetryDelayMs = BotLifecyclePlan::DefaultRetryDelayMs;
        uint32 MaxLoginAttempts = 3;
        uint32 KeepAliveIntervalMs = 30000;
        uint32 CharacterListDelayMs = 1000;

        uint8 Race = 1;
        uint8 Class = 1;
        uint8 Gender = 0;
        uint8 Skin = 0;
        uint8 Face = 0;
        uint8 HairStyle = 0;
        uint8 HairColor = 0;
        uint8 FacialHairStyle = 0;
        uint8 AccountExpansion = 7;
        std::string AccountOs = "Wn64";
    };

    /// Configuration and the feature-gate table.
    ///
    /// Parsing lives behind the Getter indirection so the table, the defaults
    /// and the key names are testable without the core's Config singleton.
    class BotConfig
    {
    public:
        class Getter
        {
        public:
            virtual ~Getter() = default;
            virtual bool GetBool(std::string const& key, bool def) const = 0;
            virtual int32 GetInt(std::string const& key, int32 def) const = 0;
            virtual uint32 GetUInt(std::string const& key, uint32 def) const = 0;
            virtual std::string GetString(std::string const& key, std::string const& def) const = 0;
        };

        static BotFeatureInfo const& GetFeatureInfo(BotFeature feature);
        static std::vector<BotFeatureInfo> const& GetAllFeatures();
        static std::string GetFeatureKey(BotFeature feature);

        /// Loads every key. Negative or zero values for the timings fall back to
        /// the documented default rather than being accepted, because a zero
        /// keepalive interval would make the module enqueue a CMSG_KEEP_ALIVE on
        /// every tick and a zero login interval would spin the worker thread.
        static BotConfigValues Load(Getter const& getter);

        static bool IsFeatureEnabled(BotConfigValues const& config, BotFeature feature);

        /// One line per feature, e.g. "Login       = ON". Printed at startup and
        /// by `.bot gates` so the live capability set is never a matter of
        /// reading source.
        static std::string FormatGateReport(BotConfigValues const& config);
    };
}

#endif // Bots_BotConfig_h__
