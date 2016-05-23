/*
 * ASYCNCW.C
 *
 *  This is a package useful for reading text date from a UART
 *  Binary date is not supported, you have to use the Windows
 *  routines instead.
 *
 * The communication is event driven. When the calling window
 * receives a
 *     WM_COMMAND,WM_COMMNOTIFY,lParam event
 * it should call cgetcbuf(...)
 *
 * when a line is received, the package will send
 *      RxEvent,RxEventId
 *  where both event and event wParam are programmed by the
 *  user in the data structure.
 *
 *
 * Revision History
 *
 * jlp 1/30/2005
 *   Removed all globals and statics and external defines
 *  The routine is now reentrant
 *
 * jlp 1/29/2005
 *      A) Fixed the Overflow problem when the main application
 *         does not get around to servicing
 *      B) Unwired the COM1: 4900,n,8,1
 *          Now you specify The port, rate, and flow control (still n,8,1)
 *  This interface is getting fairly general.
 *  However, there are two problems
 * 1) Only one instance can be run because of the use of global variables
 * 2) The routine needs access to the calling process stuff
 *
 * JLP - 1/1/2005
 *    rewrote to use Borland C++
 * This is wired to COM1: 4800,n,8,1
 *
 *
*/
#define EASYWINDEBUG 1

#include <windows.h>
#if EASYWINDEBUG 
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <alloc.h>
#include "asywin.h"


#define MAXLINES 4
#define MAXCHAR 256

static HGLOBAL hPort;

struct lbufs{
	char a[MAXLINES][MAXCHAR];
};
typedef struct lbufs *  Plbufs;

#if 0
extern HWND mymainwindow;
extern HINSTANCE hInst;	// current instance
static char inputbuf[MAXLINES][MAXCHAR];
static int putline=0;
static int getline=0;
static int idx=0;
#endif

#if EASYWINDEBUG & 1
dcbdump(char *s)
{
    int i;
    for(i=0;i<18;i++)
    {
        printf("%2x ",*s++);
    }
    printf("\n");
}

void jlpdmp(char *p,int len);
#endif

/*
**************************************************************************
*
*  Function: int copen(struct AsynWinComm *ThePort, char *WhichPort, int TheRate, int TheFlow);
*
*  Purpose: Open a COM port
*
*  Comments:
*
*   WhichPort = "COMx:"
*   TheRate = 4800,9600,14400 etc.
*   TheFlow can be none || hardware || software
*
*   The caller is expected to initialize the 5 fields for event information
*
*       TheWindow, HWND to send events to
*       RxEvent, RxEvent ID, the event and the wParam when a line is received
*       ErrorEvent, ErrorEventId the event to send when and error is received
*
*       WM_COMMAND,WM_COMMNOTIFY,8 are send as part of the processing
*
*   Sample call

    TheComPort.TheWindow=mymainwindow;
    TheComPort.RxEvent=EV_MYINTERNAL;
    TheComPort.RxEventId=EVENT_RXLINE;
    TheComPort.ErrorEvent=EV_MYINTERNAL;
    TheComPort.ErrorEventId=EVENT_RXOV;;

    tmp=copen(&TheComPort,"COM1:",4800,0);
    if(tmp<0)
    {
        MessageBox(hWnd,"Check if allready running","Port in Use",
            MB_TASKMODAL|MB_ICONSTOP|MB_OK);
        return(FALSE);
    }
	EnableCommNotification(TheComPort.PortId,mymainwindow,1,-1);
*
*   This should be very close to the last initialization the user does
*   before ready to parse messages because interrupts go live at this time
*
*
**************************************************************************
*/
int copen(struct AsynWinComm *ThePort, char *WhichPort, int TheRate, int TheFlow)
{
    int idComDev;
    int err;
    DCB dcb;

	idComDev = OpenComm(WhichPort, 8192, 128);

	if (idComDev < 0) {
	   return idComDev;
	}

    ThePort->PortId=idComDev;

    GetCommState(idComDev,&dcb);


    dcb.BaudRate=TheRate;       //OpenComm defaults to 9600
    dcb.Parity=NOPARITY;        //OpenComm defaults to even parity
    dcb.StopBits=ONESTOPBIT;
    dcb.ByteSize=8;             //OpenComm defaults to 7 bits

    switch(TheFlow)
    {
        case NOFLOW:
        break;

        case HARDWAREFLOW:
            dcb.fRtsflow=1;
            dcb.fOutxCtsFlow=1;
            dcb.XoffLim=8192-TheRate/100;
            dcb.XonLim=TheRate/100;
        break;
        case SOFTFLOW:
            dcb.fOutX=1;
            dcb.fInX=1;
            dcb.XoffLim=8192-TheRate/100;
            dcb.XonLim=TheRate/100;
        break;
    }

	err = SetCommState(&dcb);
	if (err < 0) {
		return err;
	}

/*
*  set the event to report errors only
*  The driver will still get characters
*/

    GetCommEventMask(idComDev,0x3ff);
    SetCommEventMask(idComDev,(EV_ERR));

//    ThePort->Memory=malloc(sizeof(struct lbufs));
    hPort=GlobalAlloc(GPTR,sizeof(struct lbufs));
    ThePort->Memory=GlobalLock(hPort);

#if EASYWINDEBUG&0
printf("%lx\n",ThePort->Memory);
#endif


    ThePort->getline=0;
    ThePort->putline=0;
    ThePort->idx=0;
	return(idComDev);
}


/*
**************************************************************************
*
*  Function: int cclose(struct AsynWinComm *ThePort);
*
*
*  Purpose: Close the COM port
*
*  Comments:
*
*   This needs to be called not only to close the port but to
*   free dynamically allocated memory
*
*
**************************************************************************
*/
int cclose(struct AsynWinComm *ThePort)
{
	CloseComm(ThePort->PortId);
    GlobalUnlock(hPort);
    GlobalFree(hPort);
//    free(ThePort->Memory);
    return(0);
}

/*
**************************************************************************
*
*  Function: int cgetc(struct AsynWinComm *ThePort);
*
*  Purpose: Get a character
*
*  Comments:
*
*   Normally the user should not call this because it bypasses
*   The line buffering scheme. If it is called, the next line
*   will be missing a character
*
*
**************************************************************************
*/
int cgetc(struct AsynWinComm *ThePort)
{
	int err;
    char c[4];

	err = ReadComm(ThePort->PortId,(void far*) c,1);

	if(err<=0)
		return(-1);
	else
	    return(c[0]);
}

/*
**************************************************************************
*
*  Function: int cputc(struct AsynWinComm *ThePort, char Ch);
*
*  Purpose: Write one character to the port
*
*  Comments:
*
*   This routine will return -1 if the port cannot accept characters
*   There is no cputs for strings. The user has to figure out what
*   to do if the port fills up.
*
*
**************************************************************************
*/
int cputc(struct AsynWinComm *ThePort, char Ch)
{
	int err;
	char c[4];

	c[0]=Ch;

	err = WriteComm(ThePort->PortId,(void far*)c,1);

	if(err<=0)
		return(-1);
	else
	    return(0);
}


/*
**************************************************************************
*
*  Function: void cgetcbuf(struct AsynWinComm *ThePort, unsigned long flag);
*
*  Purpose: Get lines delimited by <LF>
*
*  Comments:
*
*   This routine should be called when a WM_COMMAND,WM_COMMNOTIFY
*   Event is received. Its function is to handle the input line
*   buffering from the UART. To actually get the input lines
*   see cgetnewline(...) below.
*
*   This source is a line oriented (text) module. The input lines
*   are scanned for <LF>. If one is found, the line is terminated
*   with the 0 value. <CR> and <NULL> are stripped from the input
*   Don't try binary transfer with this module.
*
*   Assumptions
*   1) The application process events in order that they arrive
*   in the queue. If not, com: messages must be given higher
*   priority
*
*   2) Upon receiving a WM_COMMNOTIFY one or more characters are
*   buffered up.
*
*   Loop
*       read each character and place it into a line buffer
*       if the character is <LF>
*           Terminate with 0
*           Notify the application
*           Send application a WM_COMNOTIFY which should come back to us
*           return
*       else place the character into the line buffer
*   EndLoop (No more characters)
*
*   This routine executes this algorithm with the use of
*   4 line buffers in a circlar manner and overflow checks
*   in places
*
*
**************************************************************************
*/

void cgetcbuf(struct AsynWinComm *ThePort, unsigned long flag)
{
    int c,e1,e2;
    Plbufs m = (Plbufs)(ThePort->Memory);

    e2=GetCommError(ThePort->PortId,NULL);
    if(e2&CE_RXOVER)
    {
        FlushComm(ThePort->PortId,1);
    	PostMessage(ThePort->TheWindow,ThePort->ErrorEvent,ThePort->ErrorEventId,3); /*overflow */
    }
        
#if EASYWINDEBUG&0
printf("Flag %lx e1 %X\n",flag,e2);
#endif

    if(LOWORD(flag) & CN_EVENT)
    {

#if EASYWINDEBUG&0
e1=GetCommEventMask(ThePort->PortId,0x3ff);
printf("Flag %lx Event %0x Comm %0x\n",flag,e1,e2);
#endif

    	PostMessage(ThePort->TheWindow,ThePort->ErrorEvent,ThePort->ErrorEventId,4); /*overflow */
        return;
     }

    while((c=cgetc(ThePort)) >= 0)
    {
        if(c==0xa) /* LF, generate an event */
        {
            (m->a)[ThePort->putline][ThePort->idx]=0;    /* terminate */
            ThePort->idx=0;
            if(++ThePort->putline>=MAXLINES)
            {
                ThePort->putline=0;
            }

            if(ThePort->putline==ThePort->getline) /* overflow */
            {
				PostMessage(ThePort->TheWindow,ThePort->ErrorEvent,ThePort->ErrorEventId,1); /*overflow */
            }
            else
            {
				PostMessage(ThePort->TheWindow,ThePort->RxEvent,ThePort->RxEventId,0); /* RXLINE */
        		PostMessage(ThePort->TheWindow,WM_COMMAND,WM_COMMNOTIFY,8); /* Tell Me again */
                return;
            }
            
        }
        else if(! ( (c==0xd)|| (c==0x0)) ) /* ignore <CR> and <NULL> */
        {
            (m->a)[ThePort->putline][ThePort->idx++]=c;    /* terminate */
            if(ThePort->idx>=MAXCHAR)
            {
				PostMessage(ThePort->TheWindow,ThePort->ErrorEvent,ThePort->ErrorEventId,2); /*overflow */
                ThePort->idx=0;
                (m->a)[ThePort->putline][MAXCHAR-1]=0;
            }
        }
    }

}

/*
**************************************************************************
*
*  Function: int cgetnewline(struct AsynWinComm *ThePort,char *s);
*
*  Purpose: Read a buffered line
*
*  Comments:
*
*   This should be called when the RxEvent,RxEventId message is received.
*   to copy the next buffered line. There will be at least one line
*   if an error has not occurred. It will return(-1) if there is
*   no line.  At most 3 buffered lines are stored (the 4th is the
*   working line buffer) So you can drop 2 events but this is not
*   recommended.
*
*
*
**************************************************************************
*/
extern int ClockStat, jlptmp1,jlptmp2;
int cgetnewline(struct AsynWinComm *ThePort,char *s)
{
    Plbufs m = (Plbufs)(ThePort->Memory);

    if(ThePort->getline==ThePort->putline)
    {
        return(-1);
    }


     strcpy((char*)(s),(char*)((m->a)[ThePort->getline]));

    if(++ThePort->getline>=MAXLINES)
        ThePort->getline=0;

    return(0);
}

/*
**************************************************************************
*
*  Function: void ckickport(struct AsynWinComm *ThePort)
*
*  Purpose: Kick the comport to stop lock up
*
*  Comments:
*
*
**************************************************************************
*/
void ckickport(struct AsynWinComm *ThePort)
{

    GetCommEventMask(ThePort->PortId,0x3ff);
    if(GetCommError(ThePort->PortId,NULL))
        FlushComm(ThePort->PortId,1);
}

