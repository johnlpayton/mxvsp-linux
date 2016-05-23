/******************************************************************************
* FILE: lapj_iface.c
*
* CONTAINS:
*
* PURPOSE:
*   interface routines
*
******************************************************************************/
#include "jtypes.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>   /* File control definitions */
#include <stdlib.h>
#include <stdio.h>   /* Standard input/output definitions */
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */
#include <errno.h>   /* Error number definitions */
#include <termios.h> /* POSIX terminal control definitions */
#include <sys/ioctl.h>
#include <pthread.h>    /* threads */

#if USEGDK
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#endif

#if USEWIN32
#include <windows.h>
#include <commdlg.h>
#include <alloc.h>
#include <commctrl.h>
#include <shlobj.h>
#include <fcntl.h>
#include <process.h>
#endif

#include "memlock.h"
#include "muxctl.h"
//#include "framer.h"
#include "muxevb.h"
#include "muxevbW.h"
#include "bctype.h"
#include "jsk_buff.h"
#include "lapj.h"

extern void jlpByteStreamOut(char* title, char* pp, int len);

t_lock gLapjCS;


/*
**************************************************************************
*  FUNCTION: int isTxDevEmpty(int did)
*
*  PURPOSE: Check the waiting on write flag
*
*  ARGUMENTS:
*   f     flag
*
*  RETURN:
*   1 empty
*   0 is full
*
*  COMMENTS:
* 2014-dec-29
*   jlp created for mxttyecc
**************************************************************************
*/
int isTxDevEmpty(intptr_t dev)
{
    struct S_EvbThreadvars* pT = (struct S_EvbThreadvars*)dev;

#if (EASYWINDEBUG>5) && 1
if((inicfg.prflags & PRFLG_EVB) || (inicfg.prflags & PRFLG_LAPJ))
{
printf("isTxDevEmpty 0x%06x %c\n",dev,(char)((pT->fWaitingOnWrite)? 'F':'T') );
}
#endif
    if(pT->fWaitingOnWrite)
        return 0;
    else
        return 1;
}

/*
**************************************************************************
*  FUNCTION: int HandleRxOutbound(intptr_t dev, U8* buf, int nBuf)
*
*  PURPOSE: Callback to send data to the demultiplexot
*
*  ARGUMENTS:
*   did     device
*
*  RETURN:
*
*  COMMENTS:
*
**************************************************************************
*/
int HandleRxOutbound(intptr_t dev, U8* bufin, int nBuf)
{
    int     retval;
    struct S_EvbThreadvars* pT = (struct S_EvbThreadvars*)dev;
    struct AsynWinComm* TheComm = pT->TheComm;

#if (EASYWINDEBUG>1) && (LAPJDEBUG>1)  && 0
{
printf("HandleRxOutbound\n");
}
#endif
        if (nBuf > 0)
        {
#if (EASYWINDEBUG>5) && 1
if((inicfg.prflags & PRFLG_EVB) || (inicfg.prflags & PRFLG_LAPJ))
{
jlpDumpFrame("HandleRxOutbound",bufin,nBuf);
}
#endif

#if (LAPJSTREAM>0) && 1
if((inicfg.prflags & PRFLG_EVB) || (inicfg.prflags & PRFLG_LAPJ))
{
jlpByteStreamOut("tm",bufin, nBuf);
}
#endif
           DoMonStream(bufin,                  // Parse the upstream
                        nBuf,
                        ROUTESRCEVB,
                        &(TheComm->pRecvQ),
                        pT->pMuxFramer);
        }
    retval = 1;

    return(retval);
}
/*
**************************************************************************
*  FUNCTION: int DoUnf(intptr_t dev, U8* buf, int nBuf)
*
*  PURPOSE: Callback to handle unframed data
*
*  ARGUMENTS:
*   did     device
*
*  RETURN:
*
*  COMMENTS:
*   Ignores the data then
*   return 0 to continue
*   Changed because in Linux, the prompt was broken up over two buffers
*   We have to make this self sychronizing accross a break.
*
**************************************************************************
*/
#define NMACSBUGPROMPT 7
const char MACSBUGPROMPT[NMACSBUGPROMPT]=
{
0x0d, 0x0a, 0x64, 0x42, 0x55, 0x47, 0x3e
};
static int nMacsbugScan=0;

int DoUnf(intptr_t dev, U8* bufin, int nBuf)
{
    int     retval = 0;
    int     k,n;
//    struct S_EvbThreadvars* pT = (struct S_EvbThreadvars*)dev;
//    struct AsynWinComm* TheComm = pT->TheComm;

#if (LAPJSTREAM>0) && 1
if((inicfg.prflags & PRFLG_EVB) || (inicfg.prflags & PRFLG_LAPJ))
{
jlpByteStreamOut("uf",bufin, nBuf);
}
#endif

    for(k=0; k<nBuf; k++)
    {
        if(bufin[k] != MACSBUGPROMPT[nMacsbugScan])
        {
            nMacsbugScan=0;
        }
        else
        {
            nMacsbugScan += 1;
            if(nMacsbugScan >= NMACSBUGPROMPT)
            {
                retval = 1;
                nMacsbugScan = 0;
                break;
            }
        }
    }


#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_LAPJ)
{
printf("DoUnf returns %d\r\n",retval);
//jlpDumpFrame("DoUnf",bufin, nBuf);
}
#endif

    return(retval);
}
/*
**************************************************************************
*  FUNCTION: int try_writeDev(int did, char* buf, int num)
*
*  PURPOSE: Try to write the Uart (tx)
*
*  ARGUMENTS:
*   points to the evb structure
*   buf     character buffer
*   num     Nmber
*
*  RETURN:
*   1 success
*   0 Fail
*
*  COMMENTS:
*   This is the primary write to the uart
*
* 2016-4-30 jlp
*   - Linux version new.  This is like the start write
*     This is a little messy for Linux.  We might be able to clean up Lapj
*     -In Lapj mode a packet is held in memory until acked. So we don't
*       have to copy the memort, herely keep a pointer.
*     -In transparent mode the buffer can get clobbered by LAPJ while it is
*       being sent so we have to make a copy.  It is a rare event but does happen.
*       What we need is for LAPJ to hold off reusing a burrer until transmission is
*       complete.  We'll just copy all for now
*
* 2014=dec=29-4:00pm
*   Got to modify this for the new EvbSendThePacket
**************************************************************************
*/
extern int evbSendIRQ7;
int try_writeDev(intptr_t dev, U8* buf, int num)
{
    struct S_EvbThreadvars* pT = (struct S_EvbThreadvars*)dev;
    struct AsynWinComm* TheComm;
    int     dwWritten;
    int     retval = 0;
    BOOL    bWrite;
    char    tmpbuf[4];

    TheComm = pT->TheComm;


#if (EASYWINDEBUG>5) && 1
if((inicfg.prflags & PRFLG_EVB) || (inicfg.prflags & PRFLG_LAPJ))
{
printf("try_writeDev\n");
}
#endif

#if 0 // turn this off for now
//======== Hack and partial test for IRQ 7
    if(evbSendIRQ7)
    {
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("try_writeDev:evbSendIRQ7\r\n");
}
#endif
        *buf = XIRQ7;
    }
    evbSendIRQ7 = 0;
//========
#endif

    if(!isTxDevEmpty(dev))
    {
        printf("try_writeDev: Overflow A %d %d\n",(pT->nBufHold), (pT->iBufHold));
//        ErrorInComm("try_writeDev: Overflow A");
        return FALSE;
    }

    if(num <=0)
        return(FALSE);              // NOTHING TO DO

    //======== copy and signal a pending write
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_EVB)
{
printf("try_writeDev:memcpy 0x%06x <- 0x%06x (%d)\r\n",pT->pBufHold,buf,num);
}
#endif
    memcpy(pT->pBufHold,buf,num);
    pT->nBufHold = num;
    pT->iBufHold = 0;
    pT->fWaitingOnWrite = TRUE;
    tmpbuf[0]=EVBCMD_WRITETOUART;
    write(TheComm->cmdsock[0],tmpbuf,1);
    //========

#if (EASYWINDEBUG>5) && 1
if((inicfg.prflags & PRFLG_EVB) || (inicfg.prflags & PRFLG_LAPJ))
{
printf("try_writeDev: %d\n",num);
jlpDumpFrame("try_writeDev",buf, num);
}
#endif

#if (LAPJSTREAM>0) && 1
if((inicfg.prflags & PRFLG_EVB) || (inicfg.prflags & PRFLG_LAPJ))
{
jlpByteStreamOut("te",buf, num);
}
#endif

    return(TRUE);
}

/*
**************************************************************************
*  FUNCTION: int LapjEmptied(intptr_t idLap, int N)
*
*  PURPOSE: Lapj Emptied
*  ARGUMENTS:
*   N     free windows
*
*  RETURN:
*
*  COMMENTS:
*  Ooops, we need to include come argument here to access the structures

**************************************************************************
*/
int LapjEmptied(intptr_t idLap, int N)
{
    int     retval=1;
    char    cmdbuf[4];
    struct S_EvbThreadvars* pT = (struct S_EvbThreadvars*)idLap;
    struct AsynWinComm* TheComm = pT->TheComm;
    struct S_SIMECC* pLapj = pT->pEcc;

#if (EASYWINDEBUG>5) && 1
if((inicfg.prflags & PRFLG_EVB) || (inicfg.prflags & PRFLG_LAPJ))
{
printf("LapjEmptied\n");
}
#endif
    //======== Queue a phony packet request, Fetch will ignore if empty
    cmdbuf[0] = EVBCMD_FROMROUTER;
    write(TheComm->cmdsock[0],cmdbuf,1);

    return 0;
}

/*
**************************************************************************
*  FUNCTION: int LapjReturn1(int N)
*
*  PURPOSE: return 1 (true)
*  ARGUMENTS:
*   N     free windows
*
*  RETURN:
*
*  COMMENTS:

**************************************************************************
*/
int LapjReturn1(int N)
{
    return 1;
}
/*
**************************************************************************
*  FUNCTION: void* LocalMalloc(size_t N)
*
*  PURPOSE: Processor denendant Malloc
*
*  ARGUMENTS:
*   N     bytes
*   buf     character buffer
*   num     Nmber
*
*  RETURN:
*   1 success
*   0 Fail
*
*  COMMENTS:

**************************************************************************
*/
void* LocalMalloc(size_t N)
{
#if (EASYWINDEBUG>5) && 1
if((inicfg.prflags & PRFLG_EVB) || (inicfg.prflags & PRFLG_LAPJ))
{
printf("LocalMalloc\n");
}
#endif
    return malloc(N);
}
/*
**************************************************************************
*  FUNCTION: int InitializeLapj(struct S_SIMECC* pEcc)
*
*  PURPOSE: Application dependant initialization
*
*  ARGUMENTS:
*   pEcc     device
*
*  RETURN:
*   1 success
*   0 Fail
*
*  COMMENTS:
*   Params
*   IsTxUartEmpty : to uart Call to the uart to poll if it can accept data
*   CanAcceptMvsp : to mxvsp call to mxvsp to see if it can accept data
*   LapjCanSend : lapj to uart
*
*   WriteToUart : to uart Major supplied by Uart, write a data buffer
*   WriteToMvsp : Major, call to the mxvsp demultiplexor
*   DoUnframed : call to detect macsbug prompt (crash detection)
*
**************************************************************************
*/

int InitializeLapj(struct S_SIMECC* pEcc, struct S_EvbThreadvars* pTop)
{
    InitLockSection(&gLapjCS);

    //======== Init subsystems
    pEcc->IsTxUartEmpty = isTxDevEmpty;             // 2014-12-28 (use the fWaitingOnWrite flag)
                                                    // Nope,got to have two flags
    pEcc->CanAcceptMvsp = LapjReturn1;              // 2014-12-28 this queue is allways ready
    pEcc->LapjCanSend = LapjEmptied;                // Signal from Lapj it can accept a tx buffer
    pEcc->WriteToUart = try_writeDev;               // Callback for Tx data
    pEcc->WriteToMvsp = HandleRxOutbound;           // Callback for Rx data
    pEcc->DoUnframed = DoUnf;                       // Callback for unframed data

    pEcc->idTxUart = (intptr_t)pTop;                // Device to write to
    pEcc->idMvsp = (intptr_t)pTop;                  // Use structure for the id
    pEcc->idLapj = (intptr_t)pTop;

    //========= Get rid of dependence of DiD     LAPJFLAG_TXBYPASS
    pEcc->DiD = 0;

    //======== Primary initialization
    DoLapj(pEcc,LAPJCMD_INIT,(intptr_t)malloc,0);

}

/******************************************************************************
*
******************************************************************************/
/*
* enqueue at the end. This is Before  the header q
*/
#if EASYWINDEBUG && 0
static void preq(t_DQue* q, t_DQue* e)
{
printf("q:0x%06x (0x%06x 0x%06x) e:0x%06x (0x%06x 0x%06x)\r\n",q,q->next,q->prev,e,e->next,e->prev);
}
#endif

t_DQue* k_enqueue(t_DQue* q, t_DQue* elem)
{
    //======== Illegal call
    if(!q || !elem)
        return NULL;

    LockSection(&gLapjCS);

// preq(q,elem);
    elem->next = q;
    elem->prev = q->prev;
    //======== add protection here
// preq(q,elem);
    q->prev->next = elem;
    q->prev = elem;
// preq(q,elem);
    //========
    UnLockSection(&gLapjCS);

    return elem;
}

/*
* deque an element Call using k_dequeue(Hdr->prev) to get a fifo
*/
t_DQue* k_dequeue(t_DQue* elem)
{

    //======== Check for null pinter
    if(!elem)
        return NULL;

    //======== check for empty
    if(elem->next == elem)
        return(NULL);

    //======== add protection here
    LockSection(&gLapjCS);

    elem->prev->next = elem->next;
    elem->next->prev = elem->prev;
    //========
    elem->next = elem;
    elem->prev = elem;

    UnLockSection(&gLapjCS);
    return elem;
}

/*
**************************************************************************
*
*  FUNCTION: U8 BlockCRC8(U8* src, int n, U8* pCRC8)
*
*  PURPOSE: Compute a crc block
*
*  ARGUMENTS:
*   src:    block pointer
*   n:      byte count
*   pCRC8   pointer crc storage location
*
*  RETURN:
*   Value of the CRC after computation
*
*  COMMENTS:
*   The CRC is computed over a block.   The user can supply
*   a U8 pointer to an existing CRC.  If the pointer is NULL,
*   a temporary location is initialized to 0xFF and used.
*
*   The user must keep the CRC memory byte if he wants to span
*   over multiple calls.
*
*   Note, if a valid CRC has been appended to the block and
*   the composite block run through a CRC, the result is 0.
*   This is a mathematical relationship independant of the
*   initialization value.  Some implementations invert the CRC
*   before it is sent.  The value is dependant on the polynomial.
*   I don't know why people do that. Probably they don't understand
*   the underlying mathematics.
*
*   The tables use the polynomial 0xA6 recommened by Koopman
*
* Cyclic Redundancy Code (CRC) Polynomial Selection For Embedded Networks
* Philip Koopman, Tridib Chakravarty
* Preprint: The International Conference on Dependable Systems and Networks, DSN-2004
*
*   Note, the LSB corresponds to x^8 because serial conversion shifts the
*   lsb out and the crc should be shifted with x^8 first.  The 0xA6 notation
*   is in conventional math format x^8, x^7, x^5 .. x^1. Can cause some
*   confusion to readers but if the polynomial definition is bit
*   reversed then shifts are to the right for single but stuff things work out.
*   A sevond note is that CRCs are linear computations.
*   Let crcByte(n) be the result of a crc run on a byte (8 iterations)
*   then form a byte <aaaabbbb>
*   crcByte(<aaaabbbb>)
*   = crcbyte(<aaaa0000> + <0000bbbb>)
*   = crcbyte(<aaaa0000>) + crcbyte(<0000bbbb>)
*   where all adds are bitwise
*
**************************************************************************
*/
#if 0
static U8 CRC8TableFL[16]={
0x00, 0x6b, 0xd6, 0xbd, 0x67, 0x0c, 0xb1, 0xda,
0xce, 0xa5, 0x18, 0x73, 0xa9, 0xc2, 0x7f, 0x14
};
static U8 CRC8TableFH[16]={
0x00, 0x57, 0xae, 0xf9, 0x97, 0xc0, 0x39, 0x6e,
0xe5, 0xb2, 0x4b, 0x1c, 0x72, 0x25, 0xdc, 0x8b
};
U8 BlockCRC8(U8* src, int n, U8* pCRC8)
{
    int k;
    U8  t;
    U8  tmpcrc;

    if(!pCRC8)                                  // create a variable if we are not passed one
    {
        pCRC8 = &tmpcrc;
        *pCRC8 = 0xff;
    }

    for(k=0; k<n; k++)
    {
        t=*pCRC8^src[k];                        // add the input
        *pCRC8 = CRC8TableFL[t & 0xF] ^         //   crc(0000nnnn)
                 CRC8TableFH[(t >>4) & 0xF];    // + crc(mmmm0000)
    }
    return(*pCRC8);
}
#endif