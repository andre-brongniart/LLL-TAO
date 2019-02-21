/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#ifndef NEXUS_LLP_TEMPLATES_DATA_H
#define NEXUS_LLP_TEMPLATES_DATA_H

#include <LLP/include/network.h>

#include <Util/include/mutex.h>
#include <Util/include/memory.h>

#include <atomic>
#include <vector>
#include <thread>
#include <cstdint>
#include <condition_variable>

namespace LLP
{
    /* forward declarations */
    class Socket;
    class DDOS_Filter;


    /** DataThread
     *
     *  Base Template Thread Class for Server base. Used for Core LLP Packet Functionality.
     *  Not to be inherited, only for use by the LLP Server Base Class.
     *
     **/
    template <class ProtocolType>
    class DataThread
    {
    public:
        std::mutex MUTEX;

        /* Variables to track Connection / Request Count. */
        bool fDDOS;
        bool fMETER;

        /* Destructor flag. */
        std::atomic<bool> fDestruct;
        std::atomic<uint32_t> nConnections;
        uint32_t ID;
        uint32_t REQUESTS;
        uint32_t TIMEOUT;
        uint32_t DDOS_rSCORE;
        uint32_t DDOS_cSCORE;


        /* Vector to store Connections. */
        std::vector< memory::atomic_ptr<ProtocolType> > CONNECTIONS;


        /* This mirrors CONNECTIONS with pollfd settings for passing to poll methods. Windows throws SOCKET_ERROR intermittently if pass CONNECTIONS directly. */
        std::vector<pollfd> POLLFDS;


        /* The condition for thread sleeping. */
        std::condition_variable CONDITION;

        /* Data Thread. */
        std::thread DATA_THREAD;


        /** Default Constructor
         *
         **/
        DataThread<ProtocolType>(uint32_t id, bool isDDOS, uint32_t rScore, uint32_t cScore,
                                 uint32_t nTimeout, bool fMeter = false);


        /** Default Destructor
         *
         **/
        virtual ~DataThread<ProtocolType>();


        /** AddConnection
         *
         *  Adds a new connection to current Data Thread.
         *
         *  @param[in] SOCKET The socket for the connection.
         *  @param[in] DDOS The pointer to the DDOS filter to add to connection.
         *
         **/
        void AddConnection(const Socket& SOCKET, DDOS_Filter* DDOS);


        /** AddConnection
         *
         *  Adds a new connection to current Data Thread
         *
         *  @param[in] strAddress The IP address for the connection.
         *  @param[in] nPort The port number for the connection.
         *  @param[in] DDOS The pointer to the DDOS filter to add to the connection.
         *
         *  @return Returns true if successfully added, false otherwise.
         *
         **/
        bool AddConnection(std::string strAddress, uint16_t nPort, DDOS_Filter* DDOS);


        /** DisconnectAll
         *
         *  Disconnects all connections by issuing a DISCONNECT_FORCE event message
         *  and then removes the connection from this data thread.
         *
         **/
        void DisconnectAll();


        /** Thread
         *
         *  Thread that handles all the Reading / Writing of Data from Sockets.
         *  Creates a Packet QUEUE on this connection to be processed by an
         *  LLP Messaging Thread.
         *
         **/
        void Thread();


      private:


        /** disconnect_remove_event
         *
         *  Fires off a Disconnect event with the given disconnect reason
         *  and also removes the data thread connection.
         *
         *  @param[in] index The data thread index to disconnect.
         *
         *  @param[in] reason The reason why the connection is to be disconnected.
         *
         **/
        void disconnect_remove_event(uint32_t index, uint8_t reason);


        /** remove
         *
         *  Removes given connection from current Data Thread.
         *  This happens with a timeout/error, graceful close, or disconnect command.
         *
         *  @param[in] The index of the connection to remove.
         *
         **/
        void remove(int32_t index);


        /** find_slot
         *
         *  Returns the index of a component of the CONNECTIONS vector that
         *  has been flagged Disconnected
         *
         **/
        int32_t find_slot();

    };
}

#endif
