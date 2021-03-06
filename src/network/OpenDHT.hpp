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

#ifndef OPENTXS_NETWORK_IMPLEMENTATION_OPENDHT_HPP
#define OPENTXS_NETWORK_IMPLEMENTATION_OPENDHT_HPP

#include "Internal.hpp"

#if OT_DHT
#include "opentxs/network/OpenDHT.hpp"

#include "opentxs/core/Flag.hpp"

#include <atomic>
#include <memory>
#include <mutex>

namespace dht
{
class DhtRunner;
}

namespace opentxs::network::implementation
{
class OpenDHT : virtual public network::OpenDHT
{
public:
    void Insert(
        const std::string& key,
        const std::string& value,
        DhtDoneCallback cb = {}) const override;
    void Retrieve(
        const std::string& key,
        DhtResultsCallback vcb,
        DhtDoneCallback dcb = {}) const override;

    ~OpenDHT();

private:
    friend class api::network::implementation::Dht;

    std::unique_ptr<const DhtConfig> config_;
    std::unique_ptr<dht::DhtRunner> node_;
    mutable OTFlag loaded_;
    mutable OTFlag ready_;
    mutable std::mutex init_;

    bool Init() const;

    OpenDHT(const DhtConfig& config);
    OpenDHT() = delete;
    OpenDHT(const OpenDHT&) = delete;
    OpenDHT(OpenDHT&&) = delete;
    OpenDHT& operator=(const OpenDHT&) = delete;
    OpenDHT& operator=(OpenDHT&&) = delete;
};
}  // namespace opentxs::network::implementation
#endif  // OT_DHT
#endif  // OPENTXS_NETWORK_IMPLEMENTATION_OPENDHT_HPP
