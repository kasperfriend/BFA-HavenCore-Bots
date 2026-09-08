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

#include "DatabaseBotProvisioner.h"
#include "BotConfig.h"
#include "Define.h"
#include "DatabaseEnv.h"
#include "AccountMgr.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "CharacterCache.h"
#include "WorldSession.h"
#include "CharacterPackets.h"
#include "CryptoRandom.h"
#include "Log.h"
#include "MotionMaster.h"
#include <memory>

namespace Bots
{
    namespace
    {
        // The password is never used to authenticate — the injected session is
        // built by the module, not by a bnet handshake — but CreateAccount
        // requires one to derive the SRP6 salt/verifier, and a fixed value keeps
        // the accounts reproducible.
        char const* const BotAccountPassword = "BotsNotUsed1";

        std::string RandomSessionKeyHex()
        {
            std::array<uint8, 32> key;
            Trinity::Crypto::GetRandomBytes(key.data(), key.size());

            static char const hex[] = "0123456789ABCDEF";
            std::string out;
            out.reserve(key.size() * 2);
            for (uint8 byte : key)
            {
                out.push_back(hex[byte >> 4]);
                out.push_back(hex[byte & 0x0F]);
            }

            return out;
        }
    }

    bool DatabaseBotProvisioner::EnsureParentAccount(std::string const& email, uint32& parentAccountId, std::string& failure)
    {
        if (auto result = LoginDatabase.Query(
                LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_ACCOUNT_ID_BY_EMAIL), email))
        {
            parentAccountId = (*result)[0].GetUInt32();
            return true;
        }

        // sha_pass_hash is irrelevant to the injected path; an empty hash keeps
        // the row well-formed for the LEFT JOIN in LOGIN_SEL_ACCOUNT_INFO_BY_NAME.
        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_BNET_ACCOUNT);
        stmt->setString(0, email);
        stmt->setString(1, std::string());
        LoginDatabase.DirectExecute(stmt);

        if (auto result = LoginDatabase.Query(
                LoginDatabase.GetPreparedStatement(LOGIN_SEL_BNET_ACCOUNT_ID_BY_EMAIL), email))
        {
            parentAccountId = (*result)[0].GetUInt32();
            return true;
        }

        failure = "could not create or read back the battlenet parent account for " + email;
        return false;
    }

    bool DatabaseBotProvisioner::EnsureAccount(std::string const& accountName, uint32 parentAccountId,
        BotConfigValues const& config, uint32& accountId, std::string& failure)
    {
        if (uint32 existing = AccountMgr::GetId(accountName))
        {
            accountId = existing;
            return true;
        }

        AccountOpResult result = sAccountMgr->CreateAccount(accountName, BotAccountPassword,
            accountName + "@bots.invalid", parentAccountId, 1);

        if (result != AccountOpResult::AOR_OK)
        {
            failure = "AccountMgr::CreateAccount failed for " + accountName + " (code " +
                std::to_string(static_cast<uint32>(result)) + ")";
            return false;
        }

        accountId = AccountMgr::GetId(accountName);
        if (!accountId)
        {
            failure = "account " + accountName + " was created but could not be read back";
            return false;
        }

        // CreateAccount leaves expansion and os at their column defaults. Both
        // matter: expansion gates allied races / demon hunters at char-enum, and
        // os = 'Wn64' keeps InitWarden a documented no-op (WorldSession.cpp).
        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_EXPANSION);
        stmt->setUInt8(0, config.AccountExpansion);
        stmt->setUInt32(1, accountId);
        LoginDatabase.DirectExecute(stmt);

        LoginDatabase.DirectPExecute("UPDATE account SET os = '%s', session_key_bnet = '%s' WHERE id = %u",
            config.AccountOs.c_str(), RandomSessionKeyHex().c_str(), accountId);

        return true;
    }

    bool DatabaseBotProvisioner::EnsureCharacter(uint32 accountId, std::string const& characterName,
        BotConfigValues const& config, ObjectGuid& characterGuid, std::string& failure)
    {
        // Idempotent: reuse the row if a previous run already created it.
        if (QueryResult result = CharacterDatabase.PQuery(
                "SELECT guid FROM characters WHERE account = %u AND name = '%s'", accountId, characterName.c_str()))
        {
            characterGuid = ObjectGuid::CreatePlayer((*result)[0].GetUInt64());
            return true;
        }

        // A throwaway session supplies the four things Player::Create/SaveToDB
        // read — GetAccountId, GetRemoteAddress, IsARecruiter, HasPermission. A
        // null socket is fine here: the constructor only touches the socket for
        // the address and the online flag (WorldSession.cpp:150-156), and this
        // session is never added to the world.
        auto provisioningSession = std::make_unique<WorldSession>(accountId, std::string(characterName),
            0u, nullptr, SEC_PLAYER, config.AccountExpansion, 0, config.AccountOs, LOCALE_enUS,
            0u, false, AT_AUTH_FLAG_NONE, std::string());

        WorldPackets::Character::CharacterCreateInfo createInfo(characterName, config.Race, config.Class,
            config.Gender, config.Skin, config.Face, config.HairStyle, config.HairColor, config.FacialHairStyle, 0);

        // Match the core's creation path exactly (CharacterHandler.cpp:745-749):
        // a Player whose deleter runs CleanupsBeforeDelete, so a created character
        // releases anything Create/SaveToDB attached before it is destroyed.
        std::shared_ptr<Player> newChar(new Player(provisioningSession.get()), [](Player* ptr)
        {
            ptr->CleanupsBeforeDelete();
            delete ptr;
        });
        newChar->GetMotionMaster()->Initialize();

        ObjectGuid::LowType guidLow = sObjectMgr->GetGenerator<HighGuid::Player>().Generate();
        if (!newChar->Create(guidLow, &createInfo))
        {
            failure = "Player::Create failed for " + characterName + " (race " + std::to_string(config.Race) +
                ", class " + std::to_string(config.Class) + ")";
            return false;
        }

        newChar->SetAtLoginFlag(AT_LOGIN_FIRST);
        newChar->SaveToDB(true);

        characterGuid = newChar->GetGUID();
        sCharacterCache->AddCharacterCacheEntry(characterGuid, accountId, characterName,
            config.Gender, config.Race, config.Class, newChar->getLevel(), false);

        TC_LOG_INFO("bots", "Provisioned character %s (%s) on account %u",
            characterName.c_str(), characterGuid.ToString().c_str(), accountId);
        return true;
    }
}
