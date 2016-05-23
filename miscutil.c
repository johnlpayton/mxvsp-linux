#include "jtypes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if USEGDK
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include "w32defs.h"
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


/*
**************************************************************************
*
*  Function: void jlpDumpFrame(char* title, char* pp, int len)
*
*  Purpose: Display a framed packet for debug
*
*  Comments:
*
*
*
**************************************************************************
*/

void jlpDumpFrame(char* title, char* pp, int len)
{
#if (EASYWINDEBUG>0) && 1
    int k;
    unsigned char* dp = pp;

    if(len <= 0)
        return;

    printf("%s: 0x%08x %d ",title, (U32)pp, len);
    if(len<6)
    {
        printf("Short Frame (%d)",len);
        for(k=0;k<len;k++)
            printf("%02x ",dp[k]);
        printf("\n");
    }
    else if( (len<=(MAXDATASIZE+12)))
    {
        printf("Hdr <%02x><%02x><%02x><%02x>",dp[0],dp[1],dp[2],dp[3]);
        for(k=4;k < len-2; k++)
        {
            if( ((k-4)%32)==0)
                printf("\n");
            printf("%02x ",dp[k]);
        }
        printf("Tail <%02x><%02x>\n",dp[k],dp[k+1]);
    }
    else
    {
        printf("Long Frame (%d) <%02x><%02x><%02x><%02x>...<%02x><%02x><%02x><%02x>\n",
        len,
        dp[0],dp[1],dp[2],dp[3],dp[len-4],dp[len-3],dp[len-2],dp[len-1]);
    }

#endif
}
/*
**************************************************************************
*
*  Function: void jlpDumpCtl(char* title, char* pp, int len)
*
*  Purpose: Extract the packet from a ctl frame and dump
*
*  Comments:
*
*
*
**************************************************************************
*/
static char* fromid[]=
{
    "Nobody",
    "Local",
    "EVB",
    "TCP",
    "ILL"
};

void jlpDumpCtl(char* title, t_ctlreqpkt* pp)
{
    t_ctldatapkt* pDpk;
    char*   pChar;
    char*   pFm;
    int     fm;
    int     len;
    int     to;
    int     iuse;

    printf("%s: pp = 0x%06x \n",title,pp);


    // dereference
    fm = pp->from;
    if((fm<0) || (fm>4)) fm = 4;
    pFm     = fromid[fm];
    pDpk    = pp->pData;
    if(pDpk == NULL)
    {
        char tmpstr[64];
        sprintf(tmpstr,"EMPTY ctlreq @pp=0x%08x from = %d(%s)\n",(U32)pp,pp->from,pFm);
        FatalError(tmpstr);
    }
    else
    {
        pChar   = pDpk->pData;
        fm      = pp->from;
        iuse    = pDpk->iUsage;
        to      = pChar[2];
        len     = pDpk->nData;
        printf("pp 0x%08x fm=%d(%s),to 0x%02x, len %d, iuse %d\n",
                (U32)pp,fm,pFm, 0xff&to,len,iuse);
    }
}

/*
**************************************************************************
*
*  Function: void jlpCtlRoute(char* title, char* pp, int len)
*
*  Purpose: Extract the header info
*
*  Comments:
*
*
*
**************************************************************************
*/

void jlpCtlRoute(char* title, t_ctlreqpkt* pp)
{
    t_ctldatapkt* pDpk;
    char*   pChar;
    int     len;

    // dereference
    pDpk = pp->pData;
    pChar = pDpk->pData;
    len = pDpk->nData;
    jlpDumpFrame(title, pChar,len);
}

/*
**************************************************************************
*
*  Function: void jlpCtlRoute(char* title, char* pp, int len)
*
*  Purpose: Extract the header info
*
*  Comments:
*
*
*
**************************************************************************
*/

void jlpChkDataPktHdr(char* title, t_ctldatapkt* pDpk)
{
    char*   pChar;
    int     len;

    // dereference
    if(pDpk->iUsage < 0)
    {
        printf("jlpChkDataPktHdr.2:%s: pDpk 0x%08x iUsage %d\n",title,(U32)pDpk,pDpk->iUsage);
    }
    else if(pDpk->pData)
    {
        printf("jlpChkDataPktHdr.3:%s: pDpk 0x%08x pData 0x%08x len %d\n",
        title, (U32)pDpk, (U32)(pDpk->pData), pDpk->nData);
    }
    else
        printf("jlpChkDataPktHdr.1:%s: pDpk 0x%08x\n",title,(U32)pDpk);
}

/*
**************************************************************************
*
*  Function: void jlpByteStreamOut(char* title, char* pp, int len, FILE* fs)
*
*   write a stream for debug
*
*  Comments:
*
**************************************************************************
*/
extern FILE* StreamDbug;
static int EscapeChar(int n, char* sout)
{
    U8 un = 255&n;
    if(un == 0x10 ){
        strcpy(sout,"<dle>");
    }else if(un==0x01){
        strcpy(sout,"<soh>");
    }else if(un==0x02){
        strcpy(sout,"<stx>");
    }else if(un==0x03){
        strcpy(sout,"<etx>");
    }else if(un==0x08){
        strcpy(sout,"<BS>");
    }else if(un==0x0d){
        strcpy(sout,"<CR>");
    }else if(un==0x0a){
        strcpy(sout,"<LF>");
    }else if(un==0xae){
        strcpy(sout,"<SF>");
    }else if(un==0xab){
        strcpy(sout,"<EF>");
    }else if(un==0xad){
        strcpy(sout,"<DL>");
    }else if(un==0x7f){
        strcpy(sout,".");
    }else if(un<0x20){
        sprintf(sout,"^%c",un-1+'A');
    }else if(un >= 0x80){
        strcpy(sout,".");
    }else{
        sout[0]=un;
        sout[1]=0;
    }

}

void jlpByteStreamOut(char* title, char* pp, int len)
{
    int k,n;
    char tmpbuf[8];

    //======== Not opened
    if(StreamDbug == NULL)
        return;

    //======== Nothing to print
    if(len<=0)
        return;
#if 1
    fprintf(StreamDbug,"%s %4d",title,len);
    for(k=0; k<(len-1); k++)
    {
        fprintf(StreamDbug," %2x",255&pp[k]);
    }
    fprintf(StreamDbug," %2x\n",255&pp[k]);
#else
    fprintf(StreamDbug,"%s %4d ",title,len);
    for(k=0; k<(len-1); k++)
    {
        EscapeChar(pp[k],tmpbuf);
        fprintf(StreamDbug,"%s",tmpbuf);
    }
    EscapeChar(pp[k],tmpbuf);
    fprintf(StreamDbug,"%s\n",tmpbuf);
#endif
}


/*
**************************************************************************
*
*  Function: void FatalOnNullPtr(char* title, void* pPtr)
*
*  Purpose: Throw fata error on nul pointer
*
*  Comments:
*
*
*
**************************************************************************
*/
void FatalOnNullPtr(char* title, void* pPtr)
{
    if(pPtr == NULL)
    {
        printf("FatalOnNullPtr::");
        FatalError(title);
    }
}
/**********************************************************************
 *
 * Read a line
 *
 * int freadLn(char *s, int maxc, FILE* fin)
 *
 * Will read at most maxc characters from the file stream fin.
 * if a character is <CR>, it is tossed
 * if a character is <LF>, it is placed into the buffer
 * if the character if EOF, it is returned
 *
**********************************************************************/
int freadLn(char *s, int maxc, FILE* fin)
{
    int     ch;
    int     nc = 0;
    char*   pChar = s;

    *pChar = 0;
    while(nc < maxc)
    {
        ch = fgetc(fin);

        // **********
        if(ch == EOF)                       // End of the file
        // **********
            return(ch);

        // **********
        if( (ch&0x7f) == 0x0d)              // Skip <CR>
        // **********
            continue;

        // **********
        if( (ch&0x7f) == 0x0a)              // <LF>
        // **********
        {
            *pChar++ = ch;                  // append to the buffer
            *pChar = 0;
            nc += 1;
            return(nc);                     // return the character count
        }

        // **********
        {
            *pChar++ = ch;                  // buffer the character
            nc += 1;
        }
    }
    *pChar = 0;
    return(nc);
}

#if 1
/*
**************************************************************************
*
*  Function: int MessageBox(HWND hWnd, const char* Errstr, const char* title, U32 flags)
*
*  Purpose: Emulate a subset of the Windows MessageBox
*
*  Comments:
*   This dialog box merely places text on the screen then waits for the
*   User to press "Ok" Two lines are printed yo resemble the Windows box
*   But any response is igmored.
*
* Note: the flag GTK_DIALOG_MODAL automatically runs the dialog so I'm
* not sure how the destruction gets done.
* I also put retval in both fields of the connect_swapped because
* I not sure how it works
*
* 1) Yoy have to widget_show
*
**************************************************************************
*/
int MessageBox(HWND hWnd, const char* Errstr, const char* title, U32 flags)
{

    GtkWidget* wval;
    int     retval=0;
    int     selicon;

    switch( flags & 0x00f0)                     // Pick an icon
    {
        case MB_ICONHAND:
            selicon = GTK_MESSAGE_INFO;
        break;
        case MB_ICONQUESTION:
            selicon = GTK_MESSAGE_QUESTION;
        break;
        case MB_ICONASTERISK:
            selicon = GTK_MESSAGE_WARNING;
        break;
        case MB_ICONEXCLAMATION:
            selicon = GTK_MESSAGE_ERROR;
        break;
        default:
            selicon = GTK_MESSAGE_OTHER;
        break;
    }
#if 0       // modal flag is set

    wval = gtk_message_dialog_new(
        (GtkWindow *)hWnd,
        (GtkDialogFlags)(GTK_DIALOG_MODAL+GTK_DIALOG_DESTROY_WITH_PARENT),
        (GtkMessageType)GTK_MESSAGE_WARNING,
        (GtkButtonsType)GTK_BUTTONS_OK,
        "%s\r\n\n%s",title,Errstr);
    g_signal_connect_swapped (wval, "response",
        G_CALLBACK (gtk_widget_destroy), wval);
    gtk_widget_show (wval);                         // must do this
//    gtk_widget_destroy(wval);                     // can't do this

#else       // modal flag is clear I like this because widget has desptoy explicit

    wval = gtk_message_dialog_new(
        (GtkWindow *)hWnd,
        (GtkDialogFlags)(GTK_DIALOG_DESTROY_WITH_PARENT),
        (GtkMessageType)selicon,
        (GtkButtonsType)GTK_BUTTONS_NONE,
        "%s\r\n\n%s",title,Errstr);

    switch( flags & 0x000f)                         // Add the buttons
    {
        default:
        case MB_OK:
            gtk_dialog_add_buttons((GtkDialog*)wval,
                "OK",IDOK,NULL,NULL);
        break;
        case MB_OKCANCEL:
            gtk_dialog_add_buttons((GtkDialog*)wval,
                "OK",IDOK,"CANCEL",IDCANCEL,NULL,NULL);
        break;
        case MB_ABORTRETRYIGNORE:
            gtk_dialog_add_buttons((GtkDialog*)wval,
                "ABORT",IDABORT,"RETRY",IDRETRY,"IGNORE",IDIGNORE,NULL,NULL);
        break;
        case MB_YESNOCANCEL:
            gtk_dialog_add_buttons((GtkDialog*)wval,
                "YES",IDYES,"NO",IDNO,"CANCEL",IDCANCEL,NULL,NULL);
        break;
        case MB_YESNO:
            gtk_dialog_add_buttons((GtkDialog*)wval,
                "YES",IDYES,"NO",IDNO,NULL,NULL);
        break;
        case MB_RETRYCANCEL:
            gtk_dialog_add_buttons((GtkDialog*)wval,
                "RETRY",IDRETRY,"CANCEL",IDCANCEL,NULL,NULL);
        break;
    }
    gtk_widget_show (wval);                         // must do this
    retval = gtk_dialog_run((GtkDialog *)wval);                         // forces modal
    gtk_widget_destroy(wval);                       // and this
#endif
    return ((int)retval);
}
#endif
