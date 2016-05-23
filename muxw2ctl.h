#ifndef MUXW2CTL_H
#define MUXW2CTL_H 1


/*
*==========================================================================
*
*   muxw2ctl.h
*   (c) 2012 jlp
*
*   Include for swcondary window control
*
*==========================================================================
*/


typedef int (*pWinFunction)(void* pMapEntry, int cmd, long arg1, long arg2);

// See muxvtty.c::VspAction
enum {
    W2CMD_INIT = 0x401,                         // Init
    W2CMD_DESTROY,                              // Destroy
    W2CMD_TEXT,                                 // Set text
    W2CMD_CUT,                                  // WM_CUT
    W2CMD_COPY,                                 // WM_COPY
    W2CMD_PASTE,                                // WM_PASTE
    W2CMD_T500,                                 // 500ms tic
    W2CMD_START,                                 // start
    W2MSG_STATE,                               // State change
    W2MSG_DONE,
    W2MSG_ERROR,
    W2MSG_TOUT
};

typedef struct S_windowmap{
    U8      eTid;                                // EVB tid
    U8      wTab;                                // tab on display window
    U8      wFlags;                             // flag handling
    U8      pad2;
    HWND    wHnd;                                // window handle
    HWND    hWndCB;                             // Callback Handle to use
    pWinFunction  doAction;                     // function to write text
    void*   pData;                              // Data for the call
    char    wName[8];                           // Short name
} t_windowmap;

// Flabs default to 0, may be set in W2CMD_INIT
#define W2NOREFRAME 0x01    /* Bit set means send entire packet */
#define W2NODISCARD 0x02    /* Bit set means dont auto discard (testing) */


extern t_windowmap TTYWindows[];
extern t_windowmap ActiveTTY;
extern int ActiveTab;
extern t_lock gRouterCS;
extern int OutstandingDataPkts;
extern int OutstandingCtlPkts;
extern HANDLE hRouter;                                 // Handle to the thread
extern U32  dwRouterID;                              // ID set by windows
extern int     RouterActive;                           // flag for start sequencing
extern t_semaphore RouteInputSem;                           // input semaphore for the router
extern HANDLE RouteExitEvent;                          // exit event
extern t_ctlreqpkt RouteInQHeader;                     // header for the router input
extern t_mailbox   WmuxInMail;
extern int     WmuxActive;


extern int InitMuxCtl(pJfunction packetToLocal,pJfunction msgToLocalToMain);
extern int InitWrte(void);
extern int LookUpWinByTID(int wAddr);
extern int LookUpWinByTAB(int wAddr);
extern int LookUpWinByHWND(HWND hwnd);
extern int vmxRegisterWindow(int wID, int tid, pWinFunction efunc, HWND hWnd, HWND hWndCB);
extern int vmxUnRegisterWindow(int wTid);
extern void doTimerForAll();
extern void SendIRQ7(void);
extern void GoF80000(void);

#endif
