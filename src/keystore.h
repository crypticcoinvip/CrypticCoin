// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KEYSTORE_H
#define BITCOIN_KEYSTORE_H

#include "key.h"
#include "pubkey.h"
#include "script/script.h"
#include "script/standard.h"
#include "sync.h"
#include "crypticcoin/Address.hpp"
#include "crypticcoin/NoteEncryption.hpp"

#include <boost/signals2/signal.hpp>
#include <boost/variant.hpp>

/** A virtual base class for key stores */
class CKeyStore
{
protected:
    mutable CCriticalSection cs_KeyStore;
    mutable CCriticalSection cs_SpendingKeyStore;

public:
    virtual ~CKeyStore() {}

    //! Add a key to the store.
    virtual bool AddKeyPubKey(const CKey &key, const CPubKey &pubkey) =0;
    virtual bool AddKey(const CKey &key);

    //! Check whether a key corresponding to a given address is present in the store.
    virtual bool HaveKey(const CKeyID &address) const =0;
    virtual bool GetKey(const CKeyID &address, CKey& keyOut) const =0;
    virtual void GetKeys(std::set<CKeyID> &setAddress) const =0;
    virtual bool GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const;

    //! Support for BIP 0013 : see https://github.com/bitcoin/bips/blob/master/bip-0013.mediawiki
    virtual bool AddCScript(const CScript& redeemScript) =0;
    virtual bool HaveCScript(const CScriptID &hash) const =0;
    virtual bool GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const =0;

    //! Support for Watch-only addresses
    virtual bool AddWatchOnly(const CScript &dest) =0;
    virtual bool RemoveWatchOnly(const CScript &dest) =0;
    virtual bool HaveWatchOnly(const CScript &dest) const =0;
    virtual bool HaveWatchOnly() const =0;

    //! Add a spending key to the store.
    virtual bool AddSpendingKey(const libcrypticcoin::SpendingKey &sk) =0;

    //! Check whether a spending key corresponding to a given payment address is present in the store.
    virtual bool HaveSpendingKey(const libcrypticcoin::PaymentAddress &address) const =0;
    virtual bool GetSpendingKey(const libcrypticcoin::PaymentAddress &address, libcrypticcoin::SpendingKey& skOut) const =0;
    virtual void GetPaymentAddresses(std::set<libcrypticcoin::PaymentAddress> &setAddress) const =0;

    //! Support for viewing keys
    virtual bool AddViewingKey(const libcrypticcoin::ViewingKey &vk) =0;
    virtual bool RemoveViewingKey(const libcrypticcoin::ViewingKey &vk) =0;
    virtual bool HaveViewingKey(const libcrypticcoin::PaymentAddress &address) const =0;
    virtual bool GetViewingKey(const libcrypticcoin::PaymentAddress &address, libcrypticcoin::ViewingKey& vkOut) const =0;
};

typedef std::map<CKeyID, CKey> KeyMap;
typedef std::map<CScriptID, CScript > ScriptMap;
typedef std::set<CScript> WatchOnlySet;
typedef std::map<libcrypticcoin::PaymentAddress, libcrypticcoin::SpendingKey> SpendingKeyMap;
typedef std::map<libcrypticcoin::PaymentAddress, libcrypticcoin::ViewingKey> ViewingKeyMap;
typedef std::map<libcrypticcoin::PaymentAddress, ZCNoteDecryption> NoteDecryptorMap;

/** Basic key store, that keeps keys in an address->secret map */
class CBasicKeyStore : public CKeyStore
{
protected:
    KeyMap mapKeys;
    ScriptMap mapScripts;
    WatchOnlySet setWatchOnly;
    SpendingKeyMap mapSpendingKeys;
    ViewingKeyMap mapViewingKeys;
    NoteDecryptorMap mapNoteDecryptors;

public:
    bool AddKeyPubKey(const CKey& key, const CPubKey &pubkey);
    bool HaveKey(const CKeyID &address) const
    {
        bool result;
        {
            LOCK(cs_KeyStore);
            result = (mapKeys.count(address) > 0);
        }
        return result;
    }
    void GetKeys(std::set<CKeyID> &setAddress) const
    {
        setAddress.clear();
        {
            LOCK(cs_KeyStore);
            KeyMap::const_iterator mi = mapKeys.begin();
            while (mi != mapKeys.end())
            {
                setAddress.insert((*mi).first);
                mi++;
            }
        }
    }
    bool GetKey(const CKeyID &address, CKey &keyOut) const
    {
        {
            LOCK(cs_KeyStore);
            KeyMap::const_iterator mi = mapKeys.find(address);
            if (mi != mapKeys.end())
            {
                keyOut = mi->second;
                return true;
            }
        }
        return false;
    }
    virtual bool AddCScript(const CScript& redeemScript);
    virtual bool HaveCScript(const CScriptID &hash) const;
    virtual bool GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const;

    virtual bool AddWatchOnly(const CScript &dest);
    virtual bool RemoveWatchOnly(const CScript &dest);
    virtual bool HaveWatchOnly(const CScript &dest) const;
    virtual bool HaveWatchOnly() const;

    bool AddSpendingKey(const libcrypticcoin::SpendingKey &sk);
    bool HaveSpendingKey(const libcrypticcoin::PaymentAddress &address) const
    {
        bool result;
        {
            LOCK(cs_SpendingKeyStore);
            result = (mapSpendingKeys.count(address) > 0);
        }
        return result;
    }
    bool GetSpendingKey(const libcrypticcoin::PaymentAddress &address, libcrypticcoin::SpendingKey &skOut) const
    {
        {
            LOCK(cs_SpendingKeyStore);
            SpendingKeyMap::const_iterator mi = mapSpendingKeys.find(address);
            if (mi != mapSpendingKeys.end())
            {
                skOut = mi->second;
                return true;
            }
        }
        return false;
    }
    bool GetNoteDecryptor(const libcrypticcoin::PaymentAddress &address, ZCNoteDecryption &decOut) const
    {
        {
            LOCK(cs_SpendingKeyStore);
            NoteDecryptorMap::const_iterator mi = mapNoteDecryptors.find(address);
            if (mi != mapNoteDecryptors.end())
            {
                decOut = mi->second;
                return true;
            }
        }
        return false;
    }
    void GetPaymentAddresses(std::set<libcrypticcoin::PaymentAddress> &setAddress) const
    {
        setAddress.clear();
        {
            LOCK(cs_SpendingKeyStore);
            SpendingKeyMap::const_iterator mi = mapSpendingKeys.begin();
            while (mi != mapSpendingKeys.end())
            {
                setAddress.insert((*mi).first);
                mi++;
            }
            ViewingKeyMap::const_iterator mvi = mapViewingKeys.begin();
            while (mvi != mapViewingKeys.end())
            {
                setAddress.insert((*mvi).first);
                mvi++;
            }
        }
    }

    virtual bool AddViewingKey(const libcrypticcoin::ViewingKey &vk);
    virtual bool RemoveViewingKey(const libcrypticcoin::ViewingKey &vk);
    virtual bool HaveViewingKey(const libcrypticcoin::PaymentAddress &address) const;
    virtual bool GetViewingKey(const libcrypticcoin::PaymentAddress &address, libcrypticcoin::ViewingKey& vkOut) const;
};

typedef std::vector<unsigned char, secure_allocator<unsigned char> > CKeyingMaterial;
typedef std::map<CKeyID, std::pair<CPubKey, std::vector<unsigned char> > > CryptedKeyMap;
typedef std::map<libcrypticcoin::PaymentAddress, std::vector<unsigned char> > CryptedSpendingKeyMap;

#endif // BITCOIN_KEYSTORE_H
