#ifndef MUXEVB_H
#define MUXEVB_H 1

/* Asyncw.h */

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

// commands for the TID_EVB configuration channel
struct S_evbcmds {
    int len;
    int msg;
    int p1;
    int p2;
};

#define EVBCMD_BRATE 1
#define EVBCMD_FLOW 2
#define EVBCMD_IRQ7 3

enum t_evbsel {
    EVBParamFlowBrate=1,                            // Packed Flow|Rate
};

enum t_evbcmd {
    EVBcmdOpen=1,                            // Packed Flow|Rate
};

extern t_jdevice devEVB;                               // Device structure
extern int EVBActive;

extern int newEVB(t_jdevice* pDev, pJfunction cb);

#endif
