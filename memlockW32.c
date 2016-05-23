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
#include "muxvttyW.h"
#include "muxctl.h"
#include "framer.h"
#include "muxevb.h"
#include "muxsock.h"

#if USEWIN32
#include "mttty.h"
#endif

t_lock gRouterCS;                               // Lock for packets abd routing

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
    InitializeCriticalSection(lock);
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
    DeleteCriticalSection(lock);
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
        EnterCriticalSection(lock);
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
        LeaveCriticalSection(lock);
}

/*---------------------------------------------------------------------------
*  FUNCTION HANDLE SpawnThread(int(*Proc)(void*),int StkSize, void* Arg)
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
HANDLE SpawnThread(void (*Proc)(void*),int StkSize, void* Arg)
{
    HANDLE h;
    h = (HANDLE)_beginthread(Proc,StkSize,Arg);
    if( ((int)h) == -1)\
        h = NULL;
    return h;
}

/*---------------------------------------------------------------------------
*  FUNCTION int ExitThread(int iresult)
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
void ThreadReturn(int iresult)
{
    _endthread();
}


/*---------------------------------------------------------------------------
*  FUNCTION void InitCtlMailBox(t_mailbox* pMail)
*
*  Description
*   Initialize control mailbof
*
*  Parameters:
*     t_mailbox* pMail  Mail box pointer
*
*   Returns
*
*
*  Comments
*
*
*  History:   Date       Author      Comment
*  10-Jan-2013 jlp
*   Converging on calls for pprtability
*
*---------------------------------------------------------------------------*/
void InitCtlMailBox(t_mailbox* pMail)
{
    pMail->Sem = CreateSemaphore(NULL,0,MAXCTLPACKETS+100,NULL);
    initDqueue(&(pMail->Hdr.link),NULL);
}

/*---------------------------------------------------------------------------
*  FUNCTION t_ctlreqpkt* AcceptCtlMail(t_mailbox* pMail)
*
*  Description
*   Get the next t_ctlreqpkt from the mail box
*
*  Parameters:
*     t_mailbox* pMail  Mail box pointer
*
*   Returns
*
*  Comments
*   Will wait until a message arrives
*
*  History:   Date       Author      Comment
*  10-Jan-2013 jlp
*   Converging on calls for pprtability
*
*---------------------------------------------------------------------------*/
t_ctlreqpkt* AcceptCtlMail(t_mailbox* pMail)
{
    DWORD dwRes;
    t_ctlreqpkt* qpkIn;
    t_dqueue* qe;

    while(1)
    {
        dwRes = WaitForSingleObject(pMail->Sem,INFINITE);
        if(dwRes == WAIT_FAILED)
        {
            FatalError("AcceptCtlMail: wait failed");
        }

        qe = dequeue(&(pMail->Hdr.link),&gRouterCS);
        if(qe)
        {
            qpkIn = structof(t_ctlreqpkt,link,qe);
            break;
        }
    }
    return(qpkIn);
}

/*---------------------------------------------------------------------------
*  FUNCTION void ForwardCtlMail(t_mailbox* pMail, t_ctlreqpkt* pCtl)
*
*  Description
*   Enqueue a packet in a mail box and notify the receipant
*
*  Parameters:
*     t_mailbox* pMail  Mail box pointer
*
*   Returns
*
*  Comments
*   Will wait until a message arrives
*
*  History:   Date       Author      Comment
*  10-Jan-2013 jlp
*   Converging on calls for pprtability
*
*---------------------------------------------------------------------------*/
void ForwardCtlMail(t_mailbox* pMail, t_ctlreqpkt* pSend)
{
    int k;
    
    enqueue(&(pMail->Hdr.link),&(pSend->link),&gRouterCS); // link it in
    ReleaseSemaphore(pMail->Sem,1,(LPLONG)&k);      // sigal arrival
}

/*---------------------------------------------------------------------------
*  FUNCTION void FlushCtlMail(t_mailbox* pMail)
*
*  Description
*   Flush out a mail box and release resources
*
*  Parameters:
*     t_mailbox* pMail  Mail box pointer
*
*   Returns
*
*  Comments
*   Utility for an exiting thread
*
*  History:   Date       Author      Comment
*  10-Jan-2013 jlp
*   Converging on calls for pprtability
*
*---------------------------------------------------------------------------*/
void FlushCtlMail(t_mailbox* pMail)
{
    int k;
    t_dqueue* qe;
    

#if EASYWINDEBUG && 1
if(inicfg.prflags & PRFLG_ROUTER)
        {
printf("FlushCtlMail\n");
    }
#endif

    while(1)
    {
        t_dqueue*       pq;
        t_ctlreqpkt*    txctlpkt;

        pq = dequeue(&(pMail->Hdr.link),&gRouterCS);
        if(pq)
            break;
        txctlpkt = structof(t_ctlreqpkt,link,pq); // get the control packet
        DiscardCtlData(txctlpkt);      // release the control packet

}
    CloseHandle(pMail->Sem);
}
