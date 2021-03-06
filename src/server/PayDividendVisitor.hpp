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

#ifndef OPENTXS_SERVER_ACCTFUNCTOR_PAYDIVIDEND_HPP
#define OPENTXS_SERVER_ACCTFUNCTOR_PAYDIVIDEND_HPP

#include "Internal.hpp"

#include "opentxs/core/AccountVisitor.hpp"

#include <cstdint>

namespace opentxs
{

class Account;
class Identifier;
class String;

namespace server
{
class Server;
}

// Note: from OTUnitDefinition.h and .cpp.
// This is a subclass of AccountVisitor, which is used whenever OTUnitDefinition
// needs to
// loop through all the accounts for a given instrument definition (its own.)
// This subclass
// needs to
// call Server method to do its job, so it can't be defined in otlib, but must
// be defined
// here in otserver (so it can see the methods that it needs...)
//
class PayDividendVisitor : public AccountVisitor
{
    Identifier* m_pNymID{nullptr};
    Identifier* m_pPayoutInstrumentDefinitionID{nullptr};
    Identifier* m_pVoucherAcctID{nullptr};
    String* m_pstrMemo{nullptr};  // contains the original payDividend item from
                                  // the payDividend transaction request.
                                  // (Stored in the memo field for each
                                  // voucher.)
    server::Server* m_pServer{nullptr};  // no need to cleanup. It's here for
                                         // convenience only.
    std::int64_t m_lPayoutPerShare{0};
    std::int64_t m_lAmountPaidOut{0};   // as we pay each voucher out, we keep a
                                        // running count.
    std::int64_t m_lAmountReturned{0};  // as we pay each voucher out, we keep a
                                        // running count.

public:
    PayDividendVisitor(
        const Identifier& theNotaryID,
        const Identifier& theNymID,
        const Identifier& thePayoutInstrumentDefinitionID,
        const Identifier& theVoucherAcctID,
        const String& strMemo,
        server::Server& theServer,
        std::int64_t lPayoutPerShare);
    virtual ~PayDividendVisitor();

    Identifier* GetNymID() { return m_pNymID; }
    Identifier* GetPayoutInstrumentDefinitionID()
    {
        return m_pPayoutInstrumentDefinitionID;
    }
    Identifier* GetVoucherAcctID() { return m_pVoucherAcctID; }
    String* GetMemo() { return m_pstrMemo; }
    server::Server* GetServer() { return m_pServer; }
    std::int64_t GetPayoutPerShare() { return m_lPayoutPerShare; }
    std::int64_t GetAmountPaidOut() { return m_lAmountPaidOut; }
    std::int64_t GetAmountReturned() { return m_lAmountReturned; }

    bool Trigger(const Account& theAccount) override;
};

}  // namespace opentxs

#endif  // OPENTXS_SERVER_ACCTFUNCTOR_PAYDIVIDEND_HPP
