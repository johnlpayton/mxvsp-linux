/*******************************************************************************
*   FILE: muxpacket.c
*
*   Memory and buffer management.
*
*   Basic design is built around Packet buffers.
*
*   Externals
*
*
*******************************************************************************/
#include "jtypes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
//#include "muxvttyW.h"
//#include "muxctl.h"
//#include "framer.h"
//#include "muxevb.h"
//#include "muxsock.h"

t_ctldatapkt mempktdata[MAXDATAPACKETS];
t_ctlreqpkt mempktctl[MAXCTLPACKETS];
int OutstandingDataPkts;
int OutstandingCtlPkts;


/*---------------------------------------------------------------------------
*  void InitPacketCtl(void)
*
*  Description:
*   Initialize
*
*  Parameters:
*     none
*
*  Comments
*   Initialization
*
*   T O D O
*       Place data and control packet headers in a linked list for
*       better allocation and management.
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
void InitPacketCtl(void)
{

    // init packet allocators
    memset(mempktdata,0,sizeof(mempktdata));
    memset(mempktctl,0,sizeof(mempktctl));
    OutstandingDataPkts = 0;
    OutstandingCtlPkts = 0;
    InitLockSection(&gRouterCS);

}

/*---------------------------------------------------------------------------
*  int ReleaseCtlPacket(t_ctlreqpkt* ThePacket, t_lock* lock)
*
*  Description:
*   Conditionally release a control packet to the heap
*
*  Parameters:
*     t_ctldatapkt*                     ThePacket pointer to the packet
*     t_lock* lock           Section pointer for lockinh
*
*   Returns
*       Locking counter value after the decrement
*
*  Comments
*   Decrement locking counter. If it does to 0 then free the data pointer
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
int ReleaseCtlPacket(t_ctlreqpkt* ThePacket, t_lock* lock)
{
    LockSection(lock);
#if EASYWINDEBUG & 0
printf("ReleaseCtlPacket 0x%08x\n",ThePacket);
#endif
        ThePacket->pData = 0;
        OutstandingCtlPkts -= 1;
    UnLockSection(lock);
    return(0);
}

/*---------------------------------------------------------------------------
*  t_ctlreqpkt* AllocCtlPacket(t_lock* lock)
*
*  Description:
*   Open a window
*
*  Parameters:
*     t_ctldatapkt*                     ThePacket pointer to the packet
*     t_lock* lock           Section pointer for lockinh
*
*   Returns
*       NULL:   No packet available
*       pointer Packet
*
*  Comments
*   get a data packet from the system
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
t_ctlreqpkt* AllocCtlPacket(t_lock* lock)
{
    t_ctlreqpkt* retval = NULL;
    char*   pTmp;
    int     k;

    LockSection(lock);
        for(k = 0; k < MAXCTLPACKETS; k++)
        {
            retval = &mempktctl[k];
            if(retval->pData == NULL)
            {
                retval->pData = (void*)1;           // mark it
                OutstandingCtlPkts += 1;
                break;                              // get out
            }
        }
        if(k == MAXCTLPACKETS)
            retval = NULL;
#if EASYWINDEBUG & 0
printf("AllocCtlPacket 0x%08x\n",retval);
#endif

    UnLockSection(lock);

    if(!retval)
        FatalError("Control packey overflow");

    return(retval);
}

/*---------------------------------------------------------------------------
*  int ReleaseDataPacket(t_ctldatapkt* ThePacket, t_lock* lock)
*
*  Description:
*   Conditionally release a data packet to the heap
*
*  Parameters:
*     t_ctldatapkt* ThePacket   pointer to the packet
*     t_lock* lock   Section pointer for lockinh
*
*   Returns
*       Locking counter value after the decrement
*
*  Comments
*   Decrement locking counter. If it does to 0 then free the data pointer
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
int ReleaseDataPacket(t_ctldatapkt* ThePacket, t_lock* lock)
{
    int retval;

    LockSection(lock);            // set a lock
        ThePacket->iUsage -= 1;
#if EASYWINDEBUG & 0
printf("ReleaseDataPacket 0x%08x (0x%08x,%d)\n",ThePacket,ThePacket->pData,ThePacket->iUsage);
#endif
        if(ThePacket->iUsage <= 0)                             // <= 0 means everybody has used it
        {
            free(ThePacket->pData);                 // free the memory
            ThePacket->iUsage = 0;                  // kill the counter
            ThePacket->pData = 0;                   // release the packed
            OutstandingDataPkts -= 1;
        }
    UnLockSection(lock);
    return(retval);
}

/*---------------------------------------------------------------------------
*  t_ctldatapkt* AllocDataPacket(int len, t_lock* lock)
*
*  Description:
*   Open a window
*
*  Parameters:
*     int len                   length
*     t_lock* lock   Section pointer for lockinh
*
*   Returns
*       NULL:   No packet available
*       pointer Packet
*
*  Comments
*   get a data packet from the system. pData does a dual purpose.
*   while it is NULL, the packet is free. So, we first lock
*   and search for a free. If found, we temproarily set the
*   pointer to non-zero as a reservation. Next stuff is done
*   outside the lock.  If malloc fails, the result is NULL
*   which automatically cancels our reservation.
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
t_ctldatapkt* AllocDataPacket(int len, t_lock* lock)
{
    t_ctldatapkt* retval = NULL;
    char*   pTmp;
    int     k;

    LockSection(lock);            // set a lock
        for(k = 0; k < MAXDATAPACKETS; k++)          // search
        {
            retval = &mempktdata[k];
            if(retval->pData == NULL)
            {
                retval->pData = (void*)1;           // mark it
                break;                              // get out
            }
        }
    UnLockSection(lock);

    if(!retval->pData)                              // did we find one?
        return(NULL);                               // nope.

    retval->pData = malloc(len);                    // get memory

    if(!retval->pData)                              // success?
        return(NULL);                               // nope

    retval->nData = len;
    retval->iUsage = 1;                             // usage count to 1
    retval->seqNum = SEQNUM_DEFAULT;                // default sequence number
    retval->SrcId = 0;                              // no source
    retval->ttl = MAXTTL;                           // time to live
    OutstandingDataPkts += 1;
#if EASYWINDEBUG & 0
printf("AllocDataPacket 0x%08x (0x%08x,%d)\n",retval,retval->pData,retval->nData);
#endif

    return(retval);                                 // all done
}

/*
**************************************************************************
*
*  Function: void DiscardCtlData(t_ctlreqpkt* pkIn)
*
*  Purpose: Discard a control packet and release the data packet
*
*  Comments:
*   This is used when the router attempts to send a packet to a
*   destination but the destination is not ready.  For now,
*   the key is the EVB because TCP destinations do not create
*   control packets.  The main window should always be created
*   before and should be ready.  But we might find a case where
*   it is not later
*
**************************************************************************
*/
void DiscardCtlData(t_ctlreqpkt* pCpkt)
{
    t_ctldatapkt* pDpkt = pCpkt->pData;
    ReleaseDataPacket(pDpkt,&gRouterCS);
    // release the control packet
    ReleaseCtlPacket(pCpkt,&gRouterCS);
}


/*---------------------------------------------------------------------------
*  int initDqueue(t_dqueue* hdrQ, t_lock* lock)
*
*  Description:
*   Initializes a Dqueue
*
*  Parameters:
*     t_dqueue* hdrQ                 Pointer to the header
*     t_lock* lock  Section handle for lockinh
*
*   Returns
*       0:      Ok
*       -1:     not Ok
*
*  Comments
*   Dqueues are empty when next and prev bit point to the header.  This is
*   a call (not inline) for critical section protection
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
int initDqueue(t_dqueue* hdrQ, t_lock* lock)
{

    LockSection(lock);
    {
        hdrQ->prev = hdrQ->next = hdrQ;
    }
    UnLockSection(lock);
    return(0);

}


/*---------------------------------------------------------------------------
*  int enqueue(t_dqueue* hdrQ, t_dqueue* newPkt, t_lock* lock)
*
*  Description:
*   Enqueues a request as the last in a double linked list
*
*  Parameters:
*     t_dqueue* hdrQ                 Pointer to the header
*     t_dqueue* newPkt               Pointer to the packet
*     t_lock* lock  Section handle for lockinh
*
*   Returns
*       0:      Ok
*       -1:     not Ok
*
*  Comments
*   Enqueuing a packet at the end means placing it before the header.
*
*   The intergral lock is a nice idea but the locks are not portable.  It
*   would be a nice idea to have some portable locks for code conversion.
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
int enqueue(t_dqueue* hdrQ, t_dqueue* newPkt, t_lock* lock)
{
    int     k;
    int     retval;
    register t_dqueue* q1;
    register t_dqueue* q2;

#if EASYWINDEBUG & 0
// Right now queues hold only control request packets, lets address check
{
printf("enqueue 0x%08x (0x%08x,%d)\n",retval,retval->pData,retval->nData);
}
#endif

    LockSection(lock);
    {
        // get linkage pointers
        q1 = hdrQ->prev;
        // set packet pointers
        newPkt->next = hdrQ;
        newPkt->prev = q1;
        // link it in
        hdrQ->prev  = newPkt;
        q1->next    = newPkt;
    }
    UnLockSection(lock);
    return(0);

}

/*---------------------------------------------------------------------------
*  t_dqueue dequeue(t_dqueue* Pkt, t_lock* lock)
*
*  Description:
*   Removes a packet from a queue
*
*  Parameters:
*     t_dqueue* hdrQ                 Pointer to the header
*     t_lock* lock  Section handle for lockinh
*
*   Returns
*       0:      Ok
*       -1:     not Ok
*
*  Comments
*   This removes the first (next) from a queue. If the queue
*   points to itself, it returns NULL (empty)
*
*   N O T E
*       This is a little different from removing an element. It
*       removes the element following the one in the argument
*       Thus, it assumes the argument is a header.  I have elsewhere implemented
*       the case where the element itself is removed so when
*       "dequeuing" you pass "hdr->next" as the argument.  I'm not sure
*       which is more convienent but beware when cut/paste of sections
*       from other programs.
*
*   The intergral lock is a nice idea but the locks are not portable.  It
*   would be a nice idea to have some portable locks for code conversion.
*
*  History:   Date       Author      Comment
*  24-Feb-2012 jlp
*   initial pass
*
*---------------------------------------------------------------------------*/
t_dqueue* dequeue(t_dqueue* hdrQ, t_lock* lock)
{
    int     k;
    int     retval=NULL;
    register t_dqueue* firstone;
    register t_dqueue* secondone;

    LockSection(lock);
    {
        // checks, (the compilier will optimize)
        if(!hdrQ)
        {
            UnLockSection(lock);
            return(NULL);
        }
        firstone = hdrQ->next;
        if((!firstone) || (firstone == hdrQ))                 // Empty
        {
            UnLockSection(lock);
            return(NULL);
        }
        secondone = firstone->next;

        // link around the first one
        hdrQ->next  = secondone;
        secondone->prev  = hdrQ;

        // isolate the one we removed
        firstone->next = firstone;
        firstone->prev = firstone;

    }
    UnLockSection(lock);
    return(firstone);
}
