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

#ifndef OPENTXS_CORE_CRYPTO_OTCRYPTO_HPP
#define OPENTXS_CORE_CRYPTO_OTCRYPTO_HPP

#include "opentxs/Forward.hpp"

#include <mutex>

namespace opentxs
{

class Settings;

class CryptoConfig
{
private:
    static bool GetSetAll();

    static bool GetSetValue(
        std::string strKeyName,
        std::int32_t nDefaultValue,
        const std::int32_t*& out_nValue);

    static const std::int32_t& GetValue(const std::int32_t*& pValue);

    static const std::int32_t* sp_nIterationCount;
    static const std::int32_t* sp_nSymmetricSaltSize;
    static const std::int32_t* sp_nSymmetricKeySize;
    static const std::int32_t* sp_nSymmetricKeySizeMax;
    static const std::int32_t* sp_nSymmetricIvSize;
    static const std::int32_t* sp_nSymmetricBufferSize;
    static const std::int32_t* sp_nPublicKeysize;
    static const std::int32_t* sp_nPublicKeysizeMax;

public:
    EXPORT static std::uint32_t IterationCount();
    EXPORT static std::uint32_t SymmetricSaltSize();
    EXPORT static std::uint32_t SymmetricKeySize();
    EXPORT static std::uint32_t SymmetricKeySizeMax();
    EXPORT static std::uint32_t SymmetricIvSize();
    EXPORT static std::uint32_t SymmetricBufferSize();
    EXPORT static std::uint32_t PublicKeysize();
    EXPORT static std::uint32_t PublicKeysizeMax();
};

class Crypto
{
protected:
    Crypto() = default;

    virtual void Init_Override() const = 0;
    virtual void Cleanup_Override() const = 0;

public:
    virtual ~Crypto() = default;
    void Init() const;
    void Cleanup() const;
};

}  // namespace opentxs

#endif  // OPENTXS_CORE_CRYPTO_OTCRYPTO_HPP
