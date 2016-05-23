/*******************************************************************************
*   FILE: evbcmds.c
*
*   Most commands are fairly simple. We just sent a string
*   to the evb.
*
*   The evb maps unframed commands to vsp0 (jbug).  So we just
*   send these as unframed
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
#include "framer.h"
#include "muxevb.h"
#include "muxsock.h"
//#include "MTTTY.H"
#include "muxw2ctl.h"


/*
**************************************************************************
*  FUNCTION: gboolean EVBCmdEvent(gpointer pData, GtkWidget* pWidget)
*
*  PURPOSE: Common to send text
*
*  COMMENTS:
*
**************************************************************************
*/
gboolean EVBCmdEvent(gpointer pData, GtkWidget* pWidget)
{
    char    tmptxt[128];
    t_ctldatapkt* pPayload;
    int     n;

    //======== Copy and terminate the text
    strcpy(tmptxt,pData);
    strcat(tmptxt,"\r");
    n = strlen(tmptxt);

    //======== Ship it
    pPayload = ReframeData(tmptxt, n, TID_UNFRAMED, SEQNUM_DEFAULT);
    if(!pPayload)
        FatalError("EVBCmdEvent:AllocDataPacket fail 1");

#if EASYWINDEBUG & 0
printf("EVBCmdEvent 0x%08x ",pPayload->pData);
#endif

    //======== send to the router
    SendFrameToRouter(pPayload,ROUTESRCLOCAL+0x500);

    return TRUE;
}

/*
**************************************************************************
*  FUNCTION: gboolean EVBCmdEventRaw(gpointer pData, GtkWidget* pWidget)
*
*  PURPOSE: Special for IRQ 7
*
*  COMMENTS:
*   for compatability we send two, one to the unframed port ant the second
*   to the evb port
*
**************************************************************************
*/
gboolean EVBCmdEventRaw(gpointer pData, GtkWidget* pWidget)
{
    char    tmptxt[128];
    t_ctldatapkt* pPayload;
    struct S_evbcmds tCmd;
    int     n;

    
    //======== To Unframed
    strcpy(tmptxt,pData);
    n = strlen(tmptxt);

    pPayload = ReframeData(tmptxt, n, TID_UNFRAMED, SEQNUM_DEFAULT);
    if(!pPayload)
        FatalError("EVBCmdEventRaw:AllocDataPacket fail 1");

    SendFrameToRouter(pPayload,ROUTESRCLOCAL+0x500);

    //======== To the UART
    tCmd.len = 4;
    tCmd.msg = EVBCMD_IRQ7;
    tCmd.p1 = 0;
    tCmd.p2 = 0;

    pPayload = ReframeData((char*)&tCmd, sizeof(struct S_evbcmds), TID_EVB, SEQNUM_DEFAULT);
    if(!pPayload)
        FatalError("EVBCmdEventRaw:AllocDataPacket fail 1");

    SendFrameToRouter(pPayload,ROUTESRCLOCAL+0x500);
    
    return TRUE;
}

