/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLC/include/random.h>
#include <TAO/Register/types/basevm.h>

#include <TAO/Register/types/address.h>

#include <openssl/rand.h>

#include <unit/catch2/catch.hpp>

TEST_CASE( "Base VM allocation tests", "[register]")
{
    TAO::Register::BaseVM registers;

    {
        TAO::Register::Value value;
        uint8_t data = 55;

        registers.allocate(data, value);
        REQUIRE(registers.available() == 1016);

        uint8_t data2;
        registers.deallocate(data2, value);

        REQUIRE(data == data2);

        REQUIRE(value.nBytes == 1);

    }


    {
        TAO::Register::Value value;
        uint16_t data = 55555;

        registers.allocate(data, value);
        REQUIRE(registers.available() == 1016);

        uint16_t data2;
        registers.deallocate(data2, value);

        REQUIRE(data == data2);

        REQUIRE(value.nBytes == 2);
    }


    {
        TAO::Register::Value value;
        uint32_t data = 823847328;

        registers.allocate(data, value);
        REQUIRE(registers.available() == 1016);

        uint32_t data2;
        registers.deallocate(data2, value);

        REQUIRE(data == data2);

        REQUIRE(value.nBytes == 4);
    }


    {
        TAO::Register::Value value;
        uint64_t data = 82323847387847328;

        registers.allocate(data, value);
        REQUIRE(registers.available() == 1016);

        uint64_t data2;
        registers.deallocate(data2, value);

        REQUIRE(data == data2);

        REQUIRE(value.nBytes == 8);
    }


    {
        TAO::Register::Value value;
        uint256_t data = LLC::GetRand256();

        registers.allocate(data, value);
        REQUIRE(registers.available() == 992);

        uint256_t data2;
        registers.deallocate(data2, value);

        REQUIRE(data == data2);

        REQUIRE(value.nBytes == 32);
    }


    {
        TAO::Register::Value value;
        uint512_t data = LLC::GetRand512();

        registers.allocate(data, value);
        REQUIRE(registers.available() == 960);

        uint512_t data2;
        registers.deallocate(data2, value);

        REQUIRE(data == data2);

        REQUIRE(value.nBytes == 64);
    }


    {
        TAO::Register::Value value;
        uint1024_t data = LLC::GetRand1024();

        registers.allocate(data, value);
        REQUIRE(registers.available() == 896);

        uint1024_t data2;
        registers.deallocate(data2, value);

        REQUIRE(data == data2);

        REQUIRE(value.nBytes == 128);
    }


    {
        TAO::Register::Value value;
        std::vector<uint8_t> data(55, 0);
        RAND_bytes((uint8_t*)&data[0], data.size());

        registers.allocate(data, value);
        REQUIRE(registers.available() == 968);

        std::vector<uint8_t> data2;
        registers.deallocate(data2, value);

        REQUIRE(std::equal(data.begin(), data.end(), data2.begin()));

        REQUIRE(data2.size() == 55);

        REQUIRE(value.nBytes == 55);
    }


    {
        TAO::Register::Value value;
        std::vector<uint8_t> data(65, 0);
        RAND_bytes((uint8_t*)&data[0], data.size());

        registers.allocate(data, value);
        REQUIRE(registers.available() == 952);

        std::vector<uint8_t> data2;
        registers.deallocate(data2, value);

        REQUIRE(std::equal(data.begin(), data.end(), data2.begin()));

        REQUIRE(data2.size() == 65);

        REQUIRE(value.nBytes == 65);
    }


    {
        TAO::Register::Value value;
        std::string data = "this is a random string";

        registers.allocate(data, value);
        REQUIRE(registers.available() == 1000);

        std::string data2;
        registers.deallocate(data2, value);

        REQUIRE(data == data2);

        REQUIRE(data2.size() == 23);

        REQUIRE(value.nBytes == 23);
    }



    {
        TAO::Register::Value value;
        std::string data = "this is a random string";

        registers.allocate(data, value);
        REQUIRE(registers.available() == 1000);

        TAO::Register::Value value2;
        std::string data2 = "random";
        registers.allocate(data2, value2);

        REQUIRE(registers.contains(value, value2));
        REQUIRE_FALSE(registers.contains(value2, value));
        REQUIRE(registers.compare(value, value2) != 0);
    }


}
