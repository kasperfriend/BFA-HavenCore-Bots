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

#ifndef Bots_BotSocket_h__
#define Bots_BotSocket_h__

#include "Define.h"
#include "IoContext.h"
#include <boost/asio/ip/tcp.hpp>
#include <memory>
#include <utility>

class WorldSocket;

namespace Bots
{
    /// A `WorldSession` stores a concrete `std::shared_ptr<WorldSocket>`
    /// (WorldSession.h:2149) and `WorldSocket` is `Socket<WorldSocket>`
    /// (WorldSocket.h:76), a CRTP type that cannot be subclassed into a second
    /// CRTP socket. `Socket`'s constructor also calls
    /// `_socket.remote_endpoint()` (shared/Networking/Socket.h:68), so it demands
    /// a genuinely connected stream — there is no null-socket specialisation.
    ///
    /// The module therefore hands the core a *real* `WorldSocket` built over a
    /// loopback TCP stub that goes nowhere. `BotSocketHub` owns the `io_context`
    /// and the single ephemeral loopback listener those stubs are made from.
    ///
    /// The socket is never `Start()`ed — `Start()` is the only thing that posts
    /// the first `AsyncRead` and the IP-check query (WorldSocket.cpp:55-62) — and
    /// it is never registered with `WorldSocketMgr`, so no `NetworkThread` polls
    /// it. The module worker calls `WorldSocket::Update()` itself, which drains
    /// the outbound queue and writes it into the stub; the peer is retained and
    /// never read, so the bytes are simply discarded. `IsOpen()` stays true
    /// because `_closed` is never set, which is what keeps the session alive.
    class BotSocketHub
    {
    public:
        BotSocketHub();
        ~BotSocketHub();

        BotSocketHub(BotSocketHub const&) = delete;
        BotSocketHub& operator=(BotSocketHub const&) = delete;

        /// Binds the loopback listener. Returns false (and logs) if the bind
        /// fails, in which case the module disables itself rather than spawning
        /// bots that cannot be given a socket.
        bool Open();

        bool IsOpen() const { return _acceptor != nullptr; }

        /// Builds one connected loopback `WorldSocket`. The returned peer is the
        /// other end of the stub: the caller must retain it for as long as the
        /// socket lives so the connection stays ESTABLISHED and the eventual
        /// `Socket::CloseSocket` shutdown succeeds without logging an error.
        /// Returns {nullptr, nullptr} if the stub cannot be created.
        std::pair<std::shared_ptr<WorldSocket>, std::shared_ptr<boost::asio::ip::tcp::socket>> MakeStub();

    private:
        Trinity::Asio::IoContext _ioContext;
        std::unique_ptr<boost::asio::ip::tcp::acceptor> _acceptor;
        boost::asio::ip::tcp::endpoint _endpoint;
    };
}

#endif // Bots_BotSocket_h__
