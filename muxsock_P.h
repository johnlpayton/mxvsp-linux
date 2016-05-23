#ifndef MUXSOCK_P_H
#define MUXSOCK_P_H 1
/*
*==========================================================================
*
*   muxsock.h
*   (c) 2012 jlp
*
*   Include for packet data and routing
*
*==========================================================================
*/

typedef struct S_muxsockparam{
    t_jdevice*  pParent;                        // Parent used for callback
    HANDLE      hTCP;                           // handle to the thread
    int         sockID;                         // ID for straight sockets
    GSocketClient*      hSocket;                // handle to the socket
    GSocketConnection*  hTcpConnection;
    int         threadState;                    // set by the thread when it beging exec
    int         sockParamMode;                  // mode
    char*       sockParamRName;                 // name for connnect
    U32         sockParamRaddr;                 // remote address
    U32         sockParamLaddr;                 // local address
    U16         sockParamRport;                 // remote port
    U16         sockParamLport;                 // local port
    U32          sockParamFamily;
    U32          sockParamStream;
    U32          sockParamProto;
    U32          readPending;

//    t_mailbox   InMail;                         // header for the router input
    t_ctlreqpkt InQHeader;                       // received DataPacket
    int         cmdsock[2];                     // for linux, we use a socket

    t_ctldatapkt* pRecvQ;                       // received DataPacket
    t_ctldatapkt* pXmitQ;                       // transmit DataPacket
    t_framer    framer;                          // receiver framer

    char     rxbuf[MAXDATASIZE];                // buffer
    int     nRead;
}t_muxsockparam;

#define EVBCMD_WRITETOUART  60                // wants to write the network
#define EVBCMD_FROMROUTER   61                // input from router into mxvsp framing
#define EVBCMD_KICK         62                // kick to lapj
#define EVBCMD_DELAYWRITEDONE 63              // partial wirite completed (network emptied)

#define THREADNOTREADY 0
#define THREADREADY 1
#define THREADINKILL 2
#define THREADINSTARTUP 3

#endif
