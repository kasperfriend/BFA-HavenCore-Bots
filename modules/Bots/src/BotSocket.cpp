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

#include "BotSocket.h"
#include "WorldSocket.h"
#include "Log.h"
#include <boost/asio/ip/tcp.hpp>
#include <utility>

using boost::asio::ip::tcp;

namespace Bots
{
    BotSocketHub::BotSocketHub() : _ioContext(1)
    {
    }

    BotSocketHub::~BotSocketHub()
    {
        _ioContext.stop();
        _acceptor.reset();
    }

    bool BotSocketHub::Open()
    {
        if (_acceptor)
            return true;

        try
        {
            auto acceptor = std::make_unique<tcp::acceptor>(_ioContext);
            acceptor->open(tcp::v4());
            acceptor->set_option(tcp::acceptor::reuse_address(true));
            acceptor->bind(tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0));
            acceptor->listen();

            _endpoint = acceptor->local_endpoint();
            _acceptor = std::move(acceptor);
        }
        catch (boost::system::system_error const& err)
        {
            TC_LOG_ERROR("bots", "BotSocketHub could not open a loopback listener: %s", err.what());
            _acceptor.reset();
            return false;
        }

        TC_LOG_INFO("bots", "Loopback socket hub listening on %s:%u",
            _endpoint.address().to_string().c_str(), _endpoint.port());
        return true;
    }

    std::pair<std::shared_ptr<WorldSocket>, std::shared_ptr<tcp::socket>> BotSocketHub::MakeStub()
    {
        if (!_acceptor)
            return { nullptr, nullptr };

        try
        {
            // Both operations are synchronous and complete before returning: a
            // loopback connect() is answered by the kernel immediately and lands
            // in the listen backlog, so the following accept() does not block.
            // The io_context is therefore never run() — it only has to outlive the
            // sockets, which the hub does.
            auto client = std::make_shared<tcp::socket>(_ioContext);
            client->connect(_endpoint);

            auto peer = std::make_shared<tcp::socket>(_ioContext);
            _acceptor->accept(*peer);

            auto world = std::make_shared<WorldSocket>(std::move(*client));
            return { world, peer };
        }
        catch (boost::system::system_error const& err)
        {
            TC_LOG_ERROR("bots", "BotSocketHub could not build a loopback stub: %s", err.what());
            return { nullptr, nullptr };
        }
    }
}
