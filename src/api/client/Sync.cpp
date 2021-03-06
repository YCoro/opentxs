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

#include "stdafx.hpp"

#include "opentxs/api/client/Pair.hpp"
#include "opentxs/api/client/Sync.hpp"
#include "opentxs/api/client/ServerAction.hpp"
#include "opentxs/api/client/Wallet.hpp"
#include "opentxs/api/client/Workflow.hpp"
#include "opentxs/api/crypto/Encode.hpp"
#include "opentxs/api/storage/Storage.hpp"
#include "opentxs/api/Api.hpp"
#include "opentxs/api/ContactManager.hpp"
#include "opentxs/api/Settings.hpp"
#if OT_CASH
#include "opentxs/cash/Purse.hpp"
#endif  // OT_CASH
#include "opentxs/client/NymData.hpp"
#include "opentxs/client/OT_API.hpp"
#include "opentxs/client/OTAPI_Exec.hpp"
#include "opentxs/client/OTWallet.hpp"
#include "opentxs/client/ServerAction.hpp"
#include "opentxs/client/Utility.hpp"
#include "opentxs/consensus/ServerContext.hpp"
#include "opentxs/contact/Contact.hpp"
#include "opentxs/contact/ContactData.hpp"
#include "opentxs/contact/ContactGroup.hpp"
#include "opentxs/contact/ContactItem.hpp"
#include "opentxs/core/crypto/OTPassword.hpp"
#include "opentxs/core/Account.hpp"
#include "opentxs/core/Cheque.hpp"
#include "opentxs/core/Flag.hpp"
#include "opentxs/core/Identifier.hpp"
#include "opentxs/core/Ledger.hpp"
#include "opentxs/core/Lockable.hpp"
#include "opentxs/core/Log.hpp"
#include "opentxs/core/Message.hpp"
#include "opentxs/core/String.hpp"
#include "opentxs/core/UniqueQueue.hpp"
#include "opentxs/ext/OTPayment.hpp"
#include "opentxs/network/zeromq/Context.hpp"
#include "opentxs/network/zeromq/Frame.hpp"
#include "opentxs/network/zeromq/FrameIterator.hpp"
#include "opentxs/network/zeromq/FrameSection.hpp"
#include "opentxs/network/zeromq/ListenCallback.hpp"
#include "opentxs/network/zeromq/Message.hpp"
#include "opentxs/network/zeromq/PublishSocket.hpp"
#include "opentxs/network/zeromq/SubscribeSocket.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <map>
#include <thread>
#include <tuple>

#include "Sync.hpp"

#define CONTACT_REFRESH_DAYS 1
#define CONTRACT_DOWNLOAD_SECONDS 10
#define MAIN_LOOP_SECONDS 5
#define NYM_REGISTRATION_SECONDS 10

#define SHUTDOWN()                                                             \
    {                                                                          \
        YIELD(50);                                                             \
    }

#define YIELD(a)                                                               \
    {                                                                          \
        if (!running_) { return; }                                             \
                                                                               \
        Log::Sleep(std::chrono::milliseconds(a));                              \
    }

#define CHECK_NYM(a)                                                           \
    {                                                                          \
        if (a.empty()) {                                                       \
            otErr << OT_METHOD << __FUNCTION__ << ": Invalid " << #a           \
                  << std::endl;                                                \
                                                                               \
            return Identifier::Factory();                                      \
        }                                                                      \
    }

#define CHECK_SERVER(a, b)                                                     \
    {                                                                          \
        CHECK_NYM(a)                                                           \
                                                                               \
        if (b.empty()) {                                                       \
            otErr << OT_METHOD << __FUNCTION__ << ": Invalid " << #b           \
                  << std::endl;                                                \
                                                                               \
            return Identifier::Factory();                                      \
        }                                                                      \
    }

#define CHECK_ARGS(a, b, c)                                                    \
    {                                                                          \
        CHECK_SERVER(a, b)                                                     \
                                                                               \
        if (c.empty()) {                                                       \
            otErr << OT_METHOD << __FUNCTION__ << ": Invalid " << #c           \
                  << std::endl;                                                \
                                                                               \
            return Identifier::Factory();                                      \
        }                                                                      \
    }

#define INTRODUCTION_SERVER_KEY "introduction_server_id"
#define MASTER_SECTION "Master"
#define PROCESS_INBOX_RETRIES 3

#define OT_METHOD "opentxs::api::client::implementation::Sync::"

namespace opentxs
{
api::client::Sync* Factory::Sync(
    const Flag& running,
    const OT_API& otapi,
    const OTAPI_Exec& exec,
    const api::ContactManager& contacts,
    const api::Settings& config,
    const api::Api& api,
    const api::client::Wallet& wallet,
    const api::client::Workflow& workflow,
    const api::crypto::Encode& encoding,
    const api::storage::Storage& storage,
    const network::zeromq::Context& zmq,
    const ContextLockCallback& lockCallback)
{
    return new api::client::implementation::Sync(
        running,
        otapi,
        exec,
        contacts,
        config,
        api,
        wallet,
        workflow,
        encoding,
        storage,
        zmq,
        lockCallback);
}
}  // namespace opentxs

namespace opentxs::api::client::implementation
{
const std::string Sync::DEFAULT_INTRODUCTION_SERVER =
    R"(-----BEGIN OT ARMORED SERVER CONTRACT-----
Version: Open Transactions 0.99.1-113-g2b3acf5
Comment: http://opentransactions.org

CAESI290b20xcHFmREJLTmJLR3RiN0NBa0ZodFRXVFVOTHFIRzIxGiNvdHVkd3p4cWF0UHh4
bmh4VFV3RUo3am5HenE2RkhGYTRraiIMU3Rhc2ggQ3J5cHRvKr8NCAESI290dWR3enhxYXRQ
eHhuaHhUVXdFSjdqbkd6cTZGSEZhNGtqGAIoATJTCAEQAiJNCAESIQI9MywLxxKfOtai26pj
JbxKtCCPhM/DbvX08iwbW2qYqhoga6Ccvp6CABGAFj/RdWNjtg5uzIRHT5Dn+fUzdAM9SUSA
AQCIAQA6vAwIARIjb3R1ZHd6eHFhdFB4eG5oeFRVd0VKN2puR3pxNkZIRmE0a2oaI290dXdo
ZzNwb2kxTXRRdVkzR3hwYWpOaXp5bmo0NjJ4Z2RIIAIymgQIARIjb3R1d2hnM3BvaTFNdFF1
WTNHeHBhak5penluajQ2MnhnZEgYAiABKAIyI290dWR3enhxYXRQeHhuaHhUVXdFSjdqbkd6
cTZGSEZhNGtqQl0IARJTCAEQAiJNCAESIQI9MywLxxKfOtai26pjJbxKtCCPhM/DbvX08iwb
W2qYqhoga6Ccvp6CABGAFj/RdWNjtg5uzIRHT5Dn+fUzdAM9SUSAAQCIAQAaBAgBEAJKiAEI
ARACGioIARAEGAIgASogZ6MtTp4aTEDLxFfhnsGo+Esp5B4hkgjWEejNPt5J6C0aKggBEAQY
AiACKiAhqJjWf2Ugqbg6z6ps59crAx9lHwTuT6Eq4x6JmkBlGBoqCAEQBBgCIAMqII2Vps1F
C2YUMbB4kE9XsHt1jrVY6pMPV6KWc5sH3VvTem0IARIjb3R1d2hnM3BvaTFNdFF1WTNHeHBh
ak5penluajQ2MnhnZEgYASAFKkDQLsszAol/Ih56MomuBKV8zpKaw5+ry7Kse1+5nPwJlP8f
72OAgTegBlmv31K4JgLVs52EKJTBpjnV+v0pxzUOem0IARIjb3R1ZHd6eHFhdFB4eG5oeFRV
d0VKN2puR3pxNkZIRmE0a2oYAyAFKkAJZ0LTVM+XBrGbRdiZsEQSbvwqg+mqGwHD5MQ+D4h0
fPQaUrdB6Pp/HM5veox02LBKg05hVNQ64tcU+LAxK+VHQuQDCAESI290clA2dDJXY2hYMjYz
ZVpiclRuVzZyY2FCZVNQb2VqSzJnGAIgAigCMiNvdHVkd3p4cWF0UHh4bmh4VFV3RUo3am5H
enE2RkhGYTRrajonCAESI290dXdoZzNwb2kxTXRRdVkzR3hwYWpOaXp5bmo0NjJ4Z2RISogB
CAEQAhoqCAEQBBgCIAEqIDpwlCrxHNWvvtFt6k8ocB5NBo7vjkGO/mRuSOQ/j/9WGioIARAE
GAIgAiog6Dw0+AWok4dENWWc/3qhykA7NNybWecqMGs5fL8KLLYaKggBEAQYAiADKiD+s/iq
37NrYI4/xdHOYtO/ocR0YqDVz09IaDNGVEdBtnptCAESI290clA2dDJXY2hYMjYzZVpiclRu
VzZyY2FCZVNQb2VqSzJnGAEgBSpATbHtakma53Na35Be+rGvW+z1H6EtkHlljv9Mo8wfies3
in9el1Ejb4BDbGCN5ABl3lQpfedZnR+VYv2X6Y1yBnptCAESI290dXdoZzNwb2kxTXRRdVkz
R3hwYWpOaXp5bmo0NjJ4Z2RIGAEgBSpAeptEmgdqgkGUcOJCqG0MsiChEREUdDzH/hRj877u
WDIHoRHsf/k5dCOHfDct4TDszasVhGFhRdNunpgQJcp0DULnAwgBEiNvdHd6ZWd1dTY3cENI
RnZhYjZyS2JYaEpXelNvdlNDTGl5URgCIAIoAjIjb3R1ZHd6eHFhdFB4eG5oeFRVd0VKN2pu
R3pxNkZIRmE0a2o6JwgBEiNvdHV3aGczcG9pMU10UXVZM0d4cGFqTml6eW5qNDYyeGdkSEqL
AQgBEAIaKwgBEAMYAiABKiEC5p36Ivxs4Wb6CjKTnDA1MFtX3Mx2UBlrmloSt+ffXz0aKwgB
EAMYAiACKiECtMkEo4xsefeevzrBb62ll98VYZy8PipgbrPWqGUNxQMaKwgBEAMYAiADKiED
W1j2DzOZemB9OOZ/pPrFroKDfgILYu2IOtiRFfi0vDB6bQgBEiNvdHd6ZWd1dTY3cENIRnZh
YjZyS2JYaEpXelNvdlNDTGl5URgBIAUqQJYd860/Ybh13GtW+grxWtWjjmzPifHE7bTlgUWl
3bX+ZuWNeEotA4yXQvFNog4PTAOF6dbvCr++BPGepBEUEEx6bQgBEiNvdHV3aGczcG9pMU10
UXVZM0d4cGFqTml6eW5qNDYyeGdkSBgBIAUqQH6GXnKCCDDgDvcSt8dLWuVMlr75zVkHy85t
tccoy2oLHNevDvKrLfUk/zuICyaSIvDy0Kb2ytOuh/O17yabxQ8yHQgBEAEYASISb3Quc3Rh
c2hjcnlwdG8ubmV0KK03MiEIARADGAEiFnQ1NGxreTJxM2w1ZGt3bnQub25pb24orTcyRwgB
EAQYASI8b3ZpcDZrNWVycXMzYm52cjU2cmgzZm5pZ2JuZjJrZWd1cm5tNWZpYnE1NWtqenNv
YW54YS5iMzIuaTJwKK03Op8BTWVzc2FnaW5nLW9ubHkgc2VydmVyIHByb3ZpZGVkIGZvciB0
aGUgY29udmllbmllbmNlIG9mIFN0YXNoIENyeXB0byB1c2Vycy4gU2VydmljZSBpcyBwcm92
aWRlZCBhcyBpcyB3aXRob3V0IHdhcnJhbnR5IG9mIGFueSBraW5kLCBlaXRoZXIgZXhwcmVz
c2VkIG9yIGltcGxpZWQuQiCK4L5cnecfUFz/DQyvAklKC2pTmWQtxt9olQS5/0hUHUptCAES
I290clA2dDJXY2hYMjYzZVpiclRuVzZyY2FCZVNQb2VqSzJnGAUgBSpA1/bep0NTbisZqYns
MCL/PCUJ6FIMhej+ROPk41604x1jeswkkRmXRNjzLlVdiJ/pQMxG4tJ0UQwpxHxrr0IaBA==
-----END OT ARMORED SERVER CONTRACT-----)";

Sync::Sync(
    const Flag& running,
    const OT_API& otapi,
    const opentxs::OTAPI_Exec& exec,
    const api::ContactManager& contacts,
    const api::Settings& config,
    const api::Api& api,
    const api::client::Wallet& wallet,
    const api::client::Workflow& workflow,
    const api::crypto::Encode& encoding,
    const api::storage::Storage& storage,
    const opentxs::network::zeromq::Context& zmq,
    const ContextLockCallback& lockCallback)
    : lock_callback_(lockCallback)
    , running_(running)
    , ot_api_(otapi)
    , exec_(exec)
    , contacts_(contacts)
    , config_(config)
    , api_(api)
    , server_action_(api.ServerAction())
    , wallet_(wallet)
    , workflow_(workflow)
    , encoding_(encoding)
    , storage_(storage)
    , zmq_(zmq)
    , introduction_server_lock_()
    , nym_fetch_lock_()
    , task_status_lock_()
    , refresh_counter_(0)
    , operations_()
    , server_nym_fetch_()
    , missing_nyms_()
    , missing_servers_()
    , state_machines_()
    , introduction_server_id_()
    , task_status_()
    , task_message_id_()
    , account_subscriber_callback_(
          opentxs::network::zeromq::ListenCallback::Factory(
              [this](const opentxs::network::zeromq::Message& message) -> void {
                  this->process_account(message);
              }))
    , account_subscriber_(
          zmq_.SubscribeSocket(account_subscriber_callback_.get()))
{
    const auto& endpoint =
        opentxs::network::zeromq::Socket::AccountUpdateEndpoint;
    otWarn << OT_METHOD << __FUNCTION__ << ": Connecting to " << endpoint
           << std::endl;
    const auto listening = account_subscriber_->Start(endpoint);

    OT_ASSERT(listening)
}

std::pair<bool, std::size_t> Sync::accept_incoming(
    const rLock& lock[[maybe_unused]],
    const std::size_t max,
    const Identifier& accountID,
    ServerContext& context) const
{
    std::pair<bool, std::size_t> output{false, 0};
    auto& [success, remaining] = output;
    const std::string account = accountID.str();
    auto processInbox = ot_api_.CreateProcessInbox(accountID, context);
    auto& response = std::get<0>(processInbox);
    auto& inbox = std::get<1>(processInbox);

    if (false == bool(response)) {
        if (nullptr == inbox) {
            // This is a new account which has never instantiated an inbox.
            success = true;

            return output;
        }

        otErr << OT_METHOD << __FUNCTION__
              << ": Error instantiating processInbox for account: " << account
              << std::endl;

        return output;
    }

    const std::size_t items =
        (inbox->GetTransactionCount() >= 0) ? inbox->GetTransactionCount() : 0;
    const std::size_t count = (items > max) ? max : items;
    remaining = items - count;

    if (0 == count) {
        otInfo << OT_METHOD << __FUNCTION__
               << ": No items to accept in this account." << std::endl;
        success = true;

        return output;
    }

    for (std::size_t i = 0; i < count; i++) {
        auto transaction = inbox->GetTransactionByIndex(i);

        OT_ASSERT(nullptr != transaction);

        const TransactionNumber number = transaction->GetTransactionNum();

        if (transaction->IsAbbreviated()) {
            inbox->LoadBoxReceipt(number);
            transaction = inbox->GetTransaction(number);

            if (nullptr == transaction) {
                otErr << OT_METHOD << __FUNCTION__
                      << ": Unable to load item: " << number << std::endl;

                continue;
            }
        }

        if (OTTransaction::chequeReceipt == transaction->GetType()) {
            const auto workflowUpdated =
                workflow_.ClearCheque(context.Nym()->ID(), *transaction);

            if (workflowUpdated) {
                otErr << OT_METHOD << __FUNCTION__ << ": Updated workflow."
                      << std::endl;
            } else {
                otErr << OT_METHOD << __FUNCTION__
                      << ": Failed to update workflow." << std::endl;
            }
        }

        const bool accepted = ot_api_.IncludeResponse(
            accountID, true, context, *transaction, *response);

        if (!accepted) {
            otErr << OT_METHOD << __FUNCTION__
                  << ": Failed to accept item: " << number << std::endl;

            return output;
        }
    }

    const bool finalized =
        ot_api_.FinalizeProcessInbox(accountID, context, *response, *inbox);

    if (false == finalized) {
        otErr << OT_METHOD << __FUNCTION__ << ": Unable to finalize response."
              << std::endl;

        return output;
    }

    auto action = server_action_.ProcessInbox(
        context.Nym()->ID(), context.Server(), accountID, response);
    action->Run();
    success = (SendResult::VALID_REPLY == action->LastSendResult());

    return output;
}

bool Sync::AcceptIncoming(
    const Identifier& nymID,
    const Identifier& accountID,
    const Identifier& serverID,
    const std::size_t max) const
{
    rLock apiLock(lock_callback_({nymID.str(), serverID.str()}));
    auto context = wallet_.mutable_ServerContext(nymID, serverID);
    std::size_t remaining{1};
    std::size_t retries{PROCESS_INBOX_RETRIES};

    while (0 < remaining) {
        const auto attempt =
            accept_incoming(apiLock, max, accountID, context.It());
        const auto& [success, unprocessed] = attempt;
        remaining = unprocessed;

        if (false == success) {
            if (0 == retries) {
                otErr << OT_METHOD << __FUNCTION__
                      << ": Exceeded maximum retries." << std::endl;

                return false;
            }

            Utility utility(context.It(), ot_api_);
            const auto download = utility.getIntermediaryFiles(
                context.It().Server().str(),
                context.It().Nym()->ID().str(),
                accountID.str(),
                true);

            if (false == download) {
                otErr << OT_METHOD << __FUNCTION__
                      << ": Failed to download account files." << std::endl;

                return false;
            } else {
                --retries;

                continue;
            }
        }

        if (0 != remaining) {
            otErr << OT_METHOD << __FUNCTION__ << ": Accepting " << remaining
                  << " more items." << std::endl;
        }
    }

    return true;
}

void Sync::add_task(const Identifier& taskID, const ThreadStatus status) const
{
    Lock lock(task_status_lock_);

    if (0 != task_status_.count(taskID)) { return; }

    task_status_[taskID] = status;
}

void Sync::associate_message_id(
    const Identifier& messageID,
    const Identifier& taskID) const
{
    Lock lock(task_status_lock_);
    task_message_id_.emplace(taskID, messageID);
}

Depositability Sync::can_deposit(
    const OTPayment& payment,
    const Identifier& recipient,
    const Identifier& accountIDHint,
    Identifier& depositServer,
    Identifier& depositAccount) const
{
    auto unitID = Identifier::Factory();
    auto nymID = Identifier::Factory();

    if (false == extract_payment_data(payment, nymID, depositServer, unitID)) {

        return Depositability::INVALID_INSTRUMENT;
    }

    auto output = valid_recipient(payment, nymID, recipient);

    if (Depositability::READY != output) { return output; }

    const bool registered =
        exec_.IsNym_RegisteredAtServer(recipient.str(), depositServer.str());

    if (false == registered) {
        schedule_download_nymbox(recipient, depositServer);
        otErr << OT_METHOD << __FUNCTION__ << ": Recipient nym "
              << recipient.str() << " not registered on server "
              << depositServer.str() << std::endl;

        return Depositability::NOT_REGISTERED;
    }

    output = valid_account(
        payment,
        recipient,
        depositServer,
        unitID,
        accountIDHint,
        depositAccount);

    switch (output) {
        case Depositability::ACCOUNT_NOT_SPECIFIED: {
            otErr << OT_METHOD << __FUNCTION__
                  << ": Multiple valid accounts exist. "
                  << "This payment can not be automatically deposited"
                  << std::endl;
        } break;
        case Depositability::WRONG_ACCOUNT: {
            otErr << OT_METHOD << __FUNCTION__
                  << ": The specified account is not valid for this payment."
                  << std::endl;
        } break;
        case Depositability::NO_ACCOUNT: {
            otErr << OT_METHOD << __FUNCTION__ << ": Recipient "
                  << recipient.str() << " needs an account for "
                  << unitID->str() << " on server " << depositServer.str()
                  << std::endl;
            schedule_register_account(recipient, depositServer, unitID);
        } break;
        case Depositability::READY: {
            otWarn << OT_METHOD << __FUNCTION__ << ": Payment can be deposited."
                   << std::endl;
        } break;
        default: {
            OT_FAIL
        }
    }

    return output;
}

Messagability Sync::can_message(
    const Identifier& senderNymID,
    const Identifier& recipientContactID,
    Identifier& recipientNymID,
    Identifier& serverID) const
{
    auto senderNym = wallet_.Nym(senderNymID);

    if (false == bool(senderNym)) {
        otErr << OT_METHOD << __FUNCTION__ << ": Unable to load sender nym "
              << senderNymID.str() << std::endl;

        return Messagability::MISSING_SENDER;
    }

    const bool canSign = senderNym->HasCapability(NymCapability::SIGN_MESSAGE);

    if (false == canSign) {
        otErr << OT_METHOD << __FUNCTION__ << ": Sender nym "
              << senderNymID.str() << " can not sign messages (no private key)."
              << std::endl;

        return Messagability::INVALID_SENDER;
    }

    const auto contact = contacts_.Contact(recipientContactID);

    if (false == bool(contact)) {
        otErr << OT_METHOD << __FUNCTION__ << ": Recipient contact "
              << recipientContactID.str() << " does not exist." << std::endl;

        return Messagability::MISSING_CONTACT;
    }

    const auto nyms = contact->Nyms();

    if (0 == nyms.size()) {
        otErr << OT_METHOD << __FUNCTION__ << ": Recipient contact "
              << recipientContactID.str() << " does not have a nym."
              << std::endl;

        return Messagability::CONTACT_LACKS_NYM;
    }

    std::shared_ptr<const Nym> recipientNym{nullptr};

    for (const auto& it : nyms) {
        recipientNym = wallet_.Nym(it);

        if (recipientNym) {
            recipientNymID = it;
            break;
        }
    }

    if (false == bool(recipientNym)) {
        for (const auto& id : nyms) {
            missing_nyms_.Push(Identifier::Random(), id);
        }

        otErr << OT_METHOD << __FUNCTION__ << ": Recipient contact "
              << recipientContactID.str() << " credentials not available."
              << std::endl;

        return Messagability::MISSING_RECIPIENT;
    }

    const auto claims = recipientNym->Claims();
    serverID = claims.PreferredOTServer();

    // TODO maybe some of the other nyms in this contact do specify a server
    if (serverID.empty()) {
        otErr << OT_METHOD << __FUNCTION__ << ": Recipient contact "
              << recipientContactID.str() << ", nym " << recipientNymID.str()
              << ": credentials do not specify a server." << std::endl;
        missing_nyms_.Push(Identifier::Random(), recipientNymID);

        return Messagability::NO_SERVER_CLAIM;
    }

    const bool registered =
        exec_.IsNym_RegisteredAtServer(senderNymID.str(), serverID.str());

    if (false == registered) {
        schedule_download_nymbox(senderNymID, serverID);
        otErr << OT_METHOD << __FUNCTION__ << ": Sender nym "
              << senderNymID.str() << " not registered on server "
              << serverID.str() << std::endl;

        return Messagability::UNREGISTERED;
    }

    return Messagability::READY;
}

Depositability Sync::CanDeposit(
    const Identifier& recipientNymID,
    const OTPayment& payment) const
{
    auto accountHint = Identifier::Factory();

    return CanDeposit(recipientNymID, accountHint, payment);
}

Depositability Sync::CanDeposit(
    const Identifier& recipientNymID,
    const Identifier& accountIDHint,
    const OTPayment& payment) const
{
    auto serverID = Identifier::Factory();
    auto accountID = Identifier::Factory();

    return can_deposit(
        payment, recipientNymID, accountIDHint, serverID, accountID);
}

Messagability Sync::CanMessage(
    const Identifier& senderNymID,
    const Identifier& recipientContactID) const
{
    if (senderNymID.empty()) {
        otErr << OT_METHOD << __FUNCTION__ << ": Invalid sender" << std::endl;

        return Messagability::INVALID_SENDER;
    }

    if (recipientContactID.empty()) {
        otErr << OT_METHOD << __FUNCTION__ << ": Invalid recipient"
              << std::endl;

        return Messagability::MISSING_CONTACT;
    }

    auto nymID = Identifier::Factory();
    auto serverID = Identifier::Factory();
    start_introduction_server(senderNymID);

    return can_message(senderNymID, recipientContactID, nymID, serverID);
}

void Sync::check_nym_revision(
    const ServerContext& context,
    OperationQueue& queue) const
{
    if (context.StaleNym()) {
        const auto& nymID = context.Nym()->ID();
        otErr << OT_METHOD << __FUNCTION__ << ": Nym " << nymID.str()
              << " has is newer than version last registered version on server "
              << context.Server().str() << std::endl;
        queue.register_nym_.Push(Identifier::Random(), true);
    }
}

bool Sync::check_registration(
    const Identifier& nymID,
    const Identifier& serverID,
    std::shared_ptr<const ServerContext>& context) const
{
    OT_ASSERT(false == nymID.empty())
    OT_ASSERT(false == serverID.empty())

    context = wallet_.ServerContext(nymID, serverID);
    RequestNumber request{0};

    if (context) {
        request = context->Request();
    } else {
        otErr << OT_METHOD << __FUNCTION__ << ": Nym " << nymID.str()
              << " has never registered on " << serverID.str() << std::endl;
    }

    if (0 != request) {
        OT_ASSERT(context)

        return true;
    }

    const auto output = register_nym(Identifier::Random(), nymID, serverID);

    if (output) {
        context = wallet_.ServerContext(nymID, serverID);

        OT_ASSERT(context)
    }

    return output;
}

bool Sync::check_server_contract(const Identifier& serverID) const
{
    OT_ASSERT(false == serverID.empty())

    const auto serverContract = wallet_.Server(serverID);

    if (serverContract) { return true; }

    otErr << OT_METHOD << __FUNCTION__ << ": Server contract for "
          << serverID.str() << " is not in the wallet." << std::endl;
    missing_servers_.Push(Identifier::Random(), serverID);

    return false;
}

bool Sync::deposit_cheque(
    const Identifier& taskID,
    const Identifier& nymID,
    const Identifier& serverID,
    const Identifier& accountID,
    const std::shared_ptr<const OTPayment>& payment,
    UniqueQueue<DepositPaymentTask>& retry) const
{
    OT_ASSERT(false == nymID.empty())
    OT_ASSERT(false == serverID.empty())
    OT_ASSERT(false == accountID.empty())
    OT_ASSERT(payment)

    if ((false == payment->IsCheque()) && (false == payment->IsVoucher())) {
        otErr << OT_METHOD << __FUNCTION__ << ": Unhandled payment type."
              << std::endl;

        return finish_task(taskID, false);
    }

    std::unique_ptr<Cheque> cheque = std::make_unique<Cheque>();
    const auto loaded = cheque->LoadContractFromString(payment->Payment());

    if (false == loaded) {
        otErr << OT_METHOD << __FUNCTION__ << ": Invalid cheque" << std::endl;

        return finish_task(taskID, false);
    }

    auto action =
        server_action_.DepositCheque(nymID, serverID, accountID, cheque);
    action->Run();

    if (SendResult::VALID_REPLY == action->LastSendResult()) {
        OT_ASSERT(action->Reply());

        if (action->Reply()->m_bSuccess) {

            return finish_task(taskID, true);
        } else {
            otErr << OT_METHOD << __FUNCTION__
                  << ": Failed to deposit cheque:\n"
                  << String(*cheque) << std::endl;
        }
    } else {
        otErr << OT_METHOD << __FUNCTION__
              << ": Communication error while depositing cheque "
              << " on server " << serverID.str() << std::endl;
    }

    retry.Push(taskID, {accountID, payment});

    return false;
}

std::size_t Sync::DepositCheques(const Identifier& nymID) const
{
    std::size_t output{0};
    const auto workflows = workflow_.List(
        nymID,
        proto::PAYMENTWORKFLOWTYPE_INCOMINGCHEQUE,
        proto::PAYMENTWORKFLOWSTATE_CONVEYED);

    for (const auto& id : workflows) {
        const auto chequeState = workflow_.LoadChequeByWorkflow(nymID, id);
        const auto& [state, cheque] = chequeState;

        if (proto::PAYMENTWORKFLOWSTATE_CONVEYED != state) { continue; }

        OT_ASSERT(cheque)

        if (queue_cheque_deposit(nymID, *cheque)) { ++output; }
    }

    return output;
}

std::size_t Sync::DepositCheques(
    const Identifier& nymID,
    const std::set<OTIdentifier>& chequeIDs) const
{
    std::size_t output{0};

    if (chequeIDs.empty()) { return DepositCheques(nymID); }

    for (const auto& id : chequeIDs) {
        const auto chequeState = workflow_.LoadCheque(nymID, id);
        const auto& [state, cheque] = chequeState;

        if (proto::PAYMENTWORKFLOWSTATE_CONVEYED != state) { continue; }

        OT_ASSERT(cheque)

        if (queue_cheque_deposit(nymID, *cheque)) { ++output; }
    }

    return {};
}

OTIdentifier Sync::DepositPayment(
    const Identifier& recipientNymID,
    const std::shared_ptr<const OTPayment>& payment) const
{
    auto notUsed = Identifier::Factory();

    return DepositPayment(recipientNymID, notUsed, payment);
}

OTIdentifier Sync::DepositPayment(
    const Identifier& recipientNymID,
    const Identifier& accountIDHint,
    const std::shared_ptr<const OTPayment>& payment) const
{
    OT_ASSERT(payment)

    if (recipientNymID.empty()) {
        otErr << OT_METHOD << __FUNCTION__ << ": Invalid recipient"
              << std::endl;

        return Identifier::Factory();
    }

    auto serverID = Identifier::Factory();
    auto accountID = Identifier::Factory();
    const auto status = can_deposit(
        *payment, recipientNymID, accountIDHint, serverID, accountID);

    switch (status) {
        case Depositability::READY:
        case Depositability::NOT_REGISTERED:
        case Depositability::NO_ACCOUNT: {
            start_introduction_server(recipientNymID);
            auto& queue = get_operations({recipientNymID, serverID});
            const auto taskID(Identifier::Random());

            return start_task(
                taskID,
                queue.deposit_payment_.Push(taskID, {accountIDHint, payment}));
        } break;
        default: {
            otErr << OT_METHOD << __FUNCTION__
                  << ": Unable to queue payment for download" << std::endl;
        }
    }

    return Identifier::Factory();
}

bool Sync::download_account(
    const Identifier& taskID,
    const Identifier& nymID,
    const Identifier& serverID,
    const Identifier& accountID) const
{
    OT_ASSERT(false == nymID.empty())
    OT_ASSERT(false == serverID.empty())
    OT_ASSERT(false == accountID.empty())

    const auto success =
        server_action_.DownloadAccount(nymID, serverID, accountID, false);

    return finish_task(taskID, success);
}

bool Sync::download_contract(
    const Identifier& taskID,
    const Identifier& nymID,
    const Identifier& serverID,
    const Identifier& contractID) const
{
    OT_ASSERT(false == nymID.empty())
    OT_ASSERT(false == serverID.empty())
    OT_ASSERT(false == contractID.empty())

    auto action = server_action_.DownloadContract(nymID, serverID, contractID);
    action->Run();

    if (SendResult::VALID_REPLY == action->LastSendResult()) {
        OT_ASSERT(action->Reply());

        if (action->Reply()->m_bSuccess) {
            api_.Pair().Update();

            return finish_task(taskID, true);
        } else {
            otErr << OT_METHOD << __FUNCTION__ << ": Server " << serverID.str()
                  << " does not have the contract " << contractID.str()
                  << std::endl;
        }
    } else {
        otErr << OT_METHOD << __FUNCTION__
              << ": Communication error while downloading contract "
              << contractID.str() << " from server " << serverID.str()
              << std::endl;
    }

    return finish_task(taskID, false);
}

bool Sync::download_nym(
    const Identifier& taskID,
    const Identifier& nymID,
    const Identifier& serverID,
    const Identifier& targetNymID) const
{
    OT_ASSERT(false == nymID.empty())
    OT_ASSERT(false == serverID.empty())
    OT_ASSERT(false == targetNymID.empty())

    auto action = server_action_.DownloadNym(nymID, serverID, targetNymID);
    action->Run();

    if (SendResult::VALID_REPLY == action->LastSendResult()) {
        OT_ASSERT(action->Reply());

        if (action->Reply()->m_bSuccess) {
            api_.Pair().Update();

            return finish_task(taskID, true);
        } else {
            otErr << OT_METHOD << __FUNCTION__ << ": Server " << serverID.str()
                  << " does not have nym " << targetNymID.str() << std::endl;
        }
    } else {
        otErr << OT_METHOD << __FUNCTION__
              << ": Communication error while downloading nym "
              << targetNymID.str() << " from server " << serverID.str()
              << std::endl;
    }

    return finish_task(taskID, false);
}

bool Sync::download_nymbox(
    const Identifier& taskID,
    const Identifier& nymID,
    const Identifier& serverID) const
{
    OT_ASSERT(false == nymID.empty())
    OT_ASSERT(false == serverID.empty())

    const auto success = server_action_.DownloadNymbox(nymID, serverID);

    return finish_task(taskID, success);
}

bool Sync::extract_payment_data(
    const OTPayment& payment,
    Identifier& nymID,
    Identifier& serverID,
    Identifier& unitID) const
{
    if (false == payment.GetRecipientNymID(nymID)) {
        otErr << OT_METHOD << __FUNCTION__
              << ": Unable to load recipient nym from instrument" << std::endl;

        return false;
    }

    if (false == payment.GetNotaryID(serverID)) {
        otErr << OT_METHOD << __FUNCTION__
              << ": Unable to load recipient nym from instrument" << std::endl;

        return false;
    }

    OT_ASSERT(false == serverID.empty())

    if (false == payment.GetInstrumentDefinitionID(unitID)) {
        otErr << OT_METHOD << __FUNCTION__
              << ": Unable to load recipient nym from instrument" << std::endl;

        return false;
    }

    OT_ASSERT(false == unitID.empty())

    return true;
}

bool Sync::find_nym(
    const Identifier& nymID,
    const Identifier& serverID,
    const Identifier& targetID) const
{
    OT_ASSERT(false == nymID.empty())
    OT_ASSERT(false == serverID.empty())
    OT_ASSERT(false == targetID.empty())

    const auto nym = wallet_.Nym(targetID);

    if (nym) {
        missing_nyms_.CancelByValue(targetID);

        return true;
    }

    if (download_nym(Identifier::Factory(), nymID, serverID, targetID)) {
        missing_nyms_.CancelByValue(targetID);

        return true;
    }

    return false;
}

bool Sync::find_server(
    const Identifier& nymID,
    const Identifier& serverID,
    const Identifier& targetID) const
{
    OT_ASSERT(false == nymID.empty())
    OT_ASSERT(false == serverID.empty())
    OT_ASSERT(false == targetID.empty())

    const auto serverContract = wallet_.Server(targetID);

    if (serverContract) {
        missing_servers_.CancelByValue(targetID);

        return true;
    }

    if (download_contract(Identifier::Factory(), nymID, serverID, targetID)) {
        missing_servers_.CancelByValue(targetID);

        return true;
    }

    return false;
}

OTIdentifier Sync::FindNym(const Identifier& nymID) const
{
    CHECK_NYM(nymID)

    const auto taskID(Identifier::Random());

    return start_task(taskID, missing_nyms_.Push(taskID, nymID));
}

OTIdentifier Sync::FindNym(
    const Identifier& nymID,
    const Identifier& serverIDHint) const
{
    CHECK_NYM(nymID)

    auto& serverQueue = get_nym_fetch(serverIDHint);
    const auto taskID(Identifier::Random());

    return start_task(taskID, serverQueue.Push(taskID, nymID));
}

OTIdentifier Sync::FindServer(const Identifier& serverID) const
{
    CHECK_NYM(serverID)

    const auto taskID(Identifier::Random());

    return start_task(taskID, missing_servers_.Push(taskID, serverID));
}

bool Sync::finish_task(const Identifier& taskID, const bool success) const
{
    if (success) {
        update_task(taskID, ThreadStatus::FINISHED_SUCCESS);
    } else {
        update_task(taskID, ThreadStatus::FINISHED_FAILED);
    }

    return success;
}

bool Sync::get_admin(
    const Identifier& nymID,
    const Identifier& serverID,
    const OTPassword& password) const
{
    OT_ASSERT(false == nymID.empty())
    OT_ASSERT(false == serverID.empty())

    bool success{false};

    {
        const std::string serverPassword(password.getPassword());
        auto action =
            server_action_.RequestAdmin(nymID, serverID, serverPassword);
        action->Run();

        if (SendResult::VALID_REPLY == action->LastSendResult()) {
            auto reply = action->Reply();

            OT_ASSERT(reply)

            success = reply->m_bSuccess;
        }
    }

    auto mContext = wallet_.mutable_ServerContext(nymID, serverID);
    auto& context = mContext.It();
    context.SetAdminAttempted();

    if (success) {
        otErr << OT_METHOD << __FUNCTION__ << ": Got admin on server "
              << serverID.str() << std::endl;
        context.SetAdminSuccess();
    }

    return success;
}

Identifier Sync::get_introduction_server(const Lock& lock) const
{
    OT_ASSERT(verify_lock(lock, introduction_server_lock_))

    bool keyFound = false;
    String serverID;
    const bool config = config_.Check_str(
        MASTER_SECTION, INTRODUCTION_SERVER_KEY, serverID, keyFound);

    if (!config || !keyFound || !serverID.Exists()) {

        return import_default_introduction_server(lock);
    }

    return Identifier::Factory(serverID);
}

UniqueQueue<Identifier>& Sync::get_nym_fetch(const Identifier& serverID) const
{
    Lock lock(nym_fetch_lock_);

    return server_nym_fetch_[serverID];
}

Sync::OperationQueue& Sync::get_operations(const ContextID& id) const
{
    Lock lock(lock_);
    auto& queue = operations_[id];
    auto& thread = state_machines_[id];

    if (false == bool(thread)) {
        thread.reset(new std::thread(
            [id, &queue, this]() { state_machine(id, queue); }));
    }

    return queue;
}

OTIdentifier Sync::import_default_introduction_server(const Lock& lock) const
{
    OT_ASSERT(verify_lock(lock, introduction_server_lock_))

    const auto serialized = proto::StringToProto<proto::ServerContract>(
        DEFAULT_INTRODUCTION_SERVER.c_str());
    const auto instantiated = wallet_.Server(serialized);

    OT_ASSERT(instantiated)

    return set_introduction_server(lock, *instantiated);
}

const Identifier& Sync::IntroductionServer() const
{
    Lock lock(introduction_server_lock_);

    if (false == bool(introduction_server_id_)) {
        load_introduction_server(lock);
    }

    OT_ASSERT(introduction_server_id_)

    return *introduction_server_id_;
}

void Sync::load_introduction_server(const Lock& lock) const
{
    OT_ASSERT(verify_lock(lock, introduction_server_lock_))

    introduction_server_id_.reset(
        new Identifier(get_introduction_server(lock)));
}

bool Sync::message_nym(
    const Identifier& taskID,
    const Identifier& nymID,
    const Identifier& serverID,
    const Identifier& targetNymID,
    const std::string& text) const
{
    OT_ASSERT(false == nymID.empty())
    OT_ASSERT(false == serverID.empty())
    OT_ASSERT(false == targetNymID.empty())

    auto action =
        server_action_.SendMessage(nymID, serverID, targetNymID, text);
    action->Run();

    if (SendResult::VALID_REPLY == action->LastSendResult()) {
        OT_ASSERT(action->Reply());

        if (action->Reply()->m_bSuccess) {
            const auto messageID = action->MessageID();

            if (false == messageID.empty()) {
                otInfo << OT_METHOD << __FUNCTION__ << ": Sent message  "
                       << messageID.str() << std::endl;
                associate_message_id(messageID, taskID);
            }

            return finish_task(taskID, true);
        } else {
            otErr << OT_METHOD << __FUNCTION__ << ": Server  " << serverID.str()
                  << " does not accept message for " << targetNymID.str()
                  << std::endl;
        }
    } else {
        otErr << OT_METHOD << __FUNCTION__
              << ": Communication error while messaging nym "
              << targetNymID.str() << " on server " << serverID.str()
              << std::endl;
    }

    return finish_task(taskID, false);
}

bool Sync::pay_nym(
    const Identifier& taskID,
    const Identifier& nymID,
    const Identifier& serverID,
    const Identifier& targetNymID,
    std::shared_ptr<const OTPayment>& payment) const
{
    OT_ASSERT(false == nymID.empty())
    OT_ASSERT(false == serverID.empty())
    OT_ASSERT(false == targetNymID.empty())

    auto action =
        server_action_.SendPayment(nymID, serverID, targetNymID, payment);
    action->Run();

    if (SendResult::VALID_REPLY == action->LastSendResult()) {
        OT_ASSERT(action->Reply());

        if (action->Reply()->m_bSuccess) {
            const auto messageID = action->MessageID();

            if (false == messageID.empty()) {
                otInfo << OT_METHOD << __FUNCTION__
                       << ": Sent (payment) "
                          "message "
                       << messageID.str() << std::endl;
            }

            return finish_task(taskID, true);
        } else {
            otErr << OT_METHOD << __FUNCTION__ << ": Server  " << serverID.str()
                  << " does not accept (payment) message "
                     "for "
                  << targetNymID.str() << std::endl;
        }
    } else {
        otErr << OT_METHOD << __FUNCTION__
              << ": Communication error while messaging (a payment) to nym "
              << targetNymID.str() << " on server " << serverID.str()
              << std::endl;
    }

    return finish_task(taskID, false);
}

#if OT_CASH
bool Sync::pay_nym_cash(
    const Identifier& taskID,
    const Identifier& nymID,
    const Identifier& serverID,
    const Identifier& targetNymID,
    std::shared_ptr<const Purse>& recipientCopy,
    std::shared_ptr<const Purse>& senderCopy) const
{
    OT_ASSERT(false == nymID.empty())
    OT_ASSERT(false == serverID.empty())
    OT_ASSERT(false == targetNymID.empty())

    auto action = server_action_.SendCash(
        nymID, serverID, targetNymID, recipientCopy, senderCopy);
    action->Run();

    if (SendResult::VALID_REPLY == action->LastSendResult()) {
        OT_ASSERT(action->Reply());

        if (action->Reply()->m_bSuccess) {
            const auto messageID = action->MessageID();

            if (false == messageID.empty()) {
                otInfo << OT_METHOD << __FUNCTION__ << ": Sent (cash) message  "
                       << messageID.str() << std::endl;
            }

            return finish_task(taskID, true);
        } else {
            otErr << OT_METHOD << __FUNCTION__ << ": Server  " << serverID.str()
                  << " does not accept (cash) message for " << targetNymID.str()
                  << std::endl;
        }
    } else {
        otErr << OT_METHOD << __FUNCTION__
              << ": Communication error while messaging (cash) to nym "
              << targetNymID.str() << " on server " << serverID.str()
              << std::endl;
    }

    return finish_task(taskID, false);
}
#endif  // OT_CASH

OTIdentifier Sync::MessageContact(
    const Identifier& senderNymID,
    const Identifier& contactID,
    const std::string& message) const
{
    CHECK_SERVER(senderNymID, contactID)

    start_introduction_server(senderNymID);
    auto serverID = Identifier::Factory();
    auto recipientNymID = Identifier::Factory();
    const auto canMessage =
        can_message(senderNymID, contactID, recipientNymID, serverID);

    if (Messagability::READY != canMessage) { return Identifier::Factory(); }

    OT_ASSERT(false == serverID->empty())
    OT_ASSERT(false == recipientNymID->empty())

    auto& queue = get_operations({senderNymID, serverID});
    const auto taskID(Identifier::Random());

    return start_task(
        taskID, queue.send_message_.Push(taskID, {recipientNymID, message}));
}

std::pair<ThreadStatus, OTIdentifier> Sync::MessageStatus(
    const Identifier& taskID) const
{
    std::pair<ThreadStatus, OTIdentifier> output{{},
                                                 Identifier::Factory(taskID)};
    auto& [threadStatus, messageID] = output;
    Lock lock(task_status_lock_);
    threadStatus = status(lock, taskID);

    if (threadStatus == ThreadStatus::FINISHED_SUCCESS) {
        auto it = task_message_id_.find(taskID);

        if (task_message_id_.end() != it) {
            messageID = it->second;
            task_message_id_.erase(it);
        }
    }

    return output;
}

OTIdentifier Sync::PayContact(
    const Identifier& senderNymID,
    const Identifier& contactID,
    std::shared_ptr<const OTPayment>& payment) const
{
    CHECK_SERVER(senderNymID, contactID)

    start_introduction_server(senderNymID);
    auto serverID = Identifier::Factory();
    auto recipientNymID = Identifier::Factory();
    const auto canMessage =
        can_message(senderNymID, contactID, recipientNymID, serverID);

    if (Messagability::READY != canMessage) { return Identifier::Factory(); }

    OT_ASSERT(false == serverID->empty())
    OT_ASSERT(false == recipientNymID->empty())

    auto& queue = get_operations({senderNymID, serverID});
    const auto taskID(Identifier::Random());

    return start_task(
        taskID,
        queue.send_payment_.Push(
            taskID,
            {recipientNymID, std::shared_ptr<const OTPayment>(payment)}));
}

#if OT_CASH
OTIdentifier Sync::PayContactCash(
    const Identifier& senderNymID,
    const Identifier& contactID,
    std::shared_ptr<const Purse>& recipientCopy,
    std::shared_ptr<const Purse>& senderCopy) const
{
    CHECK_SERVER(senderNymID, contactID)

    start_introduction_server(senderNymID);
    auto serverID = Identifier::Factory();
    auto recipientNymID = Identifier::Factory();
    const auto canMessage =
        can_message(senderNymID, contactID, recipientNymID, serverID);

    if (Messagability::READY != canMessage) { return Identifier::Factory(); }

    OT_ASSERT(false == serverID->empty())
    OT_ASSERT(false == recipientNymID->empty())

    auto& queue = get_operations({senderNymID, serverID});
    const auto taskID(Identifier::Random());

    return start_task(
        taskID,
        queue.send_cash_.Push(
            taskID,
            {recipientNymID,
             std::shared_ptr<const Purse>(recipientCopy),
             std::shared_ptr<const Purse>(senderCopy)}));
}
#endif  // OT_CASH

void Sync::process_account(
    const opentxs::network::zeromq::Message& message) const
{
    OT_ASSERT(2 == message.Body().size())

    const std::string id(*message.Body().begin());
    const auto& balance = message.Body().at(1);

    OT_ASSERT(balance.size() == sizeof(Amount))

    otInfo << OT_METHOD << __FUNCTION__ << ": Account " << id << " balance: "
           << std::to_string(*static_cast<const Amount*>(balance.data()))
           << std::endl;
}

bool Sync::publish_server_contract(
    const Identifier& taskID,
    const Identifier& nymID,
    const Identifier& serverID,
    const Identifier& contractID) const
{
    OT_ASSERT(false == nymID.empty())
    OT_ASSERT(false == serverID.empty())
    OT_ASSERT(false == contractID.empty())

    auto action =
        server_action_.PublishServerContract(nymID, serverID, contractID);
    action->Run();

    if (SendResult::VALID_REPLY == action->LastSendResult()) {
        OT_ASSERT(action->Reply());

        if (action->Reply()->m_bSuccess) {

            return finish_task(taskID, true);
        } else {
            otErr << OT_METHOD << __FUNCTION__
                  << ": Failed to publish server contract " << contractID.str()
                  << " on server " << serverID.str() << std::endl;
        }
    } else {
        otErr << OT_METHOD << __FUNCTION__
              << ": Communication error while uploading contract "
              << contractID.str() << " to server " << serverID.str()
              << std::endl;
    }

    return finish_task(taskID, false);
}

bool Sync::publish_server_registration(
    const Identifier& nymID,
    const Identifier& serverID,
    const bool forcePrimary) const
{
    OT_ASSERT(false == nymID.empty())
    OT_ASSERT(false == serverID.empty())

    auto nym = wallet_.mutable_Nym(nymID);

    return nym.AddPreferredOTServer(serverID.str(), forcePrimary);
}

bool Sync::queue_cheque_deposit(const Identifier& nymID, const Cheque& cheque)
    const
{
    auto payment = std::make_shared<OTPayment>(String(cheque));

    OT_ASSERT(payment)

    payment->SetTempValuesFromCheque(cheque);

    if (cheque.GetRecipientNymID().empty()) {
        payment->SetTempRecipientNymID(nymID);
    }

    const auto taskID = DepositPayment(nymID, payment);

    return (false == taskID->empty());
}

void Sync::Refresh() const
{
    api_.Pair().Update();
    refresh_accounts();

    SHUTDOWN()

    refresh_contacts();
    ++refresh_counter_;
}

std::uint64_t Sync::RefreshCount() const { return refresh_counter_.load(); }

void Sync::refresh_accounts() const
{
    otInfo << OT_METHOD << __FUNCTION__ << ": Begin" << std::endl;
    const auto serverList = wallet_.ServerList();
    const auto accounts = storage_.AccountList();

    for (const auto server : serverList) {
        SHUTDOWN()

        const auto serverID = Identifier::Factory(server.first);
        otWarn << OT_METHOD << __FUNCTION__ << ": Considering server "
               << serverID->str() << std::endl;

        for (const auto& nymID : ot_api_.LocalNymList()) {
            SHUTDOWN()
            otWarn << OT_METHOD << __FUNCTION__ << ": Nym " << nymID->str()
                   << " ";
            const bool registered =
                ot_api_.IsNym_RegisteredAtServer(nymID, serverID);

            if (registered) {
                otWarn << "is ";
                auto& queue = get_operations({nymID, serverID});
                const auto taskID(Identifier::Random());
                queue.download_nymbox_.Push(taskID, true);
            } else {
                otWarn << "is not ";
            }

            otWarn << "registered here." << std::endl;
        }
    }

    SHUTDOWN()

    for (const auto& it : accounts) {
        SHUTDOWN()
        const auto accountID = Identifier::Factory(it.first);
        const auto nymID = storage_.AccountOwner(accountID);
        const auto serverID = storage_.AccountServer(accountID);
        otWarn << OT_METHOD << __FUNCTION__ << ": Account " << accountID->str()
               << ":\n"
               << "  * Owned by nym: " << nymID->str() << "\n"
               << "  * On server: " << serverID->str() << std::endl;
        auto& queue = get_operations({nymID, serverID});
        const auto taskID(Identifier::Random());
        queue.download_account_.Push(taskID, accountID);
    }

    otInfo << OT_METHOD << __FUNCTION__ << ": End" << std::endl;
}

void Sync::refresh_contacts() const
{
    for (const auto& it : contacts_.ContactList()) {
        SHUTDOWN()

        const auto& contactID = it.first;
        otInfo << OT_METHOD << __FUNCTION__
               << ": Considering contact: " << contactID << std::endl;
        const auto contact = contacts_.Contact(Identifier::Factory(contactID));

        OT_ASSERT(contact);

        const auto now = std::time(nullptr);
        const std::chrono::seconds interval(now - contact->LastUpdated());
        const std::chrono::hours limit(24 * CONTACT_REFRESH_DAYS);
        const auto nymList = contact->Nyms();

        if (nymList.empty()) {
            otInfo << OT_METHOD << __FUNCTION__
                   << ": No nyms associated with this contact." << std::endl;

            continue;
        }

        for (const auto& nymID : nymList) {
            SHUTDOWN()

            const auto nym = wallet_.Nym(nymID);
            otInfo << OT_METHOD << __FUNCTION__
                   << ": Considering nym: " << nymID->str() << std::endl;

            if (nym) {
                contacts_.Update(nym->asPublicNym());
            } else {
                otInfo << OT_METHOD << __FUNCTION__
                       << ": We don't have credentials for this nym. "
                       << " Will search on all servers." << std::endl;
                const auto taskID(Identifier::Random());
                missing_nyms_.Push(taskID, nymID);

                continue;
            }

            if (interval > limit) {
                otInfo << OT_METHOD << __FUNCTION__
                       << ": Hours since last update (" << interval.count()
                       << ") exceeds the limit (" << limit.count() << ")"
                       << std::endl;
                // TODO add a method to Contact that returns the list of
                // servers
                const auto data = contact->Data();

                if (false == bool(data)) { continue; }

                const auto serverGroup = data->Group(
                    proto::CONTACTSECTION_COMMUNICATION,
                    proto::CITEMTYPE_OPENTXS);

                if (false == bool(serverGroup)) {

                    const auto taskID(Identifier::Random());
                    missing_nyms_.Push(taskID, nymID);
                    continue;
                }

                for (const auto& [claimID, item] : *serverGroup) {
                    SHUTDOWN()
                    OT_ASSERT(item)

                    const auto& notUsed[[maybe_unused]] = claimID;
                    const auto serverID = Identifier::Factory(item->Value());

                    if (serverID->empty()) { continue; }

                    otInfo << OT_METHOD << __FUNCTION__
                           << ": Will download nym " << nymID->str()
                           << " from server " << serverID->str() << std::endl;
                    auto& serverQueue = get_nym_fetch(serverID);
                    const auto taskID(Identifier::Random());
                    serverQueue.Push(taskID, nymID);
                }
            } else {
                otInfo << OT_METHOD << __FUNCTION__
                       << ": No need to update this nym." << std::endl;
            }
        }
    }
}

bool Sync::register_account(
    const Identifier& taskID,
    const Identifier& nymID,
    const Identifier& serverID,
    const Identifier& unitID) const
{
    OT_ASSERT(false == nymID.empty())
    OT_ASSERT(false == serverID.empty())
    OT_ASSERT(false == unitID.empty())

    auto action = server_action_.RegisterAccount(nymID, serverID, unitID);
    action->Run();

    if (SendResult::VALID_REPLY == action->LastSendResult()) {
        OT_ASSERT(action->Reply());

        if (action->Reply()->m_bSuccess) {
            api_.Pair().Update();

            return finish_task(taskID, true);
        } else {
            otErr << OT_METHOD << __FUNCTION__
                  << ": Failed to register account for " << unitID.str()
                  << " on server " << serverID.str() << std::endl;
        }
    } else {
        otErr << OT_METHOD << __FUNCTION__
              << ": Communication error while registering account "
              << " on server " << serverID.str() << std::endl;
    }

    return finish_task(taskID, false);
}

bool Sync::register_nym(
    const Identifier& taskID,
    const Identifier& nymID,
    const Identifier& serverID) const
{
    OT_ASSERT(false == nymID.empty())
    OT_ASSERT(false == serverID.empty())

    set_contact(nymID, serverID);
    auto action = server_action_.RegisterNym(nymID, serverID);
    action->Run();

    if (SendResult::VALID_REPLY == action->LastSendResult()) {
        OT_ASSERT(action->Reply());

        if (action->Reply()->m_bSuccess) {
            api_.Pair().Update();

            return finish_task(taskID, true);
        } else {
            otErr << OT_METHOD << __FUNCTION__ << ": Server " << serverID.str()
                  << " did not accept registration for nym " << nymID.str()
                  << std::endl;
        }
    } else {
        otErr << OT_METHOD << __FUNCTION__
              << ": Communication error while registering nym " << nymID.str()
              << " on server " << serverID.str() << std::endl;
    }

    return finish_task(taskID, false);
}

OTIdentifier Sync::RegisterNym(
    const Identifier& nymID,
    const Identifier& serverID,
    const bool setContactData) const
{
    CHECK_SERVER(nymID, serverID)

    start_introduction_server(nymID);

    if (setContactData) { publish_server_registration(nymID, serverID, false); }

    return ScheduleRegisterNym(nymID, serverID);
}

OTIdentifier Sync::SetIntroductionServer(const ServerContract& contract) const
{
    Lock lock(introduction_server_lock_);

    return set_introduction_server(lock, contract);
}

OTIdentifier Sync::schedule_download_nymbox(
    const Identifier& localNymID,
    const Identifier& serverID) const
{
    CHECK_SERVER(localNymID, serverID)

    start_introduction_server(localNymID);
    auto& queue = get_operations({localNymID, serverID});
    const auto taskID(Identifier::Random());

    return start_task(taskID, queue.download_nymbox_.Push(taskID, true));
}

OTIdentifier Sync::schedule_register_account(
    const Identifier& localNymID,
    const Identifier& serverID,
    const Identifier& unitID) const
{
    CHECK_ARGS(localNymID, serverID, unitID)

    start_introduction_server(localNymID);
    auto& queue = get_operations({localNymID, serverID});
    const auto taskID(Identifier::Random());

    return start_task(taskID, queue.register_account_.Push(taskID, unitID));
}

OTIdentifier Sync::ScheduleDownloadAccount(
    const Identifier& localNymID,
    const Identifier& serverID,
    const Identifier& accountID) const
{
    CHECK_ARGS(localNymID, serverID, accountID)

    start_introduction_server(localNymID);
    auto& queue = get_operations({localNymID, serverID});
    const auto taskID(Identifier::Random());

    return start_task(taskID, queue.download_account_.Push(taskID, accountID));
}

OTIdentifier Sync::ScheduleDownloadContract(
    const Identifier& localNymID,
    const Identifier& serverID,
    const Identifier& contractID) const
{
    CHECK_ARGS(localNymID, serverID, contractID)

    start_introduction_server(localNymID);
    auto& queue = get_operations({localNymID, serverID});
    const auto taskID(Identifier::Random());

    return start_task(
        taskID, queue.download_contract_.Push(taskID, contractID));
}

OTIdentifier Sync::ScheduleDownloadNym(
    const Identifier& localNymID,
    const Identifier& serverID,
    const Identifier& targetNymID) const
{
    CHECK_ARGS(localNymID, serverID, targetNymID)

    start_introduction_server(localNymID);
    auto& queue = get_operations({localNymID, serverID});
    const auto taskID(Identifier::Random());

    return start_task(taskID, queue.check_nym_.Push(taskID, targetNymID));
}

OTIdentifier Sync::ScheduleDownloadNymbox(
    const Identifier& localNymID,
    const Identifier& serverID) const
{
    return schedule_download_nymbox(localNymID, serverID);
}

OTIdentifier Sync::SchedulePublishServerContract(
    const Identifier& localNymID,
    const Identifier& serverID,
    const Identifier& contractID) const
{
    CHECK_ARGS(localNymID, serverID, contractID)

    start_introduction_server(localNymID);
    auto& queue = get_operations({localNymID, serverID});
    const auto taskID(Identifier::Random());

    return start_task(
        taskID, queue.publish_server_contract_.Push(taskID, contractID));
}

OTIdentifier Sync::ScheduleRegisterAccount(
    const Identifier& localNymID,
    const Identifier& serverID,
    const Identifier& unitID) const
{
    return schedule_register_account(localNymID, serverID, unitID);
}

OTIdentifier Sync::ScheduleRegisterNym(
    const Identifier& localNymID,
    const Identifier& serverID) const
{
    CHECK_SERVER(localNymID, serverID)

    start_introduction_server(localNymID);
    auto& queue = get_operations({localNymID, serverID});
    const auto taskID(Identifier::Random());

    return start_task(taskID, queue.register_nym_.Push(taskID, true));
}

bool Sync::send_transfer(
    const Identifier& taskID,
    const Identifier& localNymID,
    const Identifier& serverID,
    const Identifier& sourceAccountID,
    const Identifier& targetAccountID,
    const std::int64_t value,
    const std::string& memo) const
{
    auto action = server_action_.SendTransfer(
        localNymID, serverID, sourceAccountID, targetAccountID, value, memo);
    action->Run();

    if (SendResult::VALID_REPLY == action->LastSendResult()) {
        OT_ASSERT(action->Reply());

        if (action->Reply()->m_bSuccess) {
            return finish_task(taskID, true);
        } else {
            otErr << OT_METHOD << __FUNCTION__ << ": Failed to send transfer "
                  << "to " << serverID.str() << " for account "
                  << targetAccountID.str() << std::endl;
        }
    } else {
        otErr << OT_METHOD << __FUNCTION__
              << ": Communication error while sending transfer to account "
              << targetAccountID.str() << " on server " << serverID.str()
              << std::endl;
    }

    return finish_task(taskID, false);
}

OTIdentifier Sync::SendTransfer(
    const Identifier& localNymID,
    const Identifier& serverID,
    const Identifier& sourceAccountID,
    const Identifier& targetAccountID,
    const std::int64_t value,
    const std::string& memo) const
{
    CHECK_ARGS(localNymID, serverID, targetAccountID)
    CHECK_NYM(sourceAccountID)

    auto sourceAccount = wallet_.Account(sourceAccountID);

    if (false == bool(sourceAccount)) {
        otErr << OT_METHOD << __FUNCTION__ << ": Invalid source account"
              << std::endl;

        return Identifier::Factory();
    }

    auto targetAccount = wallet_.Account(targetAccountID);

    if (false == bool(targetAccount)) {
        otErr << OT_METHOD << __FUNCTION__ << ": Invalid target account"
              << std::endl;

        return Identifier::Factory();
    }

    if (sourceAccount.get().GetNymID() != targetAccount.get().GetNymID()) {
        otErr << OT_METHOD << __FUNCTION__ << ": Source and target account"
              << " owner ids don't match" << std::endl;

        return Identifier::Factory();
    }

    if (sourceAccount.get().GetRealNotaryID() !=
        targetAccount.get().GetRealNotaryID()) {
        otErr << OT_METHOD << __FUNCTION__ << ": Source and target account"
              << " notary ids don't match" << std::endl;

        return Identifier::Factory();
    }

    if (sourceAccount.get().GetInstrumentDefinitionID() !=
        targetAccount.get().GetInstrumentDefinitionID()) {
        otErr << OT_METHOD << __FUNCTION__ << ": Source and target account"
              << " instrument definition ids don't match" << std::endl;

        return Identifier::Factory();
    }

    auto& queue = get_operations({localNymID, serverID});
    const auto taskID(Identifier::Random());

    return start_task(
        taskID,
        queue.send_transfer_.Push(
            taskID, {sourceAccountID, targetAccountID, value, memo}));
}

void Sync::set_contact(const Identifier& nymID, const Identifier& serverID)
    const
{
    auto nym = wallet_.mutable_Nym(nymID);
    const auto server = nym.PreferredOTServer();

    if (server.empty()) { nym.AddPreferredOTServer(serverID.str(), true); }
}

OTIdentifier Sync::set_introduction_server(
    const Lock& lock,
    const ServerContract& contract) const
{
    OT_ASSERT(verify_lock(lock, introduction_server_lock_));

    auto instantiated = wallet_.Server(contract.PublicContract());

    if (false == bool(instantiated)) { return Identifier::Factory(); }

    const auto& id = instantiated->ID();
    introduction_server_id_.reset(new Identifier(id));

    OT_ASSERT(introduction_server_id_)

    bool dontCare = false;
    const bool set = config_.Set_str(
        MASTER_SECTION, INTRODUCTION_SERVER_KEY, String(id), dontCare);

    OT_ASSERT(set)

    config_.Save();

    return id;
}

void Sync::start_introduction_server(const Identifier& nymID) const
{
    auto& serverID = IntroductionServer();

    if (serverID.empty()) { return; }

    auto& queue = get_operations({nymID, serverID});
    const auto taskID(Identifier::Random());
    start_task(taskID, queue.download_nymbox_.Push(taskID, true));
}

OTIdentifier Sync::start_task(const Identifier& taskID, bool success) const
{
    if (taskID.empty()) { return Identifier::Factory(); }

    if (false == success) { return Identifier::Factory(); }

    add_task(taskID, ThreadStatus::RUNNING);

    return taskID;
}

void Sync::StartIntroductionServer(const Identifier& localNymID) const
{
    start_introduction_server(localNymID);
}

void Sync::state_machine(const ContextID id, OperationQueue& queue) const
{
    const auto& [nymID, serverID] = id;

    // Make sure the server contract is available
    while (running_) {
        if (check_server_contract(serverID)) {
            otInfo << OT_METHOD << __FUNCTION__ << ": Server contract "
                   << serverID.str() << " exists." << std::endl;

            break;
        }

        YIELD(CONTRACT_DOWNLOAD_SECONDS);
    }

    SHUTDOWN()

    std::shared_ptr<const ServerContext> context{nullptr};

    // Make sure the nym has registered for the first time on the server
    while (running_) {
        if (check_registration(nymID, serverID, context)) {
            otInfo << OT_METHOD << __FUNCTION__ << ": Nym " << nymID.str()
                   << " has registered on server " << serverID.str()
                   << " at least once." << std::endl;

            break;
        }

        YIELD(NYM_REGISTRATION_SECONDS);
    }

    SHUTDOWN()
    OT_ASSERT(context)

    bool queueValue{false};
    bool needAdmin{false};
    bool registerNym{false};
    bool registerNymQueued{false};
    bool downloadNymbox{false};
    auto taskID = Identifier::Factory();
    auto accountID = Identifier::Factory();
    auto unitID = Identifier::Factory();
    auto contractID = Identifier::Factory();
    auto targetNymID = Identifier::Factory();
    auto nullID = Identifier::Factory();
    OTPassword serverPassword;
    MessageTask message;
    PaymentTask payment;
#if OT_CASH
    PayCashTask cash_payment;
#endif  // OT_CASH
    DepositPaymentTask deposit;
    UniqueQueue<DepositPaymentTask> depositPaymentRetry;
    SendTransferTask transfer;

    // Primary loop
    while (running_) {
        SHUTDOWN()

        // If the local nym has updated since the last registernym operation,
        // schedule a registernym
        check_nym_revision(*context, queue);

        SHUTDOWN()

        // Register the nym, if scheduled. Keep trying until success
        registerNymQueued = queue.register_nym_.Pop(taskID, queueValue);
        registerNym |= queueValue;

        if (registerNymQueued || registerNym) {
            if (register_nym(taskID, nymID, serverID)) {
                registerNym = false;
                queueValue = false;
            } else {
                registerNym = true;
            }
        }

        SHUTDOWN()

        // If this server was added by a pairing operation that included
        // a server password then request admin permissions on the server
        needAdmin =
            context->HaveAdminPassword() && (false == context->isAdmin());

        if (needAdmin) {
            serverPassword.setPassword(context->AdminPassword());
            get_admin(nymID, serverID, serverPassword);
        }

        SHUTDOWN()

        // This is a list of servers for which we do not have a contract.
        // We ask all known servers on which we are registered to try to find
        // the contracts.
        const auto servers = missing_servers_.Copy();

        for (const auto& [targetID, taskID] : servers) {
            SHUTDOWN()

            if (targetID.empty()) {
                otErr << OT_METHOD << __FUNCTION__
                      << ": How did an empty serverID get in here?"
                      << std::endl;

                continue;
            } else {
                otWarn << OT_METHOD << __FUNCTION__
                       << ": Searching for server contract for "
                       << targetID.str() << std::endl;
            }

            const auto& notUsed[[maybe_unused]] = taskID;
            find_server(nymID, serverID, targetID);
        }

        // This is a list of contracts (server and unit definition) which a
        // user of this class has requested we download from this server.
        while (queue.download_contract_.Pop(taskID, contractID)) {
            SHUTDOWN()

            if (contractID->empty()) {
                otErr << OT_METHOD << __FUNCTION__
                      << ": How did an empty contract ID get in here?"
                      << std::endl;

                continue;
            } else {
                otWarn << OT_METHOD << __FUNCTION__
                       << ": Searching for unit definition contract for "
                       << contractID->str() << std::endl;
            }

            download_contract(taskID, nymID, serverID, contractID);
        }

        // This is a list of nyms for which we do not have credentials..
        // We ask all known servers on which we are registered to try to find
        // their credentials.
        const auto nyms = missing_nyms_.Copy();

        for (const auto& [targetID, taskID] : nyms) {
            SHUTDOWN()

            if (targetID.empty()) {
                otErr << OT_METHOD << __FUNCTION__
                      << ": How did an empty nymID get in here?" << std::endl;

                continue;
            } else {
                otWarn << OT_METHOD << __FUNCTION__ << ": Searching for nym "
                       << targetID.str() << std::endl;
            }

            const auto& notUsed[[maybe_unused]] = taskID;
            find_nym(nymID, serverID, targetID);
        }

        // This is a list of nyms which haven't been updated in a while and
        // are known or suspected to be available on this server
        auto& nymQueue = get_nym_fetch(serverID);

        while (nymQueue.Pop(taskID, targetNymID)) {
            SHUTDOWN()

            if (targetNymID->empty()) {
                otErr << OT_METHOD << __FUNCTION__
                      << ": How did an empty nymID get in here?" << std::endl;

                continue;
            } else {
                otWarn << OT_METHOD << __FUNCTION__ << ": Refreshing nym "
                       << targetNymID->str() << std::endl;
            }

            download_nym(taskID, nymID, serverID, targetNymID);
        }

        // This is a list of nyms which a user of this class has requested we
        // download from this server.
        while (queue.check_nym_.Pop(taskID, targetNymID)) {
            SHUTDOWN()

            if (targetNymID->empty()) {
                otErr << OT_METHOD << __FUNCTION__
                      << ": How did an empty nymID get in here?" << std::endl;

                continue;
            } else {
                otWarn << OT_METHOD << __FUNCTION__ << ": Searching for nym "
                       << targetNymID->str() << std::endl;
            }

            download_nym(taskID, nymID, serverID, targetNymID);
        }

        // This is a list of messages which need to be delivered to a nym
        // on this server
        while (queue.send_message_.Pop(taskID, message)) {
            SHUTDOWN()

            const auto& [recipientID, text] = message;

            if (recipientID.empty()) {
                otErr << OT_METHOD << __FUNCTION__
                      << ": How did an empty recipient nymID get in here?"
                      << std::endl;

                continue;
            }

            message_nym(taskID, nymID, serverID, recipientID, text);
        }

        // This is a list of payments which need to be delivered to a nym
        // on this server
        while (queue.send_payment_.Pop(taskID, payment)) {
            SHUTDOWN()

            auto& [recipientID, pPayment] = payment;

            if (recipientID.empty()) {
                otErr << OT_METHOD << __FUNCTION__
                      << ": How did an empty recipient nymID get in here?"
                      << std::endl;

                continue;
            }

            pay_nym(taskID, nymID, serverID, recipientID, pPayment);
        }

#if OT_CASH
        // This is a list of cash payments which need to be delivered to a nym
        // on this server
        while (queue.send_cash_.Pop(taskID, cash_payment)) {
            SHUTDOWN()

            auto& [recipientID, pRecipientPurse, pSenderPurse] = cash_payment;

            if (recipientID.empty()) {
                otErr << OT_METHOD << __FUNCTION__
                      << ": How did an empty recipient nymID get in here?"
                      << std::endl;

                continue;
            }

            pay_nym_cash(
                taskID,
                nymID,
                serverID,
                recipientID,
                pRecipientPurse,
                pSenderPurse);
        }
#endif

        // Download the nymbox, if this operation has been scheduled
        if (queue.download_nymbox_.Pop(taskID, downloadNymbox)) {
            otWarn << OT_METHOD << __FUNCTION__ << ": Downloading nymbox for "
                   << nymID.str() << " on " << serverID.str() << std::endl;
            registerNym |= !download_nymbox(taskID, nymID, serverID);
        }

        SHUTDOWN()

        // Download any accounts which have been scheduled for download
        while (queue.download_account_.Pop(taskID, accountID)) {
            SHUTDOWN()

            if (accountID->empty()) {
                otErr << OT_METHOD << __FUNCTION__
                      << ": How did an empty account ID get in here?"
                      << std::endl;

                continue;
            } else {
                otWarn << OT_METHOD << __FUNCTION__ << ": Downloading account "
                       << accountID->str() << " for " << nymID.str() << " on "
                       << serverID.str() << std::endl;
            }

            registerNym |=
                !download_account(taskID, nymID, serverID, accountID);
        }

        SHUTDOWN()

        // Register any accounts which have been scheduled for creation
        while (queue.register_account_.Pop(taskID, unitID)) {
            SHUTDOWN()

            if (unitID->empty()) {
                otErr << OT_METHOD << __FUNCTION__
                      << ": How did an empty unit ID get in here?" << std::endl;

                continue;
            } else {
                otWarn << OT_METHOD << __FUNCTION__ << ": Creating account for "
                       << unitID->str() << " on " << serverID.str()
                       << std::endl;
            }

            registerNym |= !register_account(taskID, nymID, serverID, unitID);
        }

        SHUTDOWN()

        // Deposit any queued payments
        while (queue.deposit_payment_.Pop(taskID, deposit)) {
            auto& [accountIDHint, payment] = deposit;

            SHUTDOWN()
            OT_ASSERT(payment)

            const auto status =
                can_deposit(*payment, nymID, accountIDHint, nullID, accountID);

            switch (status) {
                case Depositability::READY: {
                    registerNym |= !deposit_cheque(
                        taskID,
                        nymID,
                        serverID,
                        accountID,
                        payment,
                        depositPaymentRetry);
                } break;
                case Depositability::NOT_REGISTERED:
                case Depositability::NO_ACCOUNT: {
                    otWarn << OT_METHOD << __FUNCTION__
                           << ": Temporary failure trying to deposit payment"
                           << std::endl;
                    depositPaymentRetry.Push(taskID, deposit);
                } break;
                default: {
                    otErr << OT_METHOD << __FUNCTION__
                          << ": Permanent failure trying to deposit payment"
                          << std::endl;
                }
            }
        }

        // Requeue all payments which will be retried
        while (depositPaymentRetry.Pop(taskID, deposit)) {
            SHUTDOWN()

            queue.deposit_payment_.Push(taskID, deposit);
        }

        SHUTDOWN()

        // This is a list of transfers which need to be delivered to a nym
        // on this server
        while (queue.send_transfer_.Pop(taskID, transfer)) {
            SHUTDOWN()

            const auto& [sourceAccountID, targetAccountID, value, memo] =
                transfer;

            send_transfer(
                taskID,
                nymID,
                serverID,
                sourceAccountID,
                targetAccountID,
                value,
                memo);
        }

        while (queue.publish_server_contract_.Pop(taskID, contractID)) {
            SHUTDOWN()

            if (contractID->empty()) {
                otErr << OT_METHOD << __FUNCTION__
                      << ": How did an empty contract ID get in here?"
                      << std::endl;

                continue;
            } else {
                otWarn << OT_METHOD << __FUNCTION__
                       << ": Uploading server contract " << contractID->str()
                       << std::endl;
            }

            publish_server_contract(taskID, nymID, serverID, contractID);
        }

        YIELD(MAIN_LOOP_SECONDS);
    }
}

ThreadStatus Sync::status(const Lock& lock, const Identifier& taskID) const
{
    OT_ASSERT(verify_lock(lock, task_status_lock_))

    if (!running_) { return ThreadStatus::SHUTDOWN; }

    auto it = task_status_.find(taskID);

    if (task_status_.end() == it) { return ThreadStatus::ERROR; }

    const auto output = it->second;
    const bool success = (ThreadStatus::FINISHED_SUCCESS == output);
    const bool failed = (ThreadStatus::FINISHED_FAILED == output);
    const bool finished = (success || failed);

    if (finished) { task_status_.erase(it); }

    return output;
}

ThreadStatus Sync::Status(const Identifier& taskID) const
{
    Lock lock(task_status_lock_);

    return status(lock, taskID);
}

void Sync::update_task(const Identifier& taskID, const ThreadStatus status)
    const
{
    if (taskID.empty()) { return; }

    Lock lock(task_status_lock_);

    if (0 == task_status_.count(taskID)) { return; }

    task_status_[taskID] = status;
}

Depositability Sync::valid_account(
    const OTPayment& payment,
    const Identifier& recipient,
    const Identifier& paymentServerID,
    const Identifier& paymentUnitID,
    const Identifier& accountIDHint,
    Identifier& depositAccount) const
{
    std::set<Identifier> matchingAccounts{};

    for (const auto& it : storage_.AccountList()) {
        const auto accountID = Identifier::Factory(it.first);
        const auto nymID = storage_.AccountOwner(accountID);
        const auto serverID = storage_.AccountServer(accountID);
        const auto unitID = storage_.AccountContract(accountID);

        if (nymID != recipient) { continue; }

        if (serverID != paymentServerID) { continue; }

        if (unitID != paymentUnitID) { continue; }

        matchingAccounts.emplace(accountID);
    }

    if (accountIDHint.empty()) {
        if (0 == matchingAccounts.size()) {

            return Depositability::NO_ACCOUNT;
        } else if (1 == matchingAccounts.size()) {
            depositAccount = *matchingAccounts.begin();

            return Depositability::READY;
        } else {

            return Depositability::ACCOUNT_NOT_SPECIFIED;
        }
    }

    if (0 == matchingAccounts.size()) {

        return Depositability::NO_ACCOUNT;
    } else if (1 == matchingAccounts.count(accountIDHint)) {
        depositAccount = accountIDHint;

        return Depositability::READY;
    } else {

        return Depositability::WRONG_ACCOUNT;
    }
}

Depositability Sync::valid_recipient(
    const OTPayment& payment,
    const Identifier& specified,
    const Identifier& recipient) const
{
    if (specified.empty()) {
        otErr << OT_METHOD << __FUNCTION__
              << ": Payment can be accepted by any nym" << std::endl;

        return Depositability::READY;
    }

    if (recipient == specified) { return Depositability::READY; }

    return Depositability::WRONG_RECIPIENT;
}

Sync::~Sync()
{
    for (auto& [id, thread] : state_machines_) {
        const auto& notUsed[[maybe_unused]] = id;

        OT_ASSERT(thread)

        if (thread->joinable()) { thread->join(); }
    }
}
}  // namespace opentxs::api::client::implementation
