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

#ifndef OPENTXS_UI_CONTACT_SUBSECTION_BLANK_IMPLEMENTATION_HPP
#define OPENTXS_UI_CONTACT_SUBSECTION_BLANK_IMPLEMENTATION_HPP

#include "Internal.hpp"

#include "opentxs/ui/ContactSubsection.hpp"
#include "opentxs/ui/Widget.hpp"

namespace opentxs::ui::implementation
{
class ContactSubsectionBlank : virtual public ui::ContactSubsection,
                               virtual public opentxs::ui::Widget
{
public:
    std::string Name(const std::string& lang) const override { return {}; }
    OTUIContactItem First() const override
    {
        const std::shared_ptr<const ui::ContactItem> empty;

        return OTUIContactItem{empty};
    }
    bool Last() const override { return true; }
    OTUIContactItem Next() const override
    {
        const std::shared_ptr<const ui::ContactItem> empty;

        return OTUIContactItem{empty};
    }
    proto::ContactItemType Type() const override { return {}; }
    bool Valid() const override { return false; }
    OTIdentifier WidgetID() const override { return Identifier::Factory(); }

    void Update(const opentxs::ContactGroup& group) override {}

    ~ContactSubsectionBlank() = default;

protected:
    ContactSubsectionBlank() = default;

private:
    friend opentxs::ui::implementation::ContactSection;

    ContactSubsectionBlank(const ContactSubsectionBlank&) = delete;
    ContactSubsectionBlank(ContactSubsectionBlank&&) = delete;
    ContactSubsectionBlank& operator=(const ContactSubsectionBlank&) = delete;
    ContactSubsectionBlank& operator=(ContactSubsectionBlank&&) = delete;
};
}  // namespace opentxs::ui::implementation
#endif  // OPENTXS_UI_CONTACT_SUBSECTION_BLANK_IMPLEMENTATION_HPP
