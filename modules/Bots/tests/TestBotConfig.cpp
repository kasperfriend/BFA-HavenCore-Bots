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
#include "../src/BotConfig.h"
#include <map>
#include <set>
#include <string>

using Bots::BotConfig;
using Bots::BotConfigValues;
using Bots::BotFeature;
using Bots::BotFeatureInfo;

namespace
{
    /// Stand-in for sConfigMgr: returns overrides where present, the passed-in
    /// default otherwise. Lets the tests assert the module's own defaults rather
    /// than whatever happens to be in worldserver.conf.
    class StubGetter : public BotConfig::Getter
    {
    public:
        std::map<std::string, std::string> Values;

        bool GetBool(std::string const& key, bool def) const override
        {
            auto const it = Values.find(key);
            if (it == Values.end())
                return def;

            return it->second == "1" || it->second == "true";
        }

        std::int32_t GetInt(std::string const& key, std::int32_t def) const override
        {
            auto const it = Values.find(key);
            return it == Values.end() ? def : static_cast<std::int32_t>(std::stoi(it->second));
        }

        std::uint32_t GetUInt(std::string const& key, std::uint32_t def) const override
        {
            auto const it = Values.find(key);
            return it == Values.end() ? def : static_cast<std::uint32_t>(std::stoul(it->second));
        }

        std::string GetString(std::string const& key, std::string const& def) const override
        {
            auto const it = Values.find(key);
            return it == Values.end() ? def : it->second;
        }
    };
}

BOTS_TEST(OnlyTheLoginGateIsOnByDefault)
{
    StubGetter getter;
    BotConfigValues const config = BotConfig::Load(getter);

    BOTS_CHECK(!config.Enable);
    BOTS_CHECK(BotConfig::IsFeatureEnabled(config, BotFeature::Login));

    for (BotFeatureInfo const& info : BotConfig::GetAllFeatures())
    {
        bool const enabled = BotConfig::IsFeatureEnabled(config, info.Feature);
        if (info.Feature == BotFeature::Login)
        {
            BOTS_CHECK(enabled);
            continue;
        }

        if (enabled)
        {
            BOTS_CHECK(false);
            std::printf("      gate unexpectedly on by default: %s\n", info.Name);
        }
    }
}

BOTS_TEST(EveryGateHasAUniqueDocumentedKey)
{
    std::set<std::string> keys;
    std::set<std::string> names;

    for (BotFeatureInfo const& info : BotConfig::GetAllFeatures())
    {
        std::string const key = BotConfig::GetFeatureKey(info.Feature);
        BOTS_CHECK(key.rfind("Bots.Features.", 0) == 0);
        BOTS_CHECK(keys.insert(key).second);
        BOTS_CHECK(names.insert(info.Name).second);
    }

    BOTS_CHECK_EQUAL(keys.size(), static_cast<size_t>(BotFeature::Count));
    BOTS_CHECK_EQUAL(names.size(), static_cast<size_t>(BotFeature::Count));
}

BOTS_TEST(GatesReadFromConfigOverrideDefaults)
{
    StubGetter getter;
    getter.Values["Bots.Enable"] = "1";
    getter.Values["Bots.Features.Login"] = "1";
    getter.Values["Bots.Features.Movement"] = "1";
    getter.Values["Bots.Features.Combat"] = "0";

    BotConfigValues const config = BotConfig::Load(getter);

    BOTS_CHECK(config.Enable);
    BOTS_CHECK(BotConfig::IsFeatureEnabled(config, BotFeature::Login));
    BOTS_CHECK(BotConfig::IsFeatureEnabled(config, BotFeature::Movement));
    BOTS_CHECK(!BotConfig::IsFeatureEnabled(config, BotFeature::Combat));
    BOTS_CHECK(!BotConfig::IsFeatureEnabled(config, BotFeature::Chat));
}

BOTS_TEST(DefaultsMatchTheDocumentedValues)
{
    StubGetter getter;
    BotConfigValues const config = BotConfig::Load(getter);

    BOTS_CHECK_EQUAL(config.NamePrefix, std::string("Bot"));
    BOTS_CHECK_EQUAL(config.LoginDelayMs, static_cast<std::uint32_t>(2000));
    BOTS_CHECK_EQUAL(config.LoginIntervalMs, static_cast<std::uint32_t>(1000));
    BOTS_CHECK_EQUAL(config.CharacterListDelayMs, static_cast<std::uint32_t>(1000));
    BOTS_CHECK_EQUAL(config.KeepAliveIntervalMs, static_cast<std::uint32_t>(30000));
    BOTS_CHECK_EQUAL(config.MaxBots, static_cast<std::uint32_t>(0));
    BOTS_CHECK_EQUAL(config.MaxLoginAttempts, static_cast<std::uint32_t>(3));
    BOTS_CHECK_EQUAL(config.StateTimeoutMs, Bots::BotLifecyclePlan::DefaultStateTimeoutMs);
    BOTS_CHECK_EQUAL(config.RetryDelayMs, Bots::BotLifecyclePlan::DefaultRetryDelayMs);
    BOTS_CHECK_EQUAL(config.AccountOs, std::string("Wn64"));
}

BOTS_TEST(ConfiguredValuesAreUsed)
{
    StubGetter getter;
    getter.Values["Bots.NamePrefix"] = "Haven";
    getter.Values["Bots.LoginDelayMs"] = "1234";
    getter.Values["Bots.MaxBots"] = "25";
    getter.Values["Bots.MaxLoginAttempts"] = "7";
    getter.Values["Bots.DefaultRace"] = "4";
    getter.Values["Bots.DefaultClass"] = "8";
    getter.Values["Bots.AccountOs"] = "Mc64";

    BotConfigValues const config = BotConfig::Load(getter);

    BOTS_CHECK_EQUAL(config.NamePrefix, std::string("Haven"));
    BOTS_CHECK_EQUAL(config.LoginDelayMs, static_cast<std::uint32_t>(1234));
    BOTS_CHECK_EQUAL(config.MaxBots, static_cast<std::uint32_t>(25));
    BOTS_CHECK_EQUAL(config.MaxLoginAttempts, static_cast<std::uint32_t>(7));
    BOTS_CHECK_EQUAL(static_cast<int>(config.Race), 4);
    BOTS_CHECK_EQUAL(static_cast<int>(config.Class), 8);
    BOTS_CHECK_EQUAL(config.AccountOs, std::string("Mc64"));
}

BOTS_TEST(ZeroAndNegativeTimingsFallBackToDefaults)
{
    // A zero keepalive interval would enqueue CMSG_KEEP_ALIVE every tick and a
    // zero login interval would spin the worker thread, so both are rejected.
    StubGetter getter;
    getter.Values["Bots.LoginIntervalMs"] = "0";
    getter.Values["Bots.KeepAliveIntervalMs"] = "0";
    getter.Values["Bots.CharacterListDelayMs"] = "0";
    getter.Values["Bots.LoginTimeoutMs"] = "-5";
    getter.Values["Bots.LoginRetryDelayMs"] = "0";

    BotConfigValues const config = BotConfig::Load(getter);

    BOTS_CHECK_EQUAL(config.LoginIntervalMs, static_cast<std::uint32_t>(1000));
    BOTS_CHECK_EQUAL(config.KeepAliveIntervalMs, static_cast<std::uint32_t>(30000));
    BOTS_CHECK_EQUAL(config.CharacterListDelayMs, static_cast<std::uint32_t>(1000));
    BOTS_CHECK_EQUAL(config.StateTimeoutMs, Bots::BotLifecyclePlan::DefaultStateTimeoutMs);
    BOTS_CHECK_EQUAL(config.RetryDelayMs, Bots::BotLifecyclePlan::DefaultRetryDelayMs);
}

BOTS_TEST(GateReportListsEveryGateAndTheMasterSwitch)
{
    StubGetter getter;
    getter.Values["Bots.Enable"] = "1";
    BotConfigValues const config = BotConfig::Load(getter);

    std::string const report = BotConfig::FormatGateReport(config);

    BOTS_CHECK(report.find("Bots.Enable = ON") != std::string::npos);
    BOTS_CHECK(report.find("Bots.Features.Login") != std::string::npos);
    BOTS_CHECK(report.find("ON") != std::string::npos);
    BOTS_CHECK(report.find("off") != std::string::npos);

    // One line per gate, plus the master switch line.
    size_t lines = 0;
    for (char c : report)
        if (c == '\n')
            ++lines;

    BOTS_CHECK_EQUAL(lines, static_cast<size_t>(BotFeature::Count) + 1);

    for (BotFeatureInfo const& info : BotConfig::GetAllFeatures())
        BOTS_CHECK(report.find(info.KeySuffix) != std::string::npos);
}

BOTS_TEST(GateReportReflectsDisabledMasterSwitch)
{
    StubGetter getter;
    BotConfigValues const config = BotConfig::Load(getter);
    BOTS_CHECK(BotConfig::FormatGateReport(config).find("Bots.Enable = OFF") != std::string::npos);
}

BOTS_TEST(OutOfRangeFeatureIsReportedDisabledNotUndefined)
{
    StubGetter getter;
    BotConfigValues config = BotConfig::Load(getter);

    BOTS_CHECK(!BotConfig::IsFeatureEnabled(config, static_cast<BotFeature>(200)));
    BOTS_CHECK(!BotConfig::IsFeatureEnabled(config, BotFeature::Count));
    BOTS_CHECK(BotConfig::GetFeatureInfo(static_cast<BotFeature>(200)).Name != nullptr);
}
