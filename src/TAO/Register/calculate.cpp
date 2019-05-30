/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLD/include/global.h>

#include <Legacy/types/script.h>

#include <TAO/Operation/include/enum.h>
#include <TAO/Operation/include/execute.h>
#include <TAO/Operation/include/append.h>
#include <TAO/Operation/include/claim.h>
#include <TAO/Opeartion/include/create.h>
#include <TAO/Opeartion/include/credit.h>
#include <TAO/Opeartion/include/debit.h>
#include <TAO/Opeartion/include/genesis.h>
#include <TAO/Opeartion/include/script.h>
#include <TAO/Opeartion/include/transfer.h>
#include <TAO/Opeartion/include/trust.h>
#include <TAO/Opeartion/include/write.h>

#include <TAO/Register/types/stream.h>
#include <TAO/Register/include/calculate.h>

#include <new> //std::bad_alloc

/* Global TAO namespace. */
namespace TAO
{

    /* Register Layer namespace. */
    namespace Register
    {

        /* Verify the pre-states of a register to current network state. */
        bool Calculate(Contract& contract)
        {
            /* Reset the contract streams. */
            contract.Reset();

            /* Make sure no exceptions are thrown. */
            try
            {
                /* Get the contract OP. */
                uint8_t OP = 0;
                contract >> OP;

                /* Check the current opcode. */
                switch(OP)
                {

                    /* Generate pre-state to database. */
                    case TAO::Operation::OP::WRITE:
                    {
                        /* Get the Address of the Register. */
                        uint256_t hashAddress = 0;
                        contract >> hashAddress;

                        /* Get the state data. */
                        std::vector<uint8_t> vchData;
                        contract >> vchData;

                        /* Serialize the pre-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::PRESTATE);

                        /* Read the register from database. */
                        State state;
                        if(!LLD::regDB->ReadState(hashAddress, state, TAO::Register::FLAGS::PRESTATE))
                            return debug::error(FUNCTION, "OP::WRITE: register pre-state doesn't exist");

                        /* Check the ownership. */
                        if(state.hashOwner != contract.hashCaller)
                            return debug::error(FUNCTION, "OP::WRITE: cannot generate pre-state if not owner");

                        /* Serialize the pre-state into contract. */
                        contract <<= state;

                        /* Calculate the new operation. */
                        if(!TAO::Operation::Write::Execute(state, vchData, contract.nTimestamp))
                            return debug::error(FUNCTION, "OP::WRITE: cannot generate post-state");

                        /* Serialize the post-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::POSTSTATE);

                        /* Serialize the checksum into contract. */
                        contract <<= state.GetHash();

                        break;
                    }


                    /* Generate pre-state to database. */
                    case TAO::Operation::OP::APPEND:
                    {
                        /* Get the Address of the Register. */
                        uint256_t hashAddress = 0;
                        contract >> hashAddress;

                        /* Get the state data. */
                        std::vector<uint8_t> vchData;
                        contract >> vchData;

                        /* Serialize the pre-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::PRESTATE);

                        /* Read the register from database. */
                        State state;
                        if(!LLD::regDB->ReadState(hashAddress, state, TAO::Register::FLAGS::PRESTATE))
                            return debug::error(FUNCTION, "OP::APPEND: register pre-state doesn't exist");

                        /* Check the ownership. */
                        if(state.hashOwner != contract.hashCaller)
                            return debug::error(FUNCTION, "OP::APPEND: cannot generate pre-state if not owner");

                        /* Serialize the pre-state into contract. */
                        contract <<= state;

                        /* Calculate the new operation. */
                        if(!TAO::Operation::Append::Execute(state, vchData, contract.nTimestamp))
                            return debug::error(FUNCTION, "OP::APPEND: cannot generate post-state");

                        /* Serialize the post-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::POSTSTATE);

                        /* Serialize the checksum into contract. */
                        contract <<= state.GetHash();

                        break;
                    }


                    /*
                     * This does not contain any prestates.
                     */
                    case TAO::Operation::OP::CREATE:
                    {
                        /* Get the Address of the Register. */
                        uint256_t hashAddress = 0;
                        contract >> hashAddress;

                        /* Get the Register Type. */
                        uint8_t nType = 0;
                        contract >> nType;

                        /* Get the register data. */
                        std::vector<uint8_t> vchData;
                        contract >> vchData;

                        /* Create the register object. */
                        State state;
                        state.nVersion   = 1;
                        state.nType      = nType;
                        state.hashOwner  = contract.hashCaller;

                        /* Generate the post-state. */
                        if(!TAO::Operation::Create::Execute(state, vchData, contract.nTimestamp))
                            return debug::error(FUNCTION, "OP::CREATE: cannot generate post-state");

                        /* Serialize the post-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::POSTSTATE);

                        /* Serialize the checksum into contract. */
                        contract <<= state.GetHash();                        /* Serialize the post-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::POSTSTATE);

                        /* Serialize the checksum into contract. */
                        contract <<= state.GetHash();

                        break;
                    }


                    /* Transfer ownership of a register to another signature chain. */
                    case TAO::Operation::OP::TRANSFER:
                    {
                        /* Extract the address from the tx.ssOperation. */
                        uint256_t hashAddress = 0;
                        contract >> hashAddress;

                        /* Read the register transfer recipient. */
                        uint256_t hashTransfer = 0;
                        contract >> hashTransfer;

                        /* Serialize the pre-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::PRESTATE);

                        /* Read the register from database. */
                        State state;
                        if(!LLD::regDB->ReadState(hashAddress, state, TAO::Register::FLAGS::PRESTATE))
                            return debug::error(FUNCTION, "OP::TRANSFER: register pre-state doesn't exist");

                        /* Check the ownership. */
                        if(state.hashOwner != contract.hashCaller)
                            return debug::error(FUNCTION, "OP::TRANSFER: cannot generate pre-state if not owner");

                        /* Serialize the pre-state into contract. */
                        contract <<= state;

                        /* Calculate the new operation. */
                        if(!TAO::Operation::Transfer::Execute(state, hashTransfer, contract.nTimestamp))
                            return debug::error(FUNCTION, "OP::TRANSFER: cannot generate post-state");

                        /* Serialize the post-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::POSTSTATE);

                        /* Serialize the checksum into contract. */
                        contract <<= state.GetHash();

                        break;
                    }


                    /* Transfer ownership of a register to another signature chain. */
                    case TAO::Operation::OP::CLAIM:
                    {
                        /* Seek to address. */
                        contract.Seek(68);

                        /* Get last trust block. */
                        uint256_t hashAddress = 0;
                        contract >> hashAddress;

                        /* Serialize the pre-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::PRESTATE);

                        /* Read the register from database. */
                        State state;
                        if(!LLD::regDB->ReadState(hashAddress, state, TAO::Register::FLAGS::PRESTATE))
                            return debug::error(FUNCTION, "OP::TRANSFER: register pre-state doesn't exist");

                        /* Calculate the new operation. */
                        if(!TAO::Operation::Claim::Execute(state, contract.hashCaller, contract.nTimestamp))
                            return debug::error(FUNCTION, "OP::CLAIM: cannot generate post-state");

                        /* Serialize the post-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::POSTSTATE);

                        /* Serialize the checksum into contract. */
                        contract <<= state.GetHash();

                        break;
                    }

                    /* Coinbase operation. Creates an account if none exists. */
                    case TAO::Operation::OP::COINBASE:
                    {
                        /* Seek to end. */
                        contract.Seek(40);

                        break;
                    }


                    /* Coinstake operation. Requires an account. */
                    case TAO::Operation::OP::TRUST:
                    {
                        /* Seek to scores. */
                        contract.Seek(64);

                        /* Get the trust score. */
                        uint64_t nScore = 0;
                        contract >> nScore;

                        /* Get the stake reward. */
                        uint64_t nReward = 0;
                        contract >> nReward;

                        /* Serialize the pre-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::PRESTATE);

                        /* Read the register from database. */
                        State state;
                        if(!LLD::regDB->ReadTrust(contract.hashCaller, state))
                            return debug::error(FUNCTION, "OP::TRUST: register pre-state doesn't exist");

                        /* Serialize the pre-state into contract. */
                        contract <<= state;

                        /* Calculate the new operation. */
                        if(!TAO::Operation::Trust::Execute(state, nReward, nScore, contract.nTimestamp))
                            return debug::error(FUNCTION, "OP::TRUST: cannot generate post-state");

                        /* Serialize the post-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::POSTSTATE);

                        /* Serialize the checksum into contract. */
                        contract <<= state.GetHash();

                        break;
                    }


                    /* Coinstake operation. Requires an account. */
                    case TAO::Operation::OP::GENESIS:
                    {
                        /* Get last trust block. */
                        uint256_t hashAddress = 0;
                        contract >> hashAddress;

                        /* Get the stake reward. */
                        uint64_t nReward = 0;
                        contract >> nReward;

                        /* Serialize the pre-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::PRESTATE);

                        /* Read the register from database. */
                        State state;
                        if(!LLD::regDB->ReadState(hashAddress, state))
                            return debug::error(FUNCTION, "OP::GENESIS: register pre-state doesn't exist");

                        /* Serialize the pre-state into contract. */
                        contract <<= state;

                        /* Calculate the new operation. */
                        if(!TAO::Operation::Genesis::Execute(state, nReward, contract.nTimestamp))
                            return debug::error(FUNCTION, "OP::GENESIS: cannot generate post-state");

                        /* Serialize the post-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::POSTSTATE);

                        /* Serialize the checksum into contract. */
                        contract <<= state.GetHash();

                        break;
                    }


                    /* Debit tokens from an account you own. */
                    case TAO::Operation::OP::DEBIT:
                    {
                        /* Get the register address. */
                        uint256_t hashFrom = 0;
                        contract >> hashFrom;

                        /* Get the transfer address. */
                        uint256_t hashTo = 0;
                        contract >> hashTo;

                        /* Get the transfer amount. */
                        uint64_t  nAmount = 0;
                        contract >> nAmount;

                        /* Serialize the pre-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::PRESTATE);

                        /* Read the register from database. */
                        State state;
                        if(!LLD::regDB->ReadState(hashTo, state))
                            return debug::error(FUNCTION, "OP::DEBIT: register pre-state doesn't exist");

                        /* Serialize the pre-state into contract. */
                        contract <<= state;

                        /* Calculate the new operation. */
                        if(!TAO::Operation::Debit::Execute(state, nAmount, contract.nTimestamp))
                            return debug::error(FUNCTION, "OP::DEBIT: cannot generate post-state");

                        /* Serialize the post-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::POSTSTATE);

                        /* Serialize the checksum into contract. */
                        contract <<= state.GetHash();

                        break;
                    }


                    /* Credit tokens to an account you own. */
                    case TAO::Operation::OP::CREDIT:
                    {
                        /* Seek to address. */
                        contract.Seek(68);

                        /* Get the transfer address. */
                        uint256_t hashAddress = 0;
                        contract >> hashTo;

                        /* Get the transfer address. */
                        uint256_t hashProof = 0;
                        contract >> hashProof;

                        /* Get the transfer amount. */
                        uint64_t  nAmount = 0;
                        contract >> nAmount;

                        /* Serialize the pre-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::PRESTATE);

                        /* Read the register from database. */
                        State state;
                        if(!LLD::regDB->ReadState(hashAddress, state))
                            return debug::error(FUNCTION, "OP::CREDIT: register pre-state doesn't exist");

                        /* Serialize the pre-state into contract. */
                        contract <<= state;

                        /* Calculate the new operation. */
                        if(!TAO::Operation::Credit::Execute(state, nAmount, contract.nTimestamp))
                            return debug::error(FUNCTION, "OP::CREDIT: cannot generate post-state");

                        /* Serialize the post-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::POSTSTATE);

                        /* Serialize the checksum into contract. */
                        contract <<= state.GetHash();

                        break;
                    }


                    /* Authorize is enabled in private mode only. */
                    case TAO::Operation::OP::AUTHORIZE:
                    {
                        /* Seek to address. */
                        contract.Seek(96);

                        break;
                    }


                    /* Create unspendable legacy script, that acts to debit from the account and make this unspendable. */
                    case TAO::Operation::OP::LEGACY:
                    {
                        /* Get the register address. */
                        uint256_t hashAddress = 0;
                        contract >> hashAddress;

                        /* Get the transfer amount. */
                        uint64_t  nAmount = 0;
                        contract >> nAmount;

                        /* Serialize the pre-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::PRESTATE);

                        /* Read the register from database. */
                        State state;
                        if(!LLD::regDB->ReadState(hashAddress, state))
                            return debug::error(FUNCTION, "OP::CREDIT: register pre-state doesn't exist");

                        /* Serialize the pre-state into contract. */
                        contract <<= state;

                        /* Calculate the new operation. */
                        if(!TAO::Operation::Legacy::Execute(state, nAmount, contract.nTimestamp))
                            return debug::error(FUNCTION, "OP::CREDIT: cannot generate post-state");

                        /* Serialize the post-state byte into contract. */
                        contract <<= uint8_t(TAO::Register::STATES::POSTSTATE);

                        /* Serialize the checksum into contract. */
                        contract <<= state.GetHash();

                        /* Get the script data. */
                        Legacy:;Script script;
                        contract >> script;

                        break;
                    }

                    default:
                        return debug::error(FUNCTION, "invalid code for register verification");
                }


                /* Check for end of stream. */
                if(!contract.End())
                {
                    /* Get the contract OP. */
                    OP = 0;
                    contract >> OP;

                    /* Check for OP::REQUIRE. */
                    if(OP != TAO::Operation::OP::REQUIRE && OP != TAO::Operation::OP::VALIDATE)
                        return debug::error(FUNCTION, "contract cannot contain second OP beyond REQUIRE or VALIDATE");
                }

            }
            catch(const std::exception& e)
            {
                return debug::error(FUNCTION, "exception encountered ", e.what());
            }

            /* If nothing failed, return true for evaluation. */
            return true;
        }
    }
}
