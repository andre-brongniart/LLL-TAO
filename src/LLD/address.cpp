/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLD/types/address.h>

namespace LLD
{

    /** The Database Constructor. To determine file location and the Bytes per Record. **/
    AddressDB::AddressDB(const uint16_t nPort, const uint8_t nFlagsIn, const uint32_t nBucketsIn, const uint32_t nCacheIn)
    : SectorDatabase(std::string("_ADDR/") + std::to_string(nPort)
    , nFlagsIn
    , nBucketsIn
    , nCacheIn)
    {
    }


    /* Default Destructor */
    AddressDB::~AddressDB()
    {
    }


    /*  Writes the Trust Address to the database. */
    bool AddressDB::WriteTrustAddress(uint64_t nKey, const LLP::TrustAddress &addr)
    {
        return Write(std::make_pair(std::string("addr"), nKey), addr, "addr");
    }


    /*  Reads the Trust Address from the database. */
    bool AddressDB::ReadTrustAddress(uint64_t nKey, LLP::TrustAddress &addr)
    {
        return Read(std::make_pair(std::string("addr"), nKey), addr);
    }


    /*  Writes this address to the database. */
    bool AddressDB::WriteThisAddress(uint64_t nKey, const LLP::BaseAddress &this_addr)
    {
        return Write(std::make_pair(std::string("this"), nKey), this_addr);
    }


    /*  Reads the this address from the database. */
    bool AddressDB::ReadThisAddress(uint64_t nKey, LLP::BaseAddress &this_addr)
    {
        return Read(std::make_pair(std::string("this"), nKey), this_addr);
    }


    /*  Writes the last time DNS was updated. */
    bool AddressDB::WriteLastUpdate()
    {
        uint64_t nUpdated = runtime::timestamp();
        return Write(std::string("updated"), nUpdated);
    }


    /*  Reads the last time DNS was updated. */
    bool AddressDB::ReadLastUpdate(uint64_t& nUpdated)
    {
        return Read(std::string("updated"), nUpdated);
    }


}
