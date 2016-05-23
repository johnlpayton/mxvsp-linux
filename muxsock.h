#ifndef MUXSOCK_H
#define MUXSOCK_H 1
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
#if 0
#include "muxsock_P.h"
#endif

#define THREADNOTREADY 0
#define THREADIDLE 0
#define THREADREADY 1
#define THREADINKILL 2

#define INVALID_SOCKET NULL

#if USEWIN32
extern int NewClient;                           // index of client outstanding in the acceptor
extern struct hostent FAR * TheHost;
extern struct sockaddr_in service;              // for bind()
extern SOCKET ListenerSock;                     // Open this to get requests
extern OVERLAPPED osListener;                   // overlapped for the listened
#endif

//======== parameters
enum t_socksel {
    sockParamMode=1,                            // (1=connect,2=accept,3=listen)
        // 1=Listen 2=Connect
    sockParamSOCK,                              // socket to use
        // SOCKET
    sockParamRName,                             // Remote Host name (required)
        // char*
    sockParamRaddr,                             // Remote Host address
        // U32 in host order
    sockParamRport,                             // Remote Host port
        // U16 in host order
    sockParamLName,                             // Local name (read only)
        // char*
    sockParamLaddr,                             // Local address (read only)
        // U32 in host order
    sockParamLport,                             // Local port
        // U16 in host order
    sockParamFamily,                            // Default to AF_INET
    sockParamStream,                            // Default to SOCK_STREAM
    sockParamProto                              // Default to IPPROTO_TCP
};
//======== comands

enum t_sockcmd {
    // Compatability
    sockCmdInitConnectSock=1,                   // replace InitConnectSock
    sockCmdInitListenerSock,                    // replace InitListenerSock
    sockCmdmuxSockListen,                       // replace muxSockListen
    sockCmdmuxSockSpawn,                          // replace muxSockOpen
    sockCmdGetThreadState,                      // greplace direct access
    // primatives
    sockCmdConn=1,                              // Begin connection
    sockCmdReConn,                              // Attempt to reconnect
    sockCmdDisc,                                // Graceful disconnect
    sockCmdIOctl,                               // New ioctl params
};
//======== callback ids
enum t_sockCBid {
    sockCBError=1,                              // Error messages
    sockCBStatus,                               // status change
    sockCBHostAddr,                             // found host BPARAM = TCPADDRESS
    sockCBConn,                                 // connected BPARAM = TCPADDRESS
    sockCBListen,                               // Socket is listening
    sockCBDisc,
    sockCBTextM,                                // Text message
    sockCBTextMNL                               // With nexline
};

//======== error codes for sockCbError
enum t_sockDBerr {
    sockIDCreateSock=1,                              // Error messages
    sockIDHostAddr,                             // found host BPARAM = TCPADDRESS
    sockIDConn,                                 // connected BPARAM = TCPADDRESS
    sockIBDisc
};

extern int new_tcpdev(t_jdevice* pDev, pJfunction cb);
extern void muxSocThread(LPVOID lpV);
#endif
