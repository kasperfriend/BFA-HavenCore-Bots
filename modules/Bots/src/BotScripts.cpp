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
#include "BotManager.h"
#include "ScriptMgr.h"
#include "Chat.h"
#include "WorldPacket.h"
#include <cstdlib>
#include <string>
#include <vector>

namespace Bots
{
    /// Drives the module's lifetime from the world's own start/stop hooks.
    class BotsWorldScript : public WorldScript
    {
    public:
        BotsWorldScript() : WorldScript("BotsWorldScript") { }

        void OnStartup() override
        {
            sBotManager->Initialize();
            sBotManager->StartWorker();
        }

        void OnShutdown() override
        {
            sBotManager->StopWorker();
        }
    };

    /// The module's only window into the server->bot direction. OnPacketSend is a
    /// ServerScript hook (ScriptMgr.cpp:1462) and the packet handed here is a
    /// copy, so reading it is safe and does not advance the real one.
    class BotsServerScript : public ServerScript
    {
    public:
        BotsServerScript() : ServerScript("BotsServerScript") { }

        void OnPacketSend(WorldSession* session, WorldPacket& packet) override
        {
            sBotManager->OnPacketSend(session, packet);
        }
    };

    /// `.bot login N | logout | status | gates`. Every subcommand enqueues work
    /// for the worker thread and returns immediately; none provisions inline.
    class BotsCommandScript : public CommandScript
    {
    public:
        BotsCommandScript() : CommandScript("BotsCommandScript") { }

        std::vector<ChatCommand> GetCommands() const override
        {
            static std::vector<ChatCommand> commands =
            {
                { "bot", RBAC_PERM_BOTS_COMMAND, true, &HandleBotCommand,
                  "bot login <count> | logout | status | gates", { } }
            };

            return commands;
        }

        static bool HandleBotCommand(ChatHandler* handler, char const* args)
        {
            if (!sBotManager->IsEnabled())
            {
                handler->SendSysMessage("Bots module is disabled (Bots.Enable = 0).");
                return false;
            }

            std::string command = args && *args ? args : "";
            std::string subcommand = command;
            std::string parameter;

            size_t const space = command.find(' ');
            if (space != std::string::npos)
            {
                subcommand = command.substr(0, space);
                parameter = command.substr(space + 1);
            }

            if (subcommand == "login")
            {
                uint32 const count = parameter.empty() ? 1u : static_cast<uint32>(strtoul(parameter.c_str(), nullptr, 10));
                if (count == 0)
                {
                    handler->SendSysMessage("Usage: bot login <count>");
                    return false;
                }

                sBotManager->RequestLogin(count);
                handler->PSendSysMessage("Queued %u bot(s) for login.", count);
                return true;
            }

            if (subcommand == "logout")
            {
                sBotManager->RequestLogoutAll();
                handler->SendSysMessage("Queued logout for all bots.");
                return true;
            }

            if (subcommand == "status")
            {
                handler->SendSysMessage(sBotManager->GetStatusReport().c_str());
                return true;
            }

            if (subcommand == "gates")
            {
                handler->SendSysMessage(BotConfig::FormatGateReport(sBotManager->GetConfig()).c_str());
                return true;
            }

            handler->SendSysMessage("Usage: bot login <count> | logout | status | gates");
            return false;
        }
    };
}

namespace Bots
{
    void RegisterBotsScripts()
    {
        new BotsWorldScript();
        new BotsServerScript();
        new BotsCommandScript();
    }
}
