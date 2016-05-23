#ifndef MUXEVBW_H
#define MUXEVBW_H 1

/* muxevb.h common */

#define EVBSOCKETPRIV O
#define EVBSOCKPUBLIC 1

#define NOFLOW 0
#define HARDWAREFLOW 1
#define SOFTFLOW 2

#define CN_RECEIVE 0X001
#define CN_TRANSMIT 0X002
#define CN_EVENT 0X004
#define CN_LINE 0X008

#define EVBNOTACTIVE 0
#define EVBONLINE 1
#define EVBSIM 2
#define EVBACTIVE (EVBONLINE | EVBSIM)

// notify codes for the callback function
//
#define ASYN_ERROR 1
#define ASYN_CHAR 2
#define ASYN_LINE 3


#define EVBCMD_BRATE 1                          // Change The Baudrate
#define EVBCMD_FLOW 2                           // Set the flow control
#define EVBCMD_IRQ7 3                           // send an IRQ7
#define EVBCMD_BREAK 4                          // Set/Clear Uart Break
#define EVBCMD_LAPJCONN 5                       // Set Lapj connect
#define EVBCMD_LAPJDISC 6                       // set Lapj disconnect
#define EVBCMD_LAPJPAUSE 7                      // Lapj Pause
#define EVBCMD_RESUME 8                         // Lapj Resume

#define EVBCMD_WRITETOUART  60                // lapj wants to write the uart
#define EVBCMD_FROMROUTER   61                // input from router into lapj framing
#define EVBCMD_KICK         62                // kick to lapj
#define EVBCMD_DELAYWRITEDONE 63              // partial wirite completed (uart emptied)

typedef struct AsynWinComm
{
    struct S_EvbThreadvars* pThreadVars;
    t_jdevice*  pParent;                        // Parent used for callback
    int         PortId;                         // the COM port
    char        cCOMNAME[128];                    // name of the com port
    int         comrate;                        // data rate
    int         comflow;                        // com flow control

    HANDLE      hThread;                        // handle of the thread
//    HANDLE      ExitEvent;                      // exit event
//    HANDLE      InputSem;                       // input semaphore
    t_ctlreqpkt InQHeader;                      // header for the router input
    int         cmdsock[2];                     // for linux, we use a socket
                                                // 0=muxctl, 1=muxevb

    t_ctldatapkt* pRecvQ;                       // received DataPacket
    t_ctldatapkt* pXmitQ;                       // transmit DataPacket

    char        rxbuf[MAXDATASIZE];             // buffer
}t_AsynWinComm;

#define NUM_READSTAT_HANDLES 6

typedef struct S_EvbThreadvars{
    struct AsynWinComm* TheComm;                // Com Structure
    struct S_SIMECC* pEcc;                      // Lapj structure
    struct S_framer* pMuxFramer;                // upstream framer
    char*      pBufHold;                        // Hold buffer (see comments in lapj_iface.c)
    int        nBufHold;
    int        iBufHold;
//    HANDLE     LapjEvent;                       // Lapj can accept a buffer
//    HANDLE     NullEvent;
    HANDLE     hArray[NUM_READSTAT_HANDLES];
    DWORD      dwCommEvent;                     // result from WaitCommEvent
    DWORD      dwOvRes;                         // result from GetOverlappedResult
    DWORD      dwRead;                          //   ytes actually read
    DWORD      dwWritten;
    DWORD      dwToWrite;
    BOOL       fThreadDone;
    BOOL       fWaitingOnRead;
    BOOL       fWaitingOnStat;
    BOOL       fWaitingOnWrite;

}t_EvbThreadvars;

#endif