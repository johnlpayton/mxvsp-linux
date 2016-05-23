#ifndef VDOWNLOAD_H
#define VDOWNLOAD_H 1

/*
*==========================================================================
*
*   vdownload.h
*   (c) 2012 jlp
*
*   Include for downloads and uploads
*
*==========================================================================
*/
#define CHARSOH 0x01
#define CHAREOT 0x04
#define CHARACK 0x06
#define CHARNAK 0x15
#define CHARCAN 0x18
#define CHARSUB 0x1a

#define ST_IDLE     0   // Idle
#define ST_DOWNLOAD 0x001
#define ST_UPLOAD   0x002
#define ST_ACTIVE   0x100

//======== Structures

#define CHARSOH 0x01
#define CHAREOT 0x04
#define CHARACK 0x06
#define CHARNAK 0x15
#define CHARCAN 0x18
#define CHARSUB 0x1a

typedef struct S_dlparam
{
    int     state;                              // state variable
    int     fsmstate;                           // state used in XModem
    FILE*   fin;                                // fin pointer
    int     fSize;                              // file size in bytes (int)
    int     bytessent;                          // count of bytes send/received
    int     pktsize;                            // bytes in each packet
    time_t  starttime;                          // GMT of start time; (statistics)
    time_t  firstsoh;                           // GMT of first soh (statistics)
    int     dladdr;                             // download VSP (TID_UNFRAMED + ACK)
    int     uladdr;                             // upload VSP (TID_XMODEMUP))
    int     wID;                                // window ID
    int     seqnum;                             // sequence counter
    int     nbuf;                               // number in the buffer
    int     retrycntr;                          // retry counter
    int     nakcntr;                            // nak counter
    int     maxnak;                             // max nak
    int     tout;                               // timeout counter (-1 = disabled)
    int     toutlim;                            // limit for timeout
    t_ctldatapkt* pSend;                        // frame to send
    char    buf[MAXDATASIZE];                   // file buffer
    char    fname[256];                         // file name
}t_dlparam;

#define ST_IDLE 0   // Idle
#define ST_DOWNLOAD 0x001
#define ST_UPLOAD   0x002
#define ST_ACTIVE   0x100
#define ST_ERROR    0x200
#define ST_DONE     0x300
#define ST_STATUSBITS 0xf00

enum{
 UDL_MSGSTATE = 512,                               // State change
 UDL_MSGPBAR,                                    // Progress bar (p*100)
 UDL_LABEL1,
 UDL_LABEL2,
 UDL_LABEL3,
 UDL_LABEL4,
 UDL_LABELB
};

/*
* UDL_MSGSTATE(int state, 0)
* UDL_MSGPBAR(int vis, int percent[0:100])
* UDL_MSGTBYTES(int n, 0)
* UDL_MSGSBYTES(int n, 0)
* UDL_MSGRBYTES(int n, 0)
* UDL_MSGCPS(int n, 0)
* UDL_LABEL1(int vis, char* label)
* UDL_LABEL2(int vis, char* label)
* UDL_LABEL3(int vis, char* label)
* UDL_LABEL4(int vis, char* label)
*/

// Up-Down load dialog items
typedef struct S_Dnload{
    U32 tag;
    GtkWidget*  Self;
    GtkWidget*  SendButton;                     //105 send/abort
    GtkWidget*  pProgressbar;                   //IDC_TRANSFERPROGRESS
    GtkWidget*  pCombo;                         //201 (file)
    GtkWidget*  pLabel1;                        //1001
    GtkWidget*  pLabel2;
    GtkWidget*  pLabel3;
    GtkWidget*  pLabel4;
    int     direction;
    const char* Title;
    t_dlparam   sp;                              // parameters
    char    fDnName[256];                        // Download file name
    char    fUpName[256];                        // Upload file name
}t_Dnload;

#define XFRTYPESREC 1
#define XFRTYPEXMODEM 2

//======== externs
extern int TextToXFRVsp(void* v, void* txt, int ntxt);
extern int InitDownload(t_Dnload* pParam);

#endif
