/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <TAO/API/types/finance.h>

#include <LLD/include/global.h>

#include <TAO/API/include/global.h>
#include <TAO/API/include/utils.h>

#include <TAO/Ledger/include/chainstate.h>
#include <TAO/Ledger/include/constants.h>
#include <TAO/Ledger/include/enum.h>

#include <TAO/Ledger/types/mempool.h>
#include <TAO/Ledger/types/sigchain.h>
#include <TAO/Ledger/types/state.h>

#include <TAO/Operation/include/enum.h>
#include <TAO/Operation/include/execute.h>

#include <TAO/Register/include/create.h>
#include <TAO/Register/include/enum.h>
#include <TAO/Register/include/names.h>
#include <TAO/Register/types/address.h>
#include <TAO/Register/types/object.h>

#include <Legacy/include/money.h>
#include <Legacy/include/trust.h>

#include <Legacy/types/address.h>
#include <Legacy/types/merkle.h>
#include <Legacy/types/script.h>
#include <Legacy/types/transaction.h>
#include <Legacy/types/trustkey.h>

#include <Legacy/wallet/wallet.h>
#include <Legacy/types/wallettx.h>

#include <vector>

/* Global TAO namespace. */
namespace TAO
{

    /* API Layer namespace. */
    namespace API
    {

        /* Local utility function to get the trust key for the current wallet.
         * If none exists or wallet is locked, returns false.
         */
        bool FindTrustKey(Legacy::Wallet& wallet, Legacy::TrustKey& trustKey)
        {
            if(wallet.IsLocked() || Legacy::fWalletUnlockMintOnly)
                return false;

            trustKey.SetNull();

            /* Attempt to use the trust key cached in the wallet */
            std::vector<uint8_t> vchTrustKey = wallet.GetTrustKey();

            if(!vchTrustKey.empty())
            {
                uint576_t cKey;
                cKey.SetBytes(vchTrustKey);

                /* Read the key cached in wallet from the Trust DB */
                if(!LLD::Trust->ReadTrustKey(cKey, trustKey))
                {
                    /* Cached wallet trust key not found in trust db, reset it (can happen if Genesis is orphaned). */
                    trustKey.SetNull();
                    wallet.RemoveTrustKey();
                }

                debug::log(2, FUNCTION, "Current wallet contains trust key for migration");
            }

            /* If wallet does not contain trust key, need to scan for it within the trust database.
             * Should only have to do this if new wallet (no trust key), or converting old wallet that doesn't have its key cached yet.
             */
            if(trustKey.IsNull())
            {
                /* Retrieve all raw trust database keys from keychain */
                std::vector<Legacy::TrustKey> vKeys;
                if(LLD::Trust->BatchRead("trust", vKeys, -1))
                {
                    /* Cutoff time for v4 trust keys. Anything prior to v4 end plus the original one timespan grace period.
                     * This addresses an issue that some v4 keys produced one v5 block during grace period, but then incorrectly
                     * "expired" and were replaced with a new v5 key.
                     */
                    const uint64_t nCutoff = TAO::Ledger::EndTimelock(4)
                                             + (uint64_t)(config::fTestNet ? TAO::Ledger::TRUST_KEY_TIMESPAN_TESTNET
                                                                           : TAO::Ledger::TRUST_KEY_TIMESPAN);

                    /* Search through the trust keys. */
                    for(const auto& trustKeyCheck : vKeys)
                    {
                        /* Check whether trust key is part of current wallet */
                        Legacy::NexusAddress address;
                        address.SetPubKey(trustKeyCheck.vchPubKey);

                        if(wallet.HaveKey(address))
                        {
                            /* Trust key is in wallet, check version of most recent block */
                            debug::log(2, FUNCTION, "Checking trustKey for migration ", address.ToString());

                            /* Read the block for hashLastBlock to check its version. We can use this, even if it has not
                             * been verified as most recent hashLastBlock, yet, because all we need is the version of the block.
                             */
                            TAO::Ledger::BlockState state;
                            if(LLD::Ledger->ReadBlock(trustKeyCheck.hashLastBlock, state))
                            {
                                /* This will ignore a v5 key if last block was before v4 cutoff, so it does not
                                 * use keys affected by grace period issue.
                                 */
                                if(state.nVersion >= 5 && state.nTime >= nCutoff)
                                {
                                    /* Set the trust key if found. */
                                    trustKey = trustKeyCheck;

                                    /* Store trust key */
                                    wallet.SetTrustKey(trustKey.vchPubKey);

                                    debug::log(2, FUNCTION, "Found Trust Key matching current wallet");
                                    break;
                                }
                            }
                        }
                    }
                }
            }


            /* When trust key found, verify data stored against actual last trust block.
             * Older versions of code did not revert trust key data after an orphan and it may be out of date.
             */
            if(!trustKey.IsNull() && !config::fShutdown.load())
            {
                TAO::Ledger::BlockState stateLast = TAO::Ledger::ChainState::stateBest.load();
                if(Legacy::GetLastTrust(trustKey, stateLast))
                {
                    uint1024_t hashLast = stateLast.GetHash();

                    if(trustKey.hashLastBlock != hashLast)
                    {
                        /* Trust key is out of date. Update it */
                        debug::log(2, FUNCTION, "Updating trust key");

                        uint64_t nBlockTime = stateLast.GetBlockTime();

                        trustKey.hashLastBlock = hashLast;
                        trustKey.nLastBlockTime = nBlockTime;
                        trustKey.nStakeRate = trustKey.StakeRate(stateLast, nBlockTime);

                        uint576_t cKey;
                        cKey.SetBytes(trustKey.vchPubKey);

                        LLD::Trust->WriteTrustKey(cKey, trustKey);
                    }
                }
                else
                {
                    debug::log(2, FUNCTION, "Last stake block not found. Unable to verify trust key.");
                    trustKey.SetNull();
                }
            }

            if (trustKey.IsNull() || config::fShutdown.load())
                return false;

            /* Validate the trust key can be migrated */
            TAO::Ledger::BlockState state;
            if(!LLD::Ledger->ReadBlock(trustKey.hashLastBlock, state))
                throw APIException(-40, "Previous transaction not found");

            if(state.vtx[0].first != TAO::Ledger::TRANSACTION::LEGACY)
                throw APIException(-184, "Previous stake transaction is not a Legacy transaction");

            uint512_t txHash = state.vtx[0].second;
            if(wallet.mapWallet.count(txHash) == 0)
                throw APIException(-186, "Previous stake does not belong to current wallet");

            Legacy::WalletTx wtx = wallet.mapWallet[txHash];

            if(!wtx.IsCoinStake())
                throw APIException(-185, "Previous transaction is not a stake transaction");

            if(wtx.GetBlocksToMaturity() > 0)
                throw APIException(-183, "Previous stake transaction immature");

            if(LLD::Legacy->HasTrustConversion(trustKey.GetHash()))
                throw APIException(-182, "Trust key previously migrated");

            return true;
        }


        /* Local utility function to get the trust account for a signature chain.
         * Can only migrate to a Trust account that has not staked Genesis, so this function always looks up the account
         * using the derived register address.
         */
        bool FindTrustAccount(const memory::encrypted_ptr<TAO::Ledger::SignatureChain>& user, TAO::Register::Address& hashAddress)
        {

            /* If trust account has already staked Genesis, it should be indexed */
            if(LLD::Register->HasTrust(user->Genesis()))
                throw APIException(-188, "Trust account already has genesis");

            /* Retrieve the trust account address from the name register mapping. */
            TAO::Register::Address hashRegister = TAO::Register::Address(std::string("trust"), user->Genesis(), TAO::Register::Address::TRUST);

            /* Retrieve the trust account */
            TAO::Register::Object reg;
            if(!LLD::Register->ReadState(hashRegister, reg))
                throw APIException(-75, "Unable to retrieve trust account");

            /* Verify we have trust account register for the user account */
            if(!reg.Parse())
                throw APIException(-71, "Unable to parse trust account");

            if(reg.Standard() != TAO::Register::OBJECTS::TRUST)
                throw APIException(-72, "Register is not a trust account");

            /* Check the account is a NXS account */
            if(reg.get<uint256_t>("token") != 0)
                throw APIException(-73, "Trust account is not a NXS account.");

            /* Validate that the trust account is new (no stake or trust) */
            if(reg.get<uint64_t>("stake") != 0)
                throw APIException(-189, "Trust account already has stake.");

            if(reg.get<uint64_t>("trust") != 0)
                throw APIException(-190, "Trust account already has trust.");

            hashAddress = hashRegister;

            return true;
        }


        /* Set the stake amount for trust account (add/remove stake). */
        json::json Finance::MigrateStake(const json::json& params, bool fHelp)
        {
            json::json ret;
            Legacy::Wallet& wallet = Legacy::Wallet::GetInstance();

            /* Get the PIN to be used for this API call */
            SecureString strPIN = users->GetPin(params, TAO::Ledger::PinUnlock::TRANSACTIONS);

            /* Get the session to be used for this API call */
            uint256_t nSession = users->GetSession(params);

            /* Get the user signature chain. */
            memory::encrypted_ptr<TAO::Ledger::SignatureChain>& user = users->GetAccount(nSession);
            if(!user)
                throw APIException(-10, "Invalid session ID");

            /* Check for walletpassphrase parameter. */
            SecureString strWalletPass;
            strWalletPass.reserve(100);

            if(params.find("walletpassphrase") != params.end())
                strWalletPass = params["walletpassphrase"].get<std::string>().c_str();

            /* Save the current lock state of wallet */
            bool fLocked = wallet.IsLocked();
            bool fMintOnly = Legacy::fWalletUnlockMintOnly;

            /* Must provide passphrase to send if wallet locked or unlocked for minting only */
            if(wallet.IsCrypted() && (fLocked || fMintOnly))
            {
                if(strWalletPass.length() == 0)
                    throw APIException(-179, "Legacy wallet is locked. walletpassphrase required");

                /* Unlock returns true if already unlocked, but passphrase must be validated for mint only so must lock first */
                if(fMintOnly)
                {
                    wallet.Lock();
                    Legacy::fWalletUnlockMintOnly = false; //Assures temporary unlock is a full unlock for send
                }

                /* Handle temporary unlock (send false for fStartStake so stake minter does not start during send)
                 * An incorrect passphrase will leave the wallet locked, even if it was previously unlocked for minting.
                 */
                if(!wallet.Unlock(strWalletPass, 0, false))
                    throw APIException(-180, "Incorrect walletpassphrase for Legacy wallet");
            }

            /* Need to save any exception after unlock so we can re-lock wallet before throwing.
             * If nException gets assigned, skip the remainder of the process.
             */
            int32_t nException = 0;
            std::string strException;

            /* Retrieve the trust key to migrate from (requires wallet unlock) */
            Legacy::TrustKey trustKey;
            try
            {
                if(!FindTrustKey(wallet, trustKey))
                {
                    nException = -181;
                    strException = "Trust key not found for Legacy wallet";
                }
            }
            catch(const APIException& e)
            {
                nException = e.id;
                strException = std::string(e.what());
            }

            /* Trust key checks complete. Now can use trust key address as input to migration transaction */
            Legacy::WalletTx wtx;
            Legacy::NexusAddress trustAddress;

            if(nException == 0)
            {
                trustAddress.SetPubKey(trustKey.vchPubKey);
                wtx.fromAddress = trustAddress;
            }

            /* Get the available addresses from the wallet with their balances */
            std::map<Legacy::NexusAddress, int64_t> mapAddresses;
            if(nException == 0 && !wallet.GetAddressBook().AvailableAddresses(wtx.nTime, mapAddresses))
            {
                nException = -187;
                strException = "Could not get addresses for Legacy wallet";
            }

            /* Migrate amount is current trust address balance */
            int64_t nAmount = 0;

            if(nException == 0)
            {
                for(const auto& entry : mapAddresses)
                {
                    if(entry.first == trustAddress)
                    {
                        /* Found address entry for trust key address */
                        nAmount = entry.second;
                        break;
                    }
                }

                if(Legacy::TRANSACTION_FEE > nAmount)
                {
                    nException = -69;
                    strException = "Insufficient funds";
                }
            }

            if(nException == 0 && nAmount < Legacy::MIN_TXOUT_AMOUNT)
            {
                nException = -68;
                strException = "Amount too small";
            }

            /* Lock the signature chain. */
            LOCK(users->CREATE_MUTEX);

            /* Get trust account. Any trust account that has completed Genesis will be indexed and not available for migrate. */
            TAO::Register::Address hashAddress;
            if(nException == 0)
            {
                try
                {
                    if(!FindTrustAccount(user, hashAddress))
                    {
                        nException = -70;
                        strException = "Trust account not found";
                    }
                }
                catch(const APIException& e)
                {
                    nException = e.id;
                    strException = std::string(e.what());
                }
            }

            /* Create the transaction. */

            /* The script to contain the recipient */
            Legacy::Script scriptPubKey;

            /* Migration transaction sends from legacy trust key address to trust account register address */
            if(nException == 0)
            {
                scriptPubKey.SetRegisterAddress(hashAddress);

                strException = wallet.SendToNexusAddress(scriptPubKey, nAmount, wtx, false, 1, true);
            }

            /* If used walletpassphrase to temporarily unlock wallet, re-lock the wallet
             * This does not return unlocked for minting state, because we are migrating from the trust key and
             * the minter should not be re-started.
             */
            if(wallet.IsCrypted() && (fLocked || fMintOnly))
                wallet.Lock();

            /* Check result of SendToNexusAddress only after returning to prior lock state */
            if(strException != "")
            {
                debug::log(0, FUNCTION, "Cannot migrate trust key: ", strException);

                if(nException != 0)
                    throw APIException(nException, strException);
                else
                    throw APIException(-3, strException);
            }
            else
            {
                debug::log(0, FUNCTION, "Initiated trust key migration from trust address ", trustAddress.ToString(),
                    "\n    to trust account address ", hashAddress.ToString(),
                    "\n    Balance sent (less fee): ", std::fixed, (nAmount / (double)TAO::Ledger::NXS_COIN));
            }


            /* Build a JSON response object. */
            ret["txid"] = wtx.GetHash().GetHex();

            return ret;
        }


        /* Migrate all Legacy wallet accounts to corresponding accounts in the signature chain */
        json::json Finance::MigrateAccounts(const json::json& params, bool fHelp)
        {
            /* Return value array */
            json::json ret = json::json::array();

            Legacy::Wallet& wallet = Legacy::Wallet::GetInstance();

            /* Get the PIN to be used for this API call */
            SecureString strPIN = users->GetPin(params, TAO::Ledger::PinUnlock::TRANSACTIONS);

            /* Get the session to be used for this API call */
            uint256_t nSession = users->GetSession(params);

            /* Get the user signature chain. */
            memory::encrypted_ptr<TAO::Ledger::SignatureChain>& user = users->GetAccount(nSession);
            if(!user)
                throw APIException(-10, "Invalid session ID");

            /* Check for walletpassphrase parameter. */
            SecureString strWalletPass;
            strWalletPass.reserve(100);

            if(params.find("walletpassphrase") != params.end())
                strWalletPass = params["walletpassphrase"].get<std::string>().c_str();

            /* Check to see if the caller has specified NOT to create a name (we do by default) */
            bool fCreateName = params.find("createname") == params.end() 
                    || (params["createname"].get<std::string>() != "1" && params["createname"].get<std::string>() != "true"); 

            /* Save the current lock state of wallet */
            bool fLocked = wallet.IsLocked();
            bool fMintOnly = Legacy::fWalletUnlockMintOnly;

            /* Must provide passphrase to send if wallet locked or unlocked for minting only */
            if(wallet.IsCrypted() && (fLocked || fMintOnly))
            {
                if(strWalletPass.length() == 0)
                    throw APIException(-179, "Legacy wallet is locked. walletpassphrase required");

                /* Unlock returns true if already unlocked, but passphrase must be validated for mint only so must lock first */
                if(fMintOnly)
                {
                    wallet.Lock();
                    Legacy::fWalletUnlockMintOnly = false; //Assures temporary unlock is a full unlock for send
                }

                /* Handle temporary unlock (send false for fStartStake so stake minter does not start during send)
                 * An incorrect passphrase will leave the wallet locked, even if it was previously unlocked for minting.
                 */
                if(!wallet.Unlock(strWalletPass, 0, false))
                    throw APIException(-180, "Incorrect walletpassphrase for Legacy wallet");
            }

            /* Get a map of all account balances from the legacy wallet */
            std::map<std::string, int64_t> mapAccountBalances;
            for(const auto& entry : Legacy::Wallet::GetInstance().GetAddressBook().GetAddressBookMap())
            {
                if(Legacy::Wallet::GetInstance().HaveKey(entry.first)) // This address belongs to me
                {
                    if(entry.second == "" || entry.second == "default")
                        mapAccountBalances["default"] = 0;
                    else
                        mapAccountBalances[entry.second] = 0;
                }
            }

            /* Get the available addresses from the wallet */
            std::map<Legacy::NexusAddress, int64_t> mapAddresses;
            if(!Legacy::Wallet::GetInstance().GetAddressBook().AvailableAddresses((uint32_t)runtime::unifiedtimestamp(), mapAddresses))
                throw APIException(-3, "Error Extracting the Addresses from Wallet File. Please Try Again.");

            /* Find all the addresses in the list */
            for(const auto& entry : mapAddresses)
            {
                if(Legacy::Wallet::GetInstance().GetAddressBook().HasAddress(entry.first))
                {
                    std::string strAccount = Legacy::Wallet::GetInstance().GetAddressBook().GetAddressBookMap().at(entry.first);
                    
                    /* Make sure to map blank legacy account to default */
                    if(strAccount == "")
                        strAccount = "default";

                    mapAccountBalances[strAccount] += entry.second;
                }
                else
                {
                    mapAccountBalances["default"] += entry.second;
                }
            }


            /* map of legacy account names to tritium account register addresses */
            std::map<std::string, TAO::Register::Address> mapAccountRegisters;

            /* Lock the signature chain. */
            LOCK(users->CREATE_MUTEX);

            /* Create the transaction. */
            TAO::Ledger::Transaction tx;
            if(!Users::CreateTransaction(user, strPIN, tx))
                throw APIException(-17, "Failed to create transaction");

            /* tracks how many contracts we have added to the current transaction */
            uint8_t nContracts = 0;

            /* Iterate the legacy accounts */
            for(const auto& accountBalance :  mapAccountBalances)
            {
                /* The name of the legacy account */
                std::string strAccount = accountBalance.first;

                /* The new account address */
                TAO::Register::Address hashAccount;

                /* First check to see if an account exists with this name */
                hashAccount = Names::ResolveAddress(params, strAccount, false);

                /* If one does not exist then check to see if one exists with a matching data field, from a previous migration */
                if(!hashAccount.IsValid())
                {
                    std::vector<TAO::Register::Address> vAccounts;
                    if(ListAccounts(user->Genesis(), vAccounts, false, false))
                    {
                        for(const auto& hashRegister : vAccounts)
                        {
                            /* Retrieve the account */
                            TAO::Register::Object object;
                            if(!LLD::Register->ReadState(hashRegister, object, TAO::Ledger::FLAGS::MEMPOOL))
                                throw TAO::API::APIException(-13, "Account not found");
                            
                            /* Parse the object register. */
                            if(!object.Parse())
                                throw TAO::API::APIException(-14, "Object failed to parse");

                            /* Check to see if it is a NXS account the data matches the account name */
                            if(object.get<uint256_t>("token") == 0 
                                && object.CheckName("data") && object.get<std::string>("data") == strAccount)
                            {
                                hashAccount = hashRegister;
                                break;
                            }
                        }
                    }    
                }

                /* If we still haven't found an account then create a new one */
                if(!hashAccount.IsValid())
                {
                    /* Make sure we have enough room in the current TX for this account and name. If not then submit this transaction
                       and create a new one.  NOTE we add a maximum of 99 to leave room for the fee  */
                    if(nContracts +(fCreateName ? 1 : 2) > 99)
                    {
                        /* Add the fee */
                        AddFee(tx);

                        /* Execute the operations layer. */
                        if(!tx.Build())
                            throw APIException(-44, "Transaction failed to build");

                        /* Sign the transaction. */
                        if(!tx.Sign(users->GetKey(tx.nSequence, strPIN, nSession)))
                            throw APIException(-31, "Ledger failed to sign transaction");

                        /* Execute the operations layer. */
                        if(!TAO::Ledger::mempool.Accept(tx))
                            throw APIException(-32, "Failed to accept");

                        /* Create the next transaction and reset the counter */
                        if(!Users::CreateTransaction(user, strPIN, tx))
                            throw APIException(-17, "Failed to create transaction");
                        
                        nContracts = 0;
                    }

                    /* Generate a random hash for this objects register address */
                    hashAccount = TAO::Register::Address(TAO::Register::Address::ACCOUNT);

                    /* Create an account object register for NXS (identifier 0). */
                    TAO::Register::Object account = TAO::Register::CreateAccount(0);

                    /* Store the legacy account name in the data field of the account register */
                    account << std::string("data") << uint8_t(TAO::Register::TYPES::STRING) << strAccount;

                    /* Submit the payload object. */
                    tx[nContracts++] << uint8_t(TAO::Operation::OP::CREATE) << hashAccount << uint8_t(TAO::Register::REGISTER::OBJECT) << account.GetState();

                    /* If user has not explicitly indicated not to create a name then create a Name Object register for it. */
                    if(fCreateName)
                        tx[nContracts++] = Names::CreateName(user->Genesis(), strAccount, "", hashAccount);
                }

                /* Add this to the map */
                mapAccountRegisters[strAccount] = hashAccount;
            }

            /* If there are accounts to create then submit the transaction */
            if(nContracts > 0)
            {
                /* Add the fee */
                AddFee(tx);

                /* Execute the operations layer. */
                if(!tx.Build())
                    throw APIException(-44, "Transaction failed to build");

                /* Sign the transaction. */
                if(!tx.Sign(users->GetKey(tx.nSequence, strPIN, nSession)))
                    throw APIException(-31, "Ledger failed to sign transaction");

                /* Execute the operations layer. */
                if(!TAO::Ledger::mempool.Accept(tx))
                    throw APIException(-32, "Failed to accept");
            }

            /* Once the accounts have been created transfer the balance from the legacy account to the new ones */
            for(const auto& accountBalance :  mapAccountBalances)
            {
                /* Check that there is enough balance to send */
                if(accountBalance.second <= Legacy::TRANSACTION_FEE)
                    continue;

                /* The account to send from */
                std::string strAccount = accountBalance.first;

                /* The account address to send to */
                TAO::Register::Address hashAccount = mapAccountRegisters[strAccount];

                /* The amount to send */
                int64_t nAmount = accountBalance.second;

                /* The script to contain the recipient */
                Legacy::Script scriptPubKey;
                scriptPubKey.SetRegisterAddress(hashAccount);

                /* Legacy wallet transaction  */
                Legacy::WalletTx wtx;

                /* Set the from account */
                wtx.strFromAccount = strAccount;

                /* Create the legacy transaction */
                std::string strException = wallet.SendToNexusAddress(scriptPubKey, nAmount, wtx, false, 1, true);

                json::json entry;
                entry["account"] = strAccount;
                entry["address"] = hashAccount.ToString();
                entry["amount"]  = (nAmount / (double)TAO::Ledger::NXS_COIN);
                
                if(!strException.empty())
                    entry["error"] = strException;
                else
                    entry["txid"] = wtx.GetHash().GetHex();

                ret.push_back(entry);
            }

            /* If used walletpassphrase to temporarily unlock wallet, re-lock the wallet
             * This does not return unlocked for minting state, because we are migrating from the trust key and
             * the minter should not be re-started.
             */
            if(wallet.IsCrypted() && (fLocked || fMintOnly))
                wallet.Lock();

            return ret;
        }
    }
}
