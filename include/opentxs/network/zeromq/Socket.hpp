/************************************************************
 *
 *                 OPEN TRANSACTIONS
 *
 *       Financial Cryptography and Digital Cash
 *       Library, Protocol, API, Server, CLI, GUI
 *
 *       -- Anonymous Numbered Accounts.
 *       -- Untraceable Digital Cash.
 *       -- Triple-Signed Receipts.
 *       -- Cheques, Vouchers, Transfers, Inboxes.
 *       -- Basket Currencies, Markets, Payment Plans.
 *       -- Signed, XML, Ricardian-style Contracts.
 *       -- Scripted smart contracts.
 *
 *  EMAIL:
 *  fellowtraveler@opentransactions.org
 *
 *  WEBSITE:
 *  http://www.opentransactions.org/
 *
 *  -----------------------------------------------------
 *
 *   LICENSE:
 *   This Source Code Form is subject to the terms of the
 *   Mozilla Public License, v. 2.0. If a copy of the MPL
 *   was not distributed with this file, You can obtain one
 *   at http://mozilla.org/MPL/2.0/.
 *
 *   DISCLAIMER:
 *   This program is distributed in the hope that it will
 *   be useful, but WITHOUT ANY WARRANTY; without even the
 *   implied warranty of MERCHANTABILITY or FITNESS FOR A
 *   PARTICULAR PURPOSE.  See the Mozilla Public License
 *   for more details.
 *
 ************************************************************/

#ifndef OPENTXS_NETWORK_ZEROMQ_SOCKET_HPP
#define OPENTXS_NETWORK_ZEROMQ_SOCKET_HPP

#include "opentxs/Forward.hpp"

#include "opentxs/Types.hpp"

#include <cstdint>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <tuple>

#ifdef SWIG
// clang-format off
%extend opentxs::network::zeromq::Socket {
    bool SetTimeouts(
        const int& lingerMilliseconds,
        const int& sendMilliseconds,
        const int& receiveMilliseconds) const
    {
        return $self->SetTimeouts(
            std::chrono::milliseconds(lingerMilliseconds),
            std::chrono::milliseconds(sendMilliseconds),
            std::chrono::milliseconds(receiveMilliseconds));
    }
}
%ignore opentxs::network::zeromq::Socket::Context;
%ignore opentxs::network::zeromq::Socket::SetTimeouts;
%ignore opentxs::network::zeromq::Socket::operator void*() const;
%rename(ZMQSocket) opentxs::network::zeromq::Socket;
// clang-format on
#endif  // SWIG

namespace opentxs
{
namespace network
{
namespace zeromq
{
class Socket
{
public:
    using MultipartSendResult = std::pair<SendResult, OTZMQMessage>;

    EXPORT static const std::string AccountUpdateEndpoint;
    EXPORT static const std::string ContactUpdateEndpoint;
    EXPORT static const std::string NymDownloadEndpoint;
    EXPORT static const std::string PairEndpointPrefix;
    EXPORT static const std::string PairEventEndpoint;
    EXPORT static const std::string PendingBailmentEndpoint;
    EXPORT static const std::string ThreadUpdateEndpoint;
    EXPORT static const std::string WidgetUpdateEndpoint;
    EXPORT static const std::string WorkflowAccountUpdateEndpoint;

    EXPORT virtual operator void*() const = 0;

    EXPORT virtual bool Close() const = 0;
    EXPORT virtual const class Context& Context() const = 0;
    EXPORT virtual bool SetTimeouts(
        const std::chrono::milliseconds& linger,
        const std::chrono::milliseconds& send,
        const std::chrono::milliseconds& receive) const = 0;
    EXPORT virtual bool Start(const std::string& endpoint) const = 0;
    EXPORT virtual SocketType Type() const = 0;

    EXPORT virtual ~Socket() = default;

protected:
    Socket() = default;

private:
    Socket(const Socket&) = delete;
    Socket(Socket&&) = default;
    Socket& operator=(const Socket&) = delete;
    Socket& operator=(Socket&&) = default;
};
}  // namespace zeromq
}  // namespace network
}  // namespace opentxs
#endif  // OPENTXS_NETWORK_ZEROMQ_SOCKET_HPP
