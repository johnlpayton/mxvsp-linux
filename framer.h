#ifndef FRAMER_H
#define FRAMER_H 1
/*
*==========================================================================
*
*   muxctl.h
*   (c) 2012 jlp
*
*   Include for packet data and routing
*
*==========================================================================
*/

#ifdef XOFF
#undef XOFF
#endif
#define XOFF 0x13

#ifdef XON
#undef XON
#endif
#define XON 0x11

#ifdef XIRQ7
#undef XIRQ7
#endif
#define XIRQ7 0x1c

#ifdef XNULL
#undef XNULL
#endif
#define XNULL 0x00

#ifdef XSOH
#undef XSOH
#endif
#define XSOH 0x01

#ifdef XSTX
#undef XSTX
#endif
#define XSTX 0x02

#ifdef XETX
#undef XETX
#endif
#define XETX 0x03

#ifdef XDLE
#undef XDLE
#endif
#define XDLE 0x10

#ifdef RTNC
#undef RTNC
#endif
#define RTNC -1

#ifdef RTNO
#undef RTNO
#endif
#define RTNO -2

#ifdef RTCHAR
#undef RTCHAR
#endif
#define RTCHAR -3

#ifdef XEOF
#undef XEOF
#endif
#define XEOF -4

// special addresses
#define ADDRUNFRAMED 1 // Unframed data goes here
#define ADDRFTP 2 // File transfers go here

// Framer states
#define FRM_OUTSIDE 1
#define FRM_ADDR 2
#define FRM_SEQ 3
#define FRM_INSIDE 4

typedef struct S_framer{
    char*   Bufin;      // input buffer
    int     szBufin;    // size of input
    int     cBufin;     // current index
    char*   Bufout;     // output buffer
    int     szBufOut;   // size of output (max)
    int     cBufout;    // current index
    char    id;         // id for a frame
    char    seq;        // sequence count for a frame
    char    state;      // DLE state machine variable
    char    inframe;    // frame state variable
}t_framer;


extern int StuffAFrame(t_framer* fp);
extern int UnStuffFrame(t_framer* fp);
extern int MonitorFrame(t_framer* fp);
extern int DoMonStream(char *bufIn, int nIn, int srcID, t_ctldatapkt** CurrMonFrame, t_framer* fp);
extern int SendFrameToRouter(t_ctldatapkt* p, int srcID);
extern int SendFramed(char* buf, int nBuf, int srcID, int vspID, int seqNo);
extern int SendUnFramed(char* buf, int nBuf, int srcID);
extern t_ctldatapkt* ReframeData(char* buf, int nBuf, int faddr, int fseq);
extern t_ctldatapkt*  DeFramePacket(t_ctldatapkt* pkIn, int* faddr, int* fseq);
extern int ExtractAddrSeq(char* bufin, int* pAddr, int* pSeq);
#endif 
