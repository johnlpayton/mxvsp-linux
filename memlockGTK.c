/*******************************************************************************
*   FILE: memlockW32.c
*
*   Provide locking for WIN32
*   Memory and buffer management.
*
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
//#include "mttty.h"
#include "muxctl.h"
//#include "framer.h"
//#include "muxevb.h"
//#include "muxsock.h"
//#include "MTTTY.H"

t_lock gRouterCS;                               // Lock for packets abd routing

//pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/*--------------------------------------------------------------------------
*  FUNCTION t_lock* InitLockSection(t_lock* lock)
*
*  Description
*   Create/initialize a lock
*
*  Parameters:
*     t_lock* lock  Section pointer for locking
*
*   Returns
*       pointer to be used in locking
*
*  Comments
*       For now, the memory used must be assigned abd type cast
*       my the caller.  We do not allow small chunks of memory
*       allocated with malloc.
*
*       In WIN32 this is purely initialization
*
*  History:   Date       Author      Comment
*  08-Dec-2012 jlp
*   Typedef and other conversion to ease portability
*
*---------------------------------------------------------------------------*/
t_lock* InitLockSection(t_lock* lock)
{
    g_rec_mutex_init(lock);
    return(lock);
}

/*--------------------------------------------------------------------------
*  FUNCTION t_lock* FreeLockSection(t_lock* lock)
*
*  Description
*   Free/Discard a lock
*
*  Parameters:
*     t_lock* lock  Section pointer for locking
*
*   Returns
*       pointer to be used in locking
*
*  Comments
*       In WIN32 we call a discard.  I have no idea
*       what it does if threads are pending.
*
*  History:   Date       Author      Comment
*  08-Dec-2012 jlp
*   Typedef and other conversion to ease portability
*
*---------------------------------------------------------------------------*/
t_lock* FreeLockSection(t_lock* lock)
{
//    g_rec_mutex_clear(lock);
// dont clear static mutex
    return(lock);
}

/*--------------------------------------------------------------------------
*  FUNCTION void LockSection(t_lock* lock)
*
*  Description
*   Lock a section
*
*  Parameters:
*     t_lock* lock           Section pointer for locking
*
*   Returns
*
*
*  Comments
*   Conditionally lock a section with a counting lock.  Under
*   WIN32 we can use the CRITICAL_SECTION mechanism
*
*   If the pointer is NULL, no lock is placed.  Otherwise we
*   assume the thread is stalled until the lock is released. Then
*   we lock it and can use the area
*
*
*  History:   Date       Author      Comment
*  08-Dec-2012 jlp
*   Typedef and other conversion to ease portability
*
*---------------------------------------------------------------------------*/
void LockSection(t_lock* lock)
{
    if(lock)
        g_rec_mutex_lock(lock);
/*
    pthread_mutex_lock(lock);
*/
}

/*---------------------------------------------------------------------------
*  FUNCTION void UnLockSection(t_lock* lock)
*
*  Description
*   UnLock a section
*
*  Parameters:
*     t_lock* lock           Section pointer for locking
*
*   Returns
*
*
*  Comments
*   Conditionally unlock a section with a counting lock.  Under
*   WIN32 we can use the CRITICAL_SECTION mechanism
*
*   If the pointer is NULL, no lock was placed. Otherwise, we
*   release the lock (and any waiting thread).  This must be
*   used in balance with LockSection
*
*
*  History:   Date       Author      Comment
*  08-Dec-2012 jlp
*   Typedef and other conversion to ease portability
*
*---------------------------------------------------------------------------*/
void UnLockSection(t_lock* lock)
{
    if(lock)
        g_rec_mutex_unlock(lock);
/*
    pthread_mutex_unlock(lock);
*/
}

/*---------------------------------------------------------------------------
*  FUNCTION HANDLE SpawnThread(int(*Proc)(void*),int StkSize, void* Arg)
*
*  Description
*   Spawn a thread
*
*  Parameters:
*   Proc      Thread pointer to start
*   StkSize     Not used
*   Arg         Pointer to arguments
*
*   Returns
*
*
*  Comments
*
*
*  History:   Date       Author      Comment
*  08-Dec-2012 jlp
*   Typedef and other conversion to ease portability
*
*---------------------------------------------------------------------------*/
HANDLE SpawnThread(void (*Proc)(void*),int StkSize, void* Arg)
{
    HANDLE h;
    h = (HANDLE)g_thread_try_new("thread",(GThreadFunc)Proc,Arg,NULL);
    return h;
}
/*
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine) (void *), void *arg);
 pthread_t id;
 pthread_attr_t attr;
 int e;

 e = pthread_attr_init(&attr);
e=pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
e=pthread_create(&id, &attr, Proc, Arg);
  handle errors in "e", errno is not used for pthreads

  return( (HANDLE)id);
*/

/*---------------------------------------------------------------------------
*  FUNCTION int ThreadReturn(int iresult)
*
*  Description
*   Wxit a thread
*
*  Parameters:
*     int   iresult     no used
*
*   Returns
*
*
*  Comments
*
*  History:   Date       Author      Comment
*  08-Dec-2012 jlp
*   Typedef and other conversion to ease portability
*
*---------------------------------------------------------------------------*/
void ThreadReturn(int iresult)
{
    g_thread_exit(&iresult);
//    pthread_exit(0);
}


/*---------------------------------------------------------------------------
*  FUNCTION int WaitOnSemaphore(int t_semaphore* pSem)
*
*  Description
*   Put a thread into waiting
*
*  Parameters:
*     t_lock* lock           Section pointer for locking
*
*   Returns
*
*
*  Comments
*   For now, we will just use g_cond_XX stuff and not attempt to code
*   counting semaphores.  The use is
*
*  History:   Date       Author      Comment
*  08-Dec-2012 jlp
*   Typedef and other conversion to ease portability
*
*---------------------------------------------------------------------------*/
void InitSemaphore(t_semaphore* pSem)
{
    g_cond_init(&(pSem->cnd));
    g_mutex_init(&(pSem->mx));
    pSem->count = 0;
}

int SignalSemaphore(t_semaphore* pSem)
{
    g_mutex_lock(&(pSem->mx));
    pSem->count = 1;
    g_cond_signal(&(pSem->cnd));
    g_mutex_unlock(&(pSem->mx));
}

int WaitOnSemaphore(t_semaphore* pSem)
{
    g_mutex_lock(&(pSem->mx));
    while(pSem->count <= 0)
    {
        g_cond_wait(&(pSem->cnd),&(pSem->mx));

    }
    pSem->count = 0;
    g_mutex_unlock(&(pSem->mx));

}

/*---------------------------------------------------------------------------
*  MAILBOX FUNCTIONS
*
*  FUNCTION int InitMailBox(...)
*  FUNCTION int PostMail(...)
*  FUNCTION int ReadMail(...)
*  FUNCTION int FlushMail(...)
*
*  Description
*   Routines to handle interThread auueues
*       InitMailBox:        Initialzs
*       PostMail:           Enqueue a letter
*       WatForMail:         Wait on empty mail box
*       ReadMailBox:        Read
*
*  Parameters:
*     t_lock* lock           Section pointer for locking
*
*   Returns
*
*
*  Comments
*   This group of routines handle communications via queued messages. Conceptually,
*   a Unix pipe is similiar but it operates with processes.  There may be
*   a standardized construct for threads but I don't know what it is. So this
*   group of routines builds the utilities using mutex and semaphore constructs.
*   Note: under Win32, each thread has an input "MessageQueue" so these routines
*   are not really needed.  In Gtk (it seems) thet are neded.  We will use
*   them for callback routines to send completion mail to a thread because it is
*   not clear what stackspace the callback executes from.  Having the routines,
*   we will expand on them to allow other threads to also send ordered messages.
*
*   We want to revisit this.  The initial pass is just consolidation of some
*   common code from key areas.
*
*  History:   Date       Author      Comment
*  08-Dec-2012 jlp
*   Typedef and other conversion to ease portability
*
*---------------------------------------------------------------------------*/
void InitCtlMailBox(t_mailbox* pMail)
{
    g_cond_init(&(pMail->cnd));
    g_mutex_init(&(pMail->mx));
    initDqueue(&(pMail->link),NULL);
#if (EASYWINDEBUG>4) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
printf("InitCtlMailBox 0x%06x\n",pMail);
}
#endif
}

void FlushCtlMail(t_mailbox* pMail)
{
    int k;
    t_dqueue* qe;


#if (EASYWINDEBUG>4) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
printf("FlushCtlMail 0x%06x\n",pMail);
}
#endif

    g_mutex_lock(&(pMail->mx));
    while(1)
    {
        t_dqueue*       pq;
        t_ctlreqpkt*    txctlpkt;
        t_ctldatapkt*   txdatapkt;

        pq = dequeue(&(pMail->link),NULL);
        if(pq)
            break;
        txctlpkt = structof(t_ctlreqpkt,link,pq); // get the control packet
        if(txctlpkt->tag == CTLREQMSGTAG_PDATAREQ)
        {
            txdatapkt = txctlpkt->pData;
            ReleaseDataPacket(txdatapkt,&gRouterCS); // Release old
        }
        ReleaseCtlPacket(txctlpkt,&gRouterCS);      // release the control packet

    }
    g_mutex_unlock(&(pMail->mx));
}

t_ctlreqpkt* AcceptCtlMail(t_mailbox* pMail)
{
    int k;
    t_dqueue* qe;
    t_ctlreqpkt* retval;

#if (EASYWINDEBUG>4) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
printf("AcceptCtlMail:in pMail= 0x%06x\n", pMail);
//jlpDumpCtl("ReadMail");

}
#endif

    g_mutex_lock(&(pMail->mx));
    while(1)
    {
        qe = dequeue(&(pMail->link),NULL);
        if(qe)
            break;
        g_cond_wait(&(pMail->cnd),&(pMail->mx));

    }
    g_mutex_unlock(&(pMail->mx));


    retval = (t_ctlreqpkt*)structof(t_ctlreqpkt,link,qe);
#if (EASYWINDEBUG>4) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
printf("AcceptCtlMail:out retval= 0x%06x\n", retval);
//jlpDumpCtl("ReadMail");

}
#endif
    return retval;
}

void ForwardCtlMail(t_mailbox* pMail, t_ctlreqpkt* pCtl)
{
    int k;
    t_dqueue* qe;

#if (EASYWINDEBUG>4) && 1
if(inicfg.prflags & PRFLG_ROUTER)
{
jlpDumpCtl("ForwardCtlMail",pCtl);
}
#endif

    g_mutex_lock(&(pMail->mx));
    enqueue(&(pMail->link),&(pCtl->link),NULL); // link it in
    g_cond_signal(&(pMail->cnd));
    g_mutex_unlock(&(pMail->mx));
    return;
}

/*---------------------------------------------------------------------------
*  FUNCTION U32 BMEventWait(t_BMevent* pEv, U32 enab, U32 tsec)
*
*  Description
*   Wait on a (timed) event
*
*  Parameters:
*     t_BMevent*    pEv,        Pointer to a t_BMevent
*     U32           enab        Bit mask for enables
*     U32           tsec        Time out (0 == never)
*
*   Returns
*       The event that was pending
*       0 on a timeout
*       SPURIOUS_INTERRUPT
*
*  Comments
*   Multiple events are defined by a 31 bit enable field.  The condition variable
*   to wait on (g_cond) is shared in a logical .OR. If any enabled event is pending,
*   the routine returns with the event (integer bit map with one bit set).
*   If no pending events are found, the routine will wait for a g_signal to
*   set one of the events.  A g_signal may set a disabled event.  In this case,
*   the logic remembers (stcky bit) the event but stays in the internal wait loop.
*   The tests are always done on the sticky bit. So it is possible to wait on one
*   (or more) bit, exit and wait on different bits that may have been set
*   previously.
*
*   On detection any of the enabled bits, the routine will scan from the LSB
*   higher for the first (enabled) bit that is set.  It will reset the bit and return
*   the 32bit indetifier.  If detection fails it returns SPURIOUS_INTERRUPT (2^31).
*   This should be ignored and the wait retried.  It is not clear why it can occur
*   but Gtk warns of a race condition in their own software.
*
*   Timed returns are possible if the argument tsec (32bits) is not 0.  The
*   routine wull return 0 if a timeout occurs before any enabled events
*   occur.
*
*   These appear to be based on the pthread library.  I don't know if the
*   32 bit event thing works.
*
*  History:   Date       Author      Comment
*  08-Dec-2012 jlp
*   Typedef and other conversion to ease portability
*
*---------------------------------------------------------------------------*/
U32 BMEventWait(t_BMevent* pEv, U32 enab, U32 tsec)
{
    U32     k;
    U32     ae;
    U64     asec;


    g_mutex_lock(&(pEv->e_mx));

    //========
    if(tsec > 0)                                // timed wait?
    //========
    {
        asec = g_get_monotonic_time () + tsec * G_TIME_SPAN_SECOND;
        while(!(ae = (enab & pEv->e_pend)))
        {
            k=g_cond_wait_until(&(pEv->e_cond), &(pEv->e_mx), asec);
            if(!k)                              // timeout
            {
                g_mutex_unlock(&(pEv->e_mx));
                return(0);
            }
        }
    }
    //========
    else                                        // nope
    //========
    {
        // Wait for one of the bits to be set
        while(!(ae = (enab & pEv->e_pend)))
        {
            g_cond_wait(&(pEv->e_cond), &(pEv->e_mx));
        }
    }

    // dispatch the event
    k = 1;
    while(k)
    {
        if(k & ae)
        {
            pEv->e_pend &= ~k;
            g_mutex_unlock(&(pEv->e_mx));
            return(k);
        }
        k <<= 1;
    }

    g_mutex_unlock(&(pEv->e_mx));
    return SPURIOUS_INTERRUPT;                  // Spurious interrupt
}

/*---------------------------------------------------------------------------
*  FUNCTION void BMEventSet(t_BMevent* pEv, U32 ev)
*
*  Description
*   Wait on a (timed) event
*
*  Parameters:
*     t_BMevent*    pEv,        Pointer to a t_BMevent
*     U32           ev          Bit mask for the event
*
*   Returns
*
*  Comments
*   Multiple events are defined by a 31 bit enable field.  This is the
*   companion to BMEventWait. The argument ev is a bit map (31 bits)
*   of the event.  It is first .OR. ed int the pending register then
*   g_cond is set.  Enable logic (mask) is only applied by the companion
*   BMEventWait routine so this will always cause a wakeup.
*
*   It is possible to set more than one event.  It is not clear how
*   useful that is.
*
*
*  History:   Date       Author      Comment
*  08-Dec-2012 jlp
*   Typedef and other conversion to ease portability
*
*---------------------------------------------------------------------------*/
void BMEventSet(t_BMevent* pEv, U32 ev)
{
    U32     k;

    // Lock the structure
    g_mutex_lock(&(pEv->e_mx));
    pEv->e_pend |= ev;
    g_cond_signal(&(pEv->e_cond));
    g_mutex_unlock(&(pEv->e_mx));
    return;
}

/*---------------------------------------------------------------------------
*  FUNCTION void BMEventInit(t_BMevent* pEv)
*
*  Description
*   Initialize a t_BMevent
*
*  Parameters:
*     t_BMevent*    pEv,        Pointer to a t_BMevent
*
*   Returns
*
*  Comments
*
*
*  History:   Date       Author      Comment
*  08-Dec-2012 jlp
*   Typedef and other conversion to ease portability
*
*---------------------------------------------------------------------------*/
void BMEventInit(t_BMevent* pEv)
{
    g_mutex_init(&(pEv->e_mx));
    g_cond_init(&(pEv->e_cond));
    pEv->e_pend = 0;
}
