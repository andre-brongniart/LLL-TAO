/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2018

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <Legacy/types/legacy_minter.h>

#include <Legacy/include/create.h>
#include <Legacy/include/trust.h>
#include <Legacy/types/address.h>
#include <Legacy/types/transaction.h>
#include <Legacy/wallet/addressbook.h>

#include <LLC/types/bignum.h>

#include <LLD/include/global.h>

#include <LLP/include/global.h>
#include <LLP/include/version.h>

#include <TAO/Ledger/include/chainstate.h>
#include <TAO/Ledger/include/constants.h>
#include <TAO/Ledger/include/timelocks.h>
#include <TAO/Ledger/include/enum.h>
#include <TAO/Ledger/include/stake.h>
#include <TAO/Ledger/types/state.h>

#include <Util/include/args.h>
#include <Util/include/debug.h>
#include <Util/include/runtime.h>
#include <Util/templates/datastream.h>


namespace Legacy
{

    /* Define constants for use by minter */


    /* Initialize static variables */
    std::atomic<bool> LegacyMinter::fIsStarted(false);
    std::atomic<bool> LegacyMinter::fStopMinter(false);

    std::thread LegacyMinter::minterThread;


    LegacyMinter& LegacyMinter::GetInstance()
    {
        static LegacyMinter legacyMinter;

        return legacyMinter;
    }


    /* Destructor - Returns reserve key if not used */
    LegacyMinter::~LegacyMinter()
    {
        /* If minter has reserved a key to stake for Genesis, return it to the key pool and clean up the pointer. */
        if(pReservedTrustKey != nullptr)
        {
            pReservedTrustKey->ReturnKey();

            delete pReservedTrustKey;
            pReservedTrustKey = nullptr;
        }

        /* Don't delete wallet, just null out the reference to it */
        pStakingWallet = nullptr;
    }


    /* Tests whether or not the stake minter is currently running. */
    bool LegacyMinter::IsStarted() const
    {
        return LegacyMinter::fIsStarted.load();
    }


    /* Retrieves the current internal value for the block weight metric. */
    double LegacyMinter::GetBlockWeight() const
    {
        return nBlockWeight.load();
    }


    /* Retrieves the current block weight metric as a percentage of maximum. */
    double LegacyMinter::GetBlockWeightPercent() const
    {
        return (nBlockWeight.load() * 100.0 / 10.0);
    }


    /* Retrieves the current internal value for the trust weight metric. */
    double LegacyMinter::GetTrustWeight() const
    {
        return nTrustWeight.load();
    }


    /* Retrieves the current trust weight metric as a percentage of maximum. */
    double LegacyMinter::GetTrustWeightPercent() const
    {
        return (nTrustWeight.load() * 100.0 / 90.0);
    }


    /* Retrieves the current staking reward rate (previously, interest rate) */
    double LegacyMinter::GetStakeRate() const
    {
        return nStakeRate.load();
    }


    /* Retrieves the current staking reward rate as an annual percentage */
    double LegacyMinter::GetStakeRatePercent() const
    {
        return nStakeRate.load() * 100.0;
    }


    /* Checks whether the stake minter is waiting for average coin
     * age to reach the required minimum before staking Genesis.
     */
    bool LegacyMinter::IsWaitPeriod() const
    {
        return LegacyMinter::fIsStarted.load();
    }


    /* Start the stake minter. */
    bool LegacyMinter::Start()
    {
        if(LegacyMinter::fIsStarted.load())
        {
            debug::log(0, FUNCTION, "Attempt to start Stake Minter when already started.");
            return false;
        }

    	/* Check that stake minter is configured to run.
    	 * Stake Minter default is to run for non-server and not to run for server
    	 */
        if(!(config::GetBoolArg("-stake") || config::GetBoolArg("-staking")))
    	{
    		debug::log(2, "Stake Minter not configured. Startup cancelled.");

            return false;
        }

        /* After v7 activates, legacy minter will no longer run */
        if(TAO::Ledger::VersionActive(runtime::unifiedtimestamp(), 7) || TAO::Ledger::CurrentVersion() > 7)
        {
            debug::log(0, FUNCTION, "Legacy stake minter does not support block version 7 or higher. Use Tritium stake minter");
            return false;
        }

        if(pStakingWallet == nullptr)
            pStakingWallet = &(Wallet::GetInstance());

        /* Wallet should be unlocked. */
        if(pStakingWallet->IsLocked())
        {
            debug::error(FUNCTION, "Cannot start stake minter for locked wallet.");

            return false;
        }

        /* Ensure stop flag is reset or thread will immediately exit */
        LegacyMinter::fStopMinter.store(false);

        LegacyMinter::minterThread = std::thread(LegacyMinter::LegacyMinterThread, this);

        LegacyMinter::fIsStarted.store(true);

        return true;
    }


    /* Stop the stake minter. */
    bool LegacyMinter::Stop()
    {
        if(LegacyMinter::fIsStarted.load())
        {
            debug::log(0, FUNCTION, "Shutting down Stake Minter");

            /* Set signal flag to tell minter thread to stop */
            LegacyMinter::fStopMinter.store(true);

            /* Wait for minter thread to stop */
            if(LegacyMinter::minterThread.joinable())
                LegacyMinter::minterThread.join();

            if(pReservedTrustKey != nullptr)
            {
                /* If stop while staking for Genesis, return the key. It will reserve a new one if restarted */
                pReservedTrustKey->ReturnKey();
                delete pReservedTrustKey;
                pReservedTrustKey = nullptr;
            }

            LegacyMinter::fIsStarted.store(false);
            LegacyMinter::fStopMinter.store(false);
            return true;
        }

        return false;
    }


    /*  Gets the trust key for the current wallet. If none exists, retrieves a new
     *  key from the key pool to use as the trust key for Genesis.
     */
    void LegacyMinter::FindTrustKey()
    {
        static bool fVerified = false;

        trustKey.SetNull();

        /* Attempt to use the trust key cached in the wallet */
        std::vector<uint8_t> vchTrustKey = pStakingWallet->GetTrustKey();

        if(!vchTrustKey.empty())
        {
            uint576_t cKey;
            cKey.SetBytes(vchTrustKey);

            /* Read the key cached in wallet from the Trust DB */
            if(!LLD::Trust->ReadTrustKey(cKey, trustKey))
            {
                /* Cached wallet trust key not found in trust db, reset it (can happen if Genesis is orphaned). */
                trustKey.SetNull();
                pStakingWallet->RemoveTrustKey();
            }
            else
            {
                /* Found trust key cached in wallet */
                if(!fVerified)
                {
                    /* Only need to log this first time it is retrieved (ie, before verified) */
                    debug::log(0, FUNCTION, "Staking with existing trust key");
                }
            }
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
                uint64_t nCutoff = TAO::Ledger::EndTimelock(4) + (uint64_t)(config::fTestNet ? TAO::Ledger::TRUST_KEY_TIMESPAN_TESTNET
                                                                                             : TAO::Ledger::TRUST_KEY_TIMESPAN);

                /* Search through the trust keys. */
                for(const auto& trustKeyCheck : vKeys)
                {
                    /* Check whether trust key is part of current wallet */
                    NexusAddress address;
                    address.SetPubKey(trustKeyCheck.vchPubKey);

                    if(pStakingWallet->HaveKey(address))
                    {
                        /* Trust key is in wallet, check version of most recent block */
                        debug::log(2, FUNCTION, "Checking trustKey ", address.ToString());

                        /* Read the block for hashLastBlock to check its version. We can use this, even if it has not
                         * been verified as most recent hashLastBlock, yet, because all we need is the version of the block.
                         */
                        TAO::Ledger::BlockState state;
                        if(LLD::Ledger->ReadBlock(trustKeyCheck.hashLastBlock, state))
                        {
                            debug::log(2, FUNCTION, "Checking last stake height=", state.nHeight, " version=", state.nVersion);

                            /* This will ignore a v5 key if last block was before v4 cutoff, so it does not
                             * use keys affected by grace period issue.
                             */
                            if(state.nVersion >= 5 && state.nTime >= nCutoff)
                            {
                                /* Set the trust key if found. */
                                trustKey = trustKeyCheck;

                                /* Store trust key */
                                pStakingWallet->SetTrustKey(trustKey.vchPubKey);

                                debug::log(0, FUNCTION, "Found Trust Key matching current wallet");
                                break;
                            }
                            else
                            {
                                /* Expired pre-v5 Trust Key. Do not use. */
                                debug::log(2, FUNCTION, "Found expired version 4 Trust key. Not using.");
                            }
                        }
                    }
                }
            }
        }


        /* When trust key found, verify data stored against actual last trust block.
         * Older versions of code did not revert trust key data after an orphan and it may be out of date.
         * The GetLastTrust method can be slow, so only do it the first time after minter is started.
         */
        if(!fVerified && !trustKey.IsNull() && !config::fShutdown.load())
        {
            TAO::Ledger::BlockState stateLast = TAO::Ledger::ChainState::stateBest.load();
            if((trustKey.hashLastBlock == trustKey.hashGenesisBlock) && (trustKey.hashLastBlock == stateLast.GetHash()))
            {
                /* Just mined Genesis for this trust key, so it is up to date */
                fVerified = true;
            }
            else if(GetLastTrust(trustKey, stateLast))
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

                fVerified = true;
            }
        }


        if (trustKey.IsNull() && !config::fShutdown.load())
        {
            /* No trust key found. Reserve a new key to use as the trust key for Genesis */
            debug::log(0, FUNCTION, "Staking for Genesis with new trust key");

            pReservedTrustKey = new ReserveKey(pStakingWallet);
            pReservedTrustKey->GetReservedKey();
        }

        return;
    }


    /* Creates a new legacy block that the stake minter will attempt to mine via the Proof of Stake process. */
    bool LegacyMinter::CreateCandidateBlock()
    {
        /* Use appropriate settings for Testnet or Mainnet */
        static const uint32_t nMaxTrustScore = config::fTestNet.load() ? TAO::Ledger::TRUST_SCORE_MAX_TESTNET : TAO::Ledger::TRUST_SCORE_MAX;
        static const uint32_t nMaxBlockAge = config::fTestNet.load() ? TAO::Ledger::TRUST_KEY_TIMESPAN_TESTNET : TAO::Ledger::TRUST_KEY_TIMESPAN;

        static uint32_t nWaitCounter = 0; //Prevents log spam during wait period

        /* Create the block to work on */
        block = LegacyBlock();

        /* Retrieve the current setting for minimum stake interval */
        const uint32_t nMinimumInterval = TAO::Ledger::MinStakeInterval(block);

        ReserveKey dummyReserveKey(pStakingWallet); //Reserve key not used by CreateBlock for nChannel=0
        Coinbase dummyCoinbase; // Coinbase not used for staking

        if(!CreateBlock(dummyReserveKey, dummyCoinbase, 0, 0, block))
            return debug::error(FUNCTION, "Unable to create candidate block");

        if(!trustKey.IsNull())
        {

            /* Looking to stake Trust for existing key */
            uint576_t cKey;
            cKey.SetBytes(trustKey.vchPubKey);

            /* Check that the database still has key. */
            TrustKey keyCheck;
            if(!LLD::Trust->ReadTrustKey(cKey, keyCheck))
            {
                debug::error(FUNCTION, "Trust key was disconnected");

                /* Ensure it is erased */
                LLD::Trust->Erase(cKey);

                /* Remove trust key from wallet */
                pStakingWallet->RemoveTrustKey();

                /* Set the trust key to null state. */
                trustKey.SetNull();

                /* For next iteration, go back to staking for Genesis */
                if (pReservedTrustKey != nullptr)
                {
                    delete pReservedTrustKey;  // should never happen, this is a precaution
                    pReservedTrustKey = nullptr;
                }

                pReservedTrustKey = new ReserveKey(pStakingWallet);
                pReservedTrustKey->GetReservedKey();

                return false;
            }

            /* Get the last stake block for this trust key. */
            TAO::Ledger::BlockState statePrev;
            if(!LLD::Ledger->ReadBlock(trustKey.hashLastBlock, statePrev))
                return debug::error(FUNCTION, "Failed to get last stake for trust key");

            /* Enforce the minimum staking transaction interval. (current height is candidate height - 1) */
            uint32_t nCurrentInterval = (block.nHeight - 1) - statePrev.nHeight;
            if(nCurrentInterval < nMinimumInterval)
            {
                /* Below minimum interval for generating stake blocks. Increase sleep time until can continue normally. */
                nSleepTime = 5000; //5 second wait is reset below (can't sleep too long or will hang until wakes up on shutdown)

                /* Update log every 60 iterations (5 minutes) */
                if((nWaitCounter % 60) == 0)
                    debug::log(0, FUNCTION, "Stake Minter: Too soon after mining last stake block. ",
                               (nMinimumInterval - nCurrentInterval), " blocks remaining until staking available.");

                ++nWaitCounter;

                return false;
            }

            /* Get the sequence and previous trust. */
            uint32_t nSequence = 0;
            uint32_t nScore = 0;
            uint32_t nPrevScore = 0;

            /* Validate that previous trust block was a legacy coinstake */
            if(statePrev.vtx[0].first != TAO::Ledger::TRANSACTION::LEGACY)
            {
                debug::error(FUNCTION, "Trust key for Legacy Stake Minter does not have Legacy transaction in Genesis coinstake.");

                throw std::runtime_error("Trust key for Legacy Stake Minter does not have Legacy transaction in Genesis coinstake.");
            }

            /* Retrieve the previous coinstake transaction */
            uint512_t prevHash = statePrev.vtx[0].second;
            Transaction txPrev;
            if(!LLD::Legacy->ReadTx(prevHash, txPrev))
                return debug::error(FUNCTION, "Failed to read previous coinstake for trust key");

            /* Handle if previous trust was Genesis */
            if(txPrev.IsGenesis())
            {
                nSequence   = 1;
                nPrevScore  = 0;
            }
            else
            {
                /* Extract the trust from the previous block. */
                uint1024_t hashDummy;

                if(!txPrev.ExtractTrust(hashDummy, nSequence, nPrevScore))
                    return debug::error("Failed to extract trust from previous block");

                /* Increment sequence number for next trust transaction. */
                ++nSequence;
            }

            /* Calculate time since the last trust block for this trust key (block age = age of previous trust block). */
            uint32_t nBlockAge = TAO::Ledger::ChainState::stateBest.load().GetBlockTime() - statePrev.GetBlockTime();

            /* Block age less than maximum awards trust score increase equal to the current block age. */
            if(nBlockAge <= nMaxBlockAge)
                nScore = std::min((nPrevScore + nBlockAge), nMaxTrustScore);

            /* Block age more than maximum allowed is penalized 3 times the time it has exceeded the maximum. */
            else
            {
                /* Calculate the penalty for score (3x the time). */
                uint32_t nPenalty = (nBlockAge - nMaxBlockAge) * 3;

                /* Catch overflows and zero out if penalties are greater than previous score. */
                if(nPenalty < nPrevScore)
                    nScore = nPrevScore - nPenalty;
                else
                    nScore = 0;
            }

            /* Double check that the trust score cannot exceed the maximum */
            if(nScore > nMaxTrustScore)
                nScore = nMaxTrustScore;

            /* Prevout index needs to be 0 in coinstake transaction. */
            block.vtx[0].vin[0].prevout.n = 0;

            /* Prevout hash is trust key hash */
            block.vtx[0].vin[0].prevout.hash = trustKey.GetHash();

            /* Serialize previous trust block hash, new sequence, and new trust score into vin. */
            DataStream scriptPub(block.vtx[0].vin[0].scriptSig, SER_NETWORK, LLP::PROTOCOL_VERSION);
            scriptPub << statePrev.GetHash() << nSequence << nScore;

            /* Set the script sig (Script doesn't support serializing all types needed) */
            block.vtx[0].vin[0].scriptSig.clear();
            block.vtx[0].vin[0].scriptSig.insert(block.vtx[0].vin[0].scriptSig.end(), scriptPub.begin(), scriptPub.end());

            /* Write the trust key into the output script. */
            block.vtx[0].vout.resize(1);
            block.vtx[0].vout[0].scriptPubKey << trustKey.vchPubKey << Legacy::OP_CHECKSIG;

        }
        else
        {
            /* Looking to stake Genesis for new key */
            /* Genesis prevout is null */
            block.vtx[0].vin[0].prevout.SetNull();

            /* Write the reserved key into the output script. */
            block.vtx[0].vout.resize(1);
            block.vtx[0].vout[0].scriptPubKey << pReservedTrustKey->GetReservedKey() << Legacy::OP_CHECKSIG;
        }

        /* Add the coinstake inputs. Also generates coinstake output with staking reward */
        if(!pStakingWallet->AddCoinstakeInputs(block))
        {
            /* Wallet has no balance, or balance unavailable for staking. Increase sleep time to wait for balance. */
            nSleepTime = 5000;

            /* Update log every 60 iterations (5 minutes) */
            if((nWaitCounter % 60) == 0)
                debug::log(0, FUNCTION, "Stake Minter: Wallet has no balance or no spendable inputs available.");

            ++nWaitCounter;

            return false;
        }
        else if(nSleepTime == 5000) {
            /* Normal stake operation now available. Reset sleep time and wait counter. */
            nSleepTime = 1000;
            nWaitCounter = 0;
        }

        /* Update the current stake rate in the minter (not used for calculations, retrievable for display) */
        nStakeRate.store(trustKey.StakeRate(block, block.GetBlockTime()));

        return true;
    }


    /* Calculates the Trust Weight and Block Weight values for the current trust key and candidate block. */
    bool LegacyMinter::CalculateWeights()
    {
        static const double LOG3 = log(3); // Constant for use in calculations

        /* Use appropriate settings for Testnet or Mainnet */
        static const uint32_t nTrustWeightBase = config::fTestNet.load() ? TAO::Ledger::TRUST_WEIGHT_BASE_TESTNET : TAO::Ledger::TRUST_WEIGHT_BASE;
        static const uint32_t nMaxBlockAge = config::fTestNet.load() ? TAO::Ledger::TRUST_KEY_TIMESPAN_TESTNET : TAO::Ledger::TRUST_KEY_TIMESPAN;
        static const uint32_t nMinimumCoinAge = config::fTestNet.load() ? TAO::Ledger::MINIMUM_GENESIS_COIN_AGE_TESTNET : TAO::Ledger::MINIMUM_GENESIS_COIN_AGE;

        /* Use local variables for calculations, then set instance variables at the end */
        double nCurrentTrustWeight = 0.0;
        double nCurrentBlockWeight = 0.0;

        static uint32_t nWaitCounter = 0; //Prevents log spam during wait period

        /* Weight for Trust transactions combines trust weight and block weight. */
        if(block.vtx[0].IsTrust())
        {
            uint32_t nTrustScore;
            uint32_t nBlockAge;


            /* Retrieve the current Trust Score from the candidate block */
            if(!block.TrustScore(nTrustScore))
            {
                debug::error(FUNCTION, "Failed to get trust score");
                return false;
            }

            /* Retrieve the current Block Age from the candidate block */
            if(!block.BlockAge(nBlockAge))
            {
                debug::error(FUNCTION, "Failed to get block age");
                return false;
            }

            /* Trust Weight base is time for 50% score. Weight continues to grow with Trust Score until it reaches max of 90.0
             * This formula will reach 45.0 (50%) after accumulating 84 days worth of Trust Score (Mainnet base),
             * while requiring close to a year to reach maximum.
             */
            double nTrustWeightRatio = (double)nTrustScore / (double)nTrustWeightBase;
            nCurrentTrustWeight = std::min(90.0, (44.0 * log((2.0 * nTrustWeightRatio) + 1.0) / LOG3) + 1.0);

            /* Block Weight reaches maximum of 10.0 when Block Age equals the defined timespan for max age */
            double nBlockAgeRatio = (double)nBlockAge / (double)nMaxBlockAge;
            nCurrentBlockWeight = std::min(10.0, (9.0 * log((2.0 * nBlockAgeRatio) + 1.0) / LOG3) + 1.0);
        }

        /* Weights for Genesis transactions only use trust weight with its value based on average coin age. */
        else
        {
            uint64_t nCoinAge;

            /* Calculate the average Coin Age for coinstake inputs of candidate block. */
            if(!block.vtx[0].CoinstakeAge(nCoinAge))
            {
                debug::error(FUNCTION, "Failed to get coinstake age");
                return false;
            }

            /* Genesis has to wait for average coin age to reach one full trust key timespan. */
            if(nCoinAge < nMinimumCoinAge)
            {
                /* Record that stake minter is in wait period */
                fWait.store(true);

                /* Increase sleep time to wait for coin age to meet requirement (can't sleep too long or will hang until wakes up on shutdown) */
                nSleepTime = 5000;

                /* Update log every 60 iterations (5 minutes) */
                if((nWaitCounter % 60) == 0)
                {
                    uint32_t nRemainingWaitTime = (nMinimumCoinAge - nCoinAge) / 60; //minutes

                    debug::log(0, FUNCTION, "Stake Minter: Average coin age is immature. ",
                               nRemainingWaitTime, " minutes remaining until staking available.");
                }

                ++nWaitCounter;

                return false;
            }
            else if(nSleepTime == 5000)
            {
                /* Reset wait period setting */
                fWait.store(false);

                /* Reset sleep time after coin age meets requirement. */
                nSleepTime = 1000;
                nWaitCounter = 0;
            }

            /* Trust Weight For Genesis is based on Coin Age. Genesis trust weight is less than normal trust weight,
             * only reaching a maximum of 10.0 after average Coin Age reaches 84 days (Mainnet base).
             */
            double nGenesisTrustRatio = (double)nCoinAge / (double)nTrustWeightBase;
            nCurrentTrustWeight = std::min(10.0, (9.0 * log((2.0 * nGenesisTrustRatio) + 1.0) / LOG3) + 1.0);

            /* Block Weight remains zero while staking for Genesis */
            nCurrentBlockWeight = 0.0;
        }

		/* Update instance settings */
		nBlockWeight.store(nCurrentBlockWeight);
		nTrustWeight.store(nCurrentTrustWeight);

        return true;
    }


    /* Attempt to solve the hashing algorithm at the current staking difficulty for the candidate block */
    void LegacyMinter::MintBlock()
    {
        /* Calculate the minimum Required Energy Efficiency Threshold.
         * Minter can only mine Proof of Stake when current threshold exceeds this value.
         *
         * Staking weights (trust and block) reduce the required threshold by reducing the numerator of this calculation.
         * Weight from stake balance (based on nValue from coinstake) reduces the required threshold by increasing the denominator.
         */
        uint64_t nStake = block.vtx[0].vout[0].nValue;
        double nRequired = ((108.0 - nTrustWeight.load() - nBlockWeight.load()) * TAO::Ledger::MAX_STAKE_WEIGHT) / nStake;

        /* Calculate the target value based on difficulty. */
        LLC::CBigNum bnTarget;
        bnTarget.SetCompact(block.nBits);
        uint1024_t nHashTarget = bnTarget.getuint1024();

        debug::log(0, FUNCTION, "Staking new block from ", hashLastBlock.SubString(),
                                " at weight ", (nTrustWeight.load() + nBlockWeight.load()),
                                " and stake rate ", nStakeRate.load());

        /* Minting will exit if stop minter, shutdown, or v7 activation reached mid-process */
        bool fstop = LegacyMinter::fStopMinter.load() || config::fShutdown.load()
            || TAO::Ledger::VersionActive(runtime::unifiedtimestamp(), 7) || TAO::Ledger::CurrentVersion() > 7;

        /* Search for the proof of stake hash solution until it mines a block, minter is stopped,
         * or network generates a new block (minter must start over with new candidate)
         */
        while(hashLastBlock == TAO::Ledger::ChainState::hashBestChain.load() && !fstop)
        {
            /* Check for stop before attempting to mint block */
            fstop = LegacyMinter::fStopMinter.load() || config::fShutdown.load()
                || TAO::Ledger::VersionActive(runtime::unifiedtimestamp(), 7) || TAO::Ledger::CurrentVersion() > 7;

            if(fstop)
                continue;

            /* Update the block time for difficulty accuracy. */
            block.UpdateTime();
            uint32_t nBlockTime = block.GetBlockTime() - block.vtx[0].nTime; // How long have we been working on this block

            /* If just starting on block, wait */
            if(nBlockTime == 0)
            {
                runtime::sleep(1);
                continue;
            }

            /* Calculate the new Efficiency Threshold for the next nonce.
             * To stake, this value must be larger than required threshhold.
             * Block time increases the value while nonce decreases it.
             */
            double nThreshold = (nBlockTime * 100.0) / block.nNonce;

            /* If threshhold is not larger than required, wait and keep trying with the same nonce value until threshold increases */
            if(nThreshold < nRequired)
            {
                runtime::sleep(10);
                continue;
            }

            /* Log every 1000 attempts */
            if(block.nNonce % 1000 == 0)
                debug::log(3, FUNCTION, "Threshold ", nThreshold, " exceeds required ", nRequired,
                                        ", mining Proof of Stake with nonce ", block.nNonce);

            /* Handle if block is found. */
            uint1024_t hashProof = block.StakeHash();
            if(hashProof < nHashTarget)
            {
                debug::log(0, FUNCTION, "Found new stake hash ", hashProof.SubString());

                ProcessBlock();
                break;
            }

            /* Increment nonce for next iteration. */
            ++block.nNonce;
        }

        return;
    }


    bool LegacyMinter::ProcessBlock()
    {
        /* Add the transactions into the block from memory pool, but only if not Genesis (Genesis block for trust key has no transactions except coinstake). */
        if(!block.vtx[0].IsGenesis())
            AddTransactions(block.vtx);

        /* Build the Merkle Root. */
        std::vector<uint512_t> vMerkleTree;
        for(const auto& tx : block.vtx)
            vMerkleTree.push_back(tx.GetHash());

        block.hashMerkleRoot = block.BuildMerkleTree(vMerkleTree);

        /* Sign the block. */
        if(!SignBlock(block, *pStakingWallet))
            return debug::error(FUNCTION, "Failed to sign block");

        /* Check the block. */
        if(!block.Check())
            return debug::error(FUNCTION, "Check block failed");

        /* Check the work and process the block.
         * After a successful check, CheckWork() calls LLP::Process() for the new block.
         * That method will call LegacyBlock::Accept() and BlockState::Accept()
         * After all is accepted, BlockState::Accept() will call BlockState::SetBest()
         * to set the new best chain. This final method relays the new block to the
         * network. It also connects the block.
         */
        if(!CheckWork(block, *pStakingWallet))
            return debug::error(FUNCTION, "Check work failed");

        if(pReservedTrustKey != nullptr)
        {
            /* New block was Genesis using reserved key.
             * Block processing creates the actual TrustKey and writes it to trust db.
             * Here just need to save it to the wallet for future retrieval.
             */
            pStakingWallet->SetTrustKey(pReservedTrustKey->GetReservedKey());

            /* Marks the key as used by the wallet and removes permanently from key pool. */
            pReservedTrustKey->KeepKey();

            delete pReservedTrustKey;
            pReservedTrustKey = nullptr;

            debug::log(0, FUNCTION, "New trust key generated and stored");
        }

        return true;
    }


    /* Method run on its own thread to oversee stake minter operation. */
    void LegacyMinter::LegacyMinterThread(LegacyMinter* pLegacyMinter)
    {

        debug::log(0, FUNCTION, "Stake Minter Started");
        pLegacyMinter->nSleepTime = 5000;
        bool fLocalTestnet = config::fTestNet.load() && !config::GetBoolArg("-dns", true);
        uint32_t nConnections = (LLP::LEGACY_SERVER ? LLP::LEGACY_SERVER->GetConnectionCount() : 0) + LLP::TRITIUM_SERVER->GetConnectionCount();

        /* If the system is still syncing/connecting on startup, wait to run minter */
        while((TAO::Ledger::ChainState::Synchronizing() || (nConnections == 0 && !fLocalTestnet))
                && !LegacyMinter::fStopMinter.load() && !config::fShutdown.load())
        {
            runtime::sleep(pLegacyMinter->nSleepTime);
            nConnections = (LLP::LEGACY_SERVER ? LLP::LEGACY_SERVER->GetConnectionCount() : 0) + LLP::TRITIUM_SERVER->GetConnectionCount();
        }

        /* Check stop/shutdown status after wait ends */
        if(LegacyMinter::fStopMinter.load() || config::fShutdown.load())
            return;

        debug::log(0, FUNCTION, "Stake Minter Initialized");

        pLegacyMinter->nSleepTime = 1000;

        /* Stake minter will cease operation if stop minter, shutdown, or v7 activation reached */
        bool fexit = LegacyMinter::fStopMinter.load() || config::fShutdown.load()
            || TAO::Ledger::VersionActive(runtime::unifiedtimestamp(), 7) || TAO::Ledger::CurrentVersion() > 7;

        /* Minting thread will continue repeating this loop until exit flag set. */
        while(!fexit)
        {
            runtime::sleep(pLegacyMinter->nSleepTime);

            /* Check stop/shutdown status after wakeup */
            fexit = LegacyMinter::fStopMinter.load() || config::fShutdown.load()
                || TAO::Ledger::VersionActive(runtime::unifiedtimestamp(), 7) || TAO::Ledger::CurrentVersion() > 7;

            if(fexit)
                continue;

            /* Save the current best block hash immediately after sleep in case it changes while we do setup */
            pLegacyMinter->hashLastBlock = TAO::Ledger::ChainState::hashBestChain.load();

            /* Reload trust key each block iteration to assure we get updates after new block or orphan disconnect.
             * Don't need to do this if staking Genesis.
             */
            if(pLegacyMinter->pReservedTrustKey == nullptr)
                pLegacyMinter->FindTrustKey();

            /* Set up the candidate block the minter is attempting to mine */
            if(!pLegacyMinter->CreateCandidateBlock())
                continue;

            /* Updates weights for new candidate block */
            if(!pLegacyMinter->CalculateWeights())
                continue;

            /* Attempt to mine the current proof of stake block */
            pLegacyMinter->MintBlock();
        }

        /* If get here because fShutdown set or v7 activates, have to wait for join.
         * Join is issued in Stop, which needs to be called by shutdown process, too.
         */
        while(!LegacyMinter::fStopMinter.load())
            runtime::sleep(500);


        /* Stop has been issued. Now thread can end. */
    }

}
