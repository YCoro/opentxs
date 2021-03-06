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

#ifndef OPENTXS_STORAGE_TREE_ACCOUNTS_HPP
#define OPENTXS_STORAGE_TREE_ACCOUNTS_HPP

#include "Internal.hpp"

#include "opentxs/core/Identifier.hpp"

#include "Node.hpp"

#include <map>
#include <set>

namespace opentxs::storage
{
class Accounts : public Node
{
public:
    OTIdentifier AccountContract(const Identifier& accountID) const;
    OTIdentifier AccountIssuer(const Identifier& accountID) const;
    OTIdentifier AccountOwner(const Identifier& accountID) const;
    OTIdentifier AccountServer(const Identifier& accountID) const;
    OTIdentifier AccountSigner(const Identifier& accountID) const;
    proto::ContactItemType AccountUnit(const Identifier& accountID) const;
    std::set<OTIdentifier> AccountsByContract(const Identifier& unit) const;
    std::set<OTIdentifier> AccountsByIssuer(const Identifier& issuerNym) const;
    std::set<OTIdentifier> AccountsByOwner(const Identifier& ownerNym) const;
    std::set<OTIdentifier> AccountsByServer(const Identifier& server) const;
    std::set<OTIdentifier> AccountsByUnit(
        const proto::ContactItemType unit) const;
    std::string Alias(const std::string& id) const;
    bool Load(
        const std::string& id,
        std::string& output,
        std::string& alias,
        const bool checking) const;

    bool Delete(const std::string& id);
    bool SetAlias(const std::string& id, const std::string& alias);
    bool Store(
        const std::string& id,
        const std::string& data,
        const std::string& alias,
        const Identifier& ownerNym,
        const Identifier& signerNym,
        const Identifier& issuerNym,
        const Identifier& server,
        const Identifier& contract,
        const proto::ContactItemType unit);

    ~Accounts() = default;

private:
    friend class Tree;

    using Index = std::map<OTIdentifier, std::set<OTIdentifier>>;
    using UnitIndex = std::map<proto::ContactItemType, std::set<OTIdentifier>>;
    /** owner, signer, issuer, server, contract, unit */
    using AccountData = std::tuple<
        OTIdentifier,
        OTIdentifier,
        OTIdentifier,
        OTIdentifier,
        OTIdentifier,
        proto::ContactItemType>;
    using ReverseIndex = std::map<OTIdentifier, AccountData>;

    Index owner_index_{};
    Index signer_index_{};
    Index issuer_index_{};
    Index server_index_{};
    Index contract_index_{};
    UnitIndex unit_index_{};
    mutable ReverseIndex account_data_{};

    static bool add_set_index(
        const Identifier& accountID,
        const Identifier& argID,
        Identifier& mapID,
        Index& index);

    template <typename K, typename I>
    static void erase(const Identifier& accountID, const K& key, I& index)
    {
        try {
            auto& set = index.at(key);
            set.erase(accountID);

            if (0 == set.size()) { index.erase(key); }
        } catch (...) {
        }
    }

    AccountData& get_account_data(
        const Lock& lock,
        const OTIdentifier& accountID) const;
    proto::StorageAccounts serialize() const;

    bool check_update_account(
        const Lock& lock,
        const OTIdentifier& accountID,
        const Identifier& ownerNym,
        const Identifier& signerNym,
        const Identifier& issuerNym,
        const Identifier& server,
        const Identifier& contract,
        const proto::ContactItemType unit);
    void init(const std::string& hash) override;
    bool save(const Lock& lock) const override;

    Accounts(
        const opentxs::api::storage::Driver& storage,
        const std::string& key);
    Accounts() = delete;
    Accounts(const Accounts&) = delete;
    Accounts(Accounts&&) = delete;
    Accounts operator=(const Accounts&) = delete;
    Accounts operator=(Accounts&&) = delete;
};
}  // namespace opentxs::storage
#endif  // OPENTXS_STORAGE_TREE_ACCOUNTS_HPP
