/*-----------------------------------------------------------------------------

    MODULE: Init.c

    PURPOSE: Intializes global data and comm port connects.
                Closes comm ports and cleans up global data.

    FUNCTIONS:
        GlobalInitialize  - Init global variables and system objects
        GlobalCleanup     - cleanup global variables and system objects

-----------------------------------------------------------------------------*/

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
#include "muxw2ctl.h"
#include "framer.h"
#include "muxevb.h"
#include "muxsock.h"

#if USEWIN32
#include "mttty.h"
#endif

extern HFONT ghFontVTTY;
extern t_windowmap TTYWindows[];
extern HFONT CreateVTTYFont(void);

/*
    Prototypes for functions called only within this file
*/

#define USEINIFILE 1

void StartThreads( void );
DWORD WaitForThreads( DWORD );
#if USEWIN32
BOOL CALLBACK _export DlgDlSrecProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

#if USEWIN32
void DownLoadInit(char* iniPath, int select);
/*
    TimeoutsDefault
        We need ReadIntervalTimeout here to cause the read operations
        that we do to actually timeout and become overlapped.
        Specifying 1 here causes ReadFile to return very quickly
        so that our reader thread will continue execution.
*/
COMMTIMEOUTS gTimeoutsDefault = { 4, 1000, 1000, 1000, 1000 };
#endif

extern char iniPath[256];


/*-----------------------------------------------------------------------------

FUNCTION: Interface translators to use the downloaded source for INI files

PURPOSE:

COMMENTS:


HISTORY:   Date:      Author:     Comment:
           2012-Sep-12   jlp      Wrote it

-----------------------------------------------------------------------------*/
#if USEGDK
extern int ini_getl(char*, char*, int, char*);
extern int ini_gets(char*, char*, char*, char*, int, char*);
extern int ini_putl(char*, char*, int, char*);
extern int ini_puts(char*, char*, char*, char*);

static int GetPrivateProfileInt(
        char*   SecName,
        char*   ParmName,
        int     defval,
        char*   iniPath)
{
    return ini_getl(
        SecName,
        ParmName,
        defval,
        iniPath);
}

static int GetPrivateProfileString(
        char*   SecName,
        char*   ParmName,
        char*   defval,
        char*   pVal,
        int     lenMax,
        char*   iniPath)
{
    return ini_gets(
            SecName,
            ParmName,
            defval,
            pVal,
            lenMax,
            iniPath);
}


int WritePrivateProfileString(
        char*   SecName,
        char*   ParmName,
        char*   pVal,
        char*   iniPath)
{
    return ini_puts(
            SecName,
            ParmName,
            pVal,
            iniPath);
}

#endif


/*-----------------------------------------------------------------------------

FUNCTION: void DoMSVPIni(char* ininame, int save_load)

PURPOSE: Initializes MSVP globals before command line

COMMENTS: If an ini file is present, it uses it


HISTORY:   Date:      Author:     Comment:
           2012-Sep-12   jlp      Wrote it

-----------------------------------------------------------------------------*/

void DoMSVPIni(char* iniPath, int save_load)
{
    FILE*   fini;
    char    tmpStr[256];
    char    SecName[64];
    char    ParmName[64];
    int     i,k;

// default ini stuff Command line can change
    if(save_load)  // temporary for now
    {
    inicfg.muxmode      = MUXMODECLIENT;
    inicfg.SrvrWinMode  = 1;                    // server only creates the VSP0 window
//    inicfg.SrvrWinMode  = 2;                    // server creates all windows but hides them
//    inicfg.SrvrWinMode  = 3;                    // server creates and displays windows
    inicfg.EvbMode      = 1;                    // Evable search for an EVB
    inicfg.port         = 1;                    // COM1 for the EVB
    inicfg.brate        = 19200;                // 19200
    inicfg.flow         = 2;                    // no flow control
    inicfg.TTYHeight    = 248;                  // default TTY sixe
    inicfg.TTYWidth     = 450;
    inicfg.maxwin       = 8;                    // Max tty windows (actually set at compile time)
    inicfg.lineend      = UART_LINEEND_CR;      // <CR> ends a line sent to the EVB
    inicfg.autolapj     = 0;
    strcpy(inicfg.NameSrvr,"DOPEY");
    inicfg.PortTCP      = 3283;                 // Appletalk remote desktop
    strcpy(inicfg.editfname,"mxvspout.txt");
    }

    inicfg.prflags = 0 +
         0*PRFLG_TCP +
         1*PRFLG_EVB +
         0*PRFLG_ROUTER +
         0*PRFLG_WINMUX +
         0*PRFLG_VSP +
         0*PRFLG_REGISTER +
         0*PRFLG_OTHER +
         1*PRFLG_TCP +
         1*PRFLG_LAPJ +
         0;

// process section [MUX]
    strcpy(SecName,"MUX");

    strcpy(ParmName,"muxmode");
    if(save_load)
    {
        inicfg.muxmode=GetPrivateProfileInt(
            SecName,
            ParmName,
            MUXMODEDIRECT,
            iniPath);
    }
    else
    {
        sprintf(tmpStr,"%d",inicfg.muxmode);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }

    strcpy(ParmName,"SrvrWinMode");
    if(save_load)
    {
        inicfg.SrvrWinMode=GetPrivateProfileInt(
            SecName,
            ParmName,
            1,
            iniPath);
    }
    else
    {
        sprintf(tmpStr,"%d",inicfg.SrvrWinMode);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }

    strcpy(ParmName,"EvbMode");
    if(save_load)
    {
        inicfg.EvbMode=GetPrivateProfileInt(
            SecName,
            ParmName,
            1,
            iniPath);
    }
    else
    {
#if 0   // don't write evbmode
        sprintf(tmpStr,"%d",inicfg.EvbMode);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
#endif
    }

    strcpy(ParmName,"NameSrvr");
    if(save_load)
    {
        GetPrivateProfileString(
            SecName,
            ParmName,
            "DOPEY",
            inicfg.NameSrvr,
            96,
            iniPath);
    }
    else
    {
        strcpy(tmpStr,inicfg.NameSrvr);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }

    strcpy(ParmName,"PortTCP");
    if(save_load)
    {
        inicfg.PortTCP=GetPrivateProfileInt(
            SecName,
            ParmName,
            3283,
            iniPath);
    }
    else
    {
        sprintf(tmpStr,"%d",inicfg.PortTCP);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }

// process section [UART]
    strcpy(SecName,"UART");

    strcpy(ParmName,"port");
    if(save_load)
    {
        inicfg.port=GetPrivateProfileInt(
            SecName,
            ParmName,
            1,
            iniPath);
    }
    else
    {
        sprintf(tmpStr,"%d",inicfg.port);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }

    strcpy(ParmName,"brate");
    if(save_load)
    {
        inicfg.brate=GetPrivateProfileInt(
            SecName,
            ParmName,
            19200,
            iniPath);
    }
    else
    {
        sprintf(tmpStr,"%d",inicfg.brate);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }

    strcpy(ParmName,"flow");
    if(save_load)
    {
        inicfg.flow=GetPrivateProfileInt(
            SecName,
            ParmName,
            2,
            iniPath);
    }
    else
    {
        sprintf(tmpStr,"%d",inicfg.flow);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }

    strcpy(ParmName,"lineend");
    if(save_load)
    {
        inicfg.lineend=GetPrivateProfileInt(
            SecName,
            ParmName,
            UART_LINEEND_CR,
            iniPath);
    }
    else
    {
        sprintf(tmpStr,"%d",inicfg.lineend);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }

    strcpy(ParmName,"autolapj");
    if(save_load)
    {
        inicfg.autolapj=GetPrivateProfileInt(
            SecName,
            ParmName,
            UART_LINEEND_CR,
            iniPath);
    }
    else
    {
        sprintf(tmpStr,"%d",inicfg.autolapj);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }
// process section [VTTYWIN]
    strcpy(SecName,"VTTYWIN");
    /** Leave at defaults for now **/

// process section [EVBSTR]
    strcpy(SecName,"EVBSTR");

    strcpy(ParmName,"GF80000");
    if(save_load)
    {
        GetPrivateProfileString(
            SecName,
            ParmName,
            "g 0xf80000",
            inicfg.GF80000,
            64,
            iniPath);
    }
    else
    {
        strcpy(tmpStr,inicfg.GF80000);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }

    strcpy(ParmName,"GFLASH");
    if(save_load)
    {
        GetPrivateProfileString(
            SecName,
            ParmName,
            "g 0xfff0011c",
            inicfg.GFLASH,
            64,
            iniPath);
    }
    else
    {
        strcpy(tmpStr,inicfg.GFLASH);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }

// process section [USERSTR]
    strcpy(SecName,"USERSTR");

    strcpy(ParmName,"userstr1");
    if(save_load)
    {
        GetPrivateProfileString(
            SecName,
            ParmName,
            "ds",
            inicfg.userstr1,
            64,
            iniPath);
    }
    else
    {
        strcpy(tmpStr,inicfg.userstr1);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }

    strcpy(ParmName,"userstr2");
    if(save_load)
    {
        GetPrivateProfileString(
            SecName,
            ParmName,
            "md k_tasktbl 28",
            inicfg.userstr2,
            64,
            iniPath);
    }
    else
    {
        strcpy(tmpStr,inicfg.userstr2);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }

    strcpy(ParmName,"userstr3");
    if(save_load)
    {
        GetPrivateProfileString(
            SecName,
            ParmName,
            "md k_devtbl+64*46 64",
            inicfg.userstr3,
            64,
            iniPath);
    }
    else
    {
        strcpy(tmpStr,inicfg.userstr3);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }

    strcpy(ParmName,"userstr4");
    if(save_load)
    {
        GetPrivateProfileString(
            SecName,
            ParmName,
            "ds",
            inicfg.userstr4,
            64,
            iniPath);
    }
    else
    {
        strcpy(tmpStr,inicfg.userstr4);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }
// process section [MISC]
    strcpy(SecName,"MISC");

    strcpy(ParmName,"editfname");
    if(save_load)
    {
        GetPrivateProfileString(
            SecName,
            ParmName,
            "mxvspout.txt",
            inicfg.editfname,
            255,
            iniPath);
    }
    else
    {
        strcpy(tmpStr,inicfg.editfname);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }

    strcpy(ParmName,"DLfilename");
    if(save_load)
    {
        GetPrivateProfileString(
            SecName,
            ParmName,
            "kgps.s37",
            inicfg.DLfilename,
            255,
            iniPath);
    }
    else
    {
        strcpy(tmpStr,inicfg.DLfilename);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }

    strcpy(ParmName,"ULfilename");
    if(save_load)
    {
        GetPrivateProfileString(
            SecName,
            ParmName,
            "t.bin",
            inicfg.ULfilename,
            256,
            iniPath);
    }
    else
    {
        strcpy(tmpStr,inicfg.ULfilename);
        WritePrivateProfileString(
            SecName,
            ParmName,
            tmpStr,
            iniPath);
    }
}


/*
**************************************************************************
*
*  Function: static void prhlp(void)
*
*  Purpose: Print command line help summary
*
*  Comments:
*
*  Does not work in windows.  There is no console.
*
*  We'll leave it here for documentation.  Maybe later a dialog or a help
*  menu might be ok.
*
**************************************************************************
*/
static void prhlp(void)
{
    printf("mxvsp [options]\n");
    printf("-c      : client mode (remote TCP/IP) (default)\n");
    printf("-client : client mode\n");
    printf("-s      : server mode (connected to EVB)\n");
    printf("-server : server mode\n");
    printf("-d      : direct mode (used as a TTY, no network)\n");
    printf("-direct : direct mode\n");
    printf("    Note: server and direct modes must have a COM port or use the -noevb switch\n");
    printf("-noevb  : EVB (TTY) is a simulated loopback for development\n");
    printf("-cr     : line ends to the EVB are <CR>, default\n");
    printf("-lf     : line ends to the EVB are <LF>\n");
    printf("-crlf   : line ends to the EVB are <CR><LF>\n");
    printf("-host <name> : Network name of the host\n");
    printf("-dopey  : host network name is dopey\n");
    printf("-happy  : host network name is happy\n");

}
/*
**************************************************************************
*
*  Function: int QueueCommandLine(HWND hWnd, HINSTANCE hInstance);
*
*  Purpose: Queue off message based on command ling parsing
*
*  Comments:
*
*
**************************************************************************
*/
int QueueCommandLine(int _argc, char** _argv)
{
/*
*---
* Here is the interface to C based command line stuff
* Borland provides the globals _argc and _argv
*---
*/
    int i,k,file;
    int argp;
    char tmpStr[96];

    argp=1;         // skip the first arg (the command text itself)

    while(argp < _argc)   // scan arguments
    {

        if(strcmpi(_argv[argp],"-s") == 0)              // set server mode (alias)
        {
            inicfg.muxmode = MUXMODESERVER;
        }
        else if(strcmpi(_argv[argp],"-server") == 0)    // set server mode
        {
            inicfg.muxmode = MUXMODESERVER;
        }
        else if(strcmpi(_argv[argp],"-c") == 0)         // set client mode (alias)
        {
            inicfg.muxmode = MUXMODECLIENT;
        }
        else if(strcmpi(_argv[argp],"-client") == 0)    // set client mode
        {
            inicfg.muxmode = MUXMODECLIENT;
        }
        else if(strcmpi(_argv[argp],"-d") == 0)         // set direct mode (alias)
        {
            inicfg.muxmode = MUXMODEDIRECT;
        }
        else if(strcmpi(_argv[argp],"-direct") == 0)    // set direct mode
        {
            inicfg.muxmode = MUXMODEDIRECT;
        }
        else if(strcmpi(_argv[argp],"-loop") == 0)    // set direct mode
        {
            inicfg.muxmode = MUXMODELOOP;
        }
        else if(strcmpi(_argv[argp],"-noevb") == 0)    // simulate evb
        {
            inicfg.EvbMode = 0;
        }
        else if(strcmpi(_argv[argp],"-crlf") == 0)     // add <CR><LF> to line ends
        {
            inicfg.lineend = UART_LINEEND_CRLF;
        }
        else if(strcmpi(_argv[argp],"-cr") == 0)     // add <CR> to line ends
        {
            inicfg.lineend = UART_LINEEND_CR;
        }
        else if(strcmpi(_argv[argp],"-lf") == 0)     // add <LF> to line ends
        {
            inicfg.lineend = UART_LINEEND_LF;
        }
        else if(strcmpi(_argv[argp],"-lapj") == 0)    // attempt lapj
        {
            inicfg.autolapj = 1;
        }
        else if(strcmpi(_argv[argp],"-nolapj") == 0)    // don't attempt lapj
        {
            inicfg.autolapj = 0;
        }
        else if(strcmpi(_argv[argp],"-host") == 0)      // set host name
        {
            argp += 1;
            strcpy(inicfg.NameSrvr,_argv[argp]);
        }
        else if(strcmpi(_argv[argp],"-dopey") == 0)     // host name = dopen
        {
            strcpy(inicfg.NameSrvr,"DOPEY");
        }
        else if(strcmpi(_argv[argp],"-HAPPY") == 0)     // host name = happy
        {
            strcpy(inicfg.NameSrvr,"HAPPY");
        }
        else if(strcmpi(_argv[argp],"-?") == 0)     //
        {
            prhlp();
            exit(1);
        }
        else
        {
            prhlp();
        }

        argp += 1;
    }
    return 0;
}


/*-----------------------------------------------------------------------------

FUNCTION: CreateStatusEditFont

PURPOSE: Creates the font for the status edit control

RETURN: HFONT of new font created

HISTORY:   Date:      Author:     Comment:
           10/27/95   AllenD      Wrote it

-----------------------------------------------------------------------------*/

#if USEWIN32
HFONT CreateStatusEditFont()
{
    LOGFONT lf = {0};
    HFONT   hFont;

    lf.lfHeight         = 14 ;
    lf.lfCharSet        = ANSI_CHARSET ;
    lf.lfOutPrecision   = OUT_DEFAULT_PRECIS ;
    lf.lfClipPrecision  = CLIP_DEFAULT_PRECIS ;
    lf.lfQuality        = DEFAULT_QUALITY ;
    lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS ;
    strcpy(lf.lfFaceName,"Lucida Console");

    hFont = CreateFontIndirect(&lf);
    return hFont;
}

/*-----------------------------------------------------------------------------

FUNCTION: CreateVTTYFont

PURPOSE: Creates the font for the status edit control

RETURN: HFONT of new font created

HISTORY:   Date:      Author:     Comment:
           10/27/95   AllenD      Wrote it

-----------------------------------------------------------------------------*/
HFONT CreateVTTYFont()
{
    LOGFONT lf = {0};
    HFONT   hFont;

    lf.lfHeight         = -11 ;
    lf.lfCharSet        = ANSI_CHARSET ;
    lf.lfOutPrecision   = OUT_DEFAULT_PRECIS ;
    lf.lfClipPrecision  = CLIP_DEFAULT_PRECIS ;
    lf.lfQuality        = DEFAULT_QUALITY ;
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN ;
    strcpy(lf.lfFaceName,"Lucida Console");
    hFont = CreateFontIndirect(&lf);
    return hFont;
}


/*-----------------------------------------------------------------------------
FUNCTION: GlobalInitialize

PURPOSE: Intializes global variables before any windows are created

COMMENTS: Partner to GlobalCleanup

HISTORY:   Date:      Author:     Comment:
           10/27/95   AllenD      Wrote it

-----------------------------------------------------------------------------*/
void GlobalInitialize()
{
#if USEWIN32

    int cyMenuHeight, cyCaptionHeight, cyFrameHeight;
    int k;


    //
    // font for status reporting control
    //
    ghFontStatus = CreateStatusEditFont();
    ghFontVTTY = CreateVTTYFont();

    //
    // the following are used for sizing the tty window and dialog windows
    //
    gwBaseY = HIWORD(GetDialogBaseUnits());
    cyMenuHeight = GetSystemMetrics(SM_CYMENU);
    cyCaptionHeight = GetSystemMetrics(SM_CYCAPTION);
    cyFrameHeight = GetSystemMetrics(SM_CYFRAME);
    gcyMinimumWindowHeight = cyMenuHeight + \
                            4 * cyCaptionHeight + \
                            2 * cyFrameHeight +
                            (SETTINGSFACTOR + STATUSFACTOR) * gwBaseY ;
#endif
    return ;
}
#endif

/*-----------------------------------------------------------------------------

FUNCTION: GlobalCleanup

PURPOSE: Cleans up any global variables

COMMENTS: Partner to GlobalInitialize

HISTORY:   Date:      Author:     Comment:
           10/27/95   AllenD      Wrote it

-----------------------------------------------------------------------------*/
void GlobalCleanup()
{
#if USEWIN32

    DeleteObject(ghFontStatus);
    DeleteObject(ghFontVTTY);
#endif
    return;
}

#if USEWIN32

/*
    HMENU mainmenu;
    HMENU mn4;
    MENUITEMINFO tmpminfo;
    int fok;
    char tmpstr[72];

    mainmenu = GetMenu(ghwndMain);
    memset(&tmpminfo,0,sizeof(MENUITEMINFO));
    tmpminfo.cbSize = sizeof(MENUITEMINFO);
    tmpminfo.fMask = MIIM_SUBMENU;
    fok = GetMenuItemInfo(mainmenu,3,TRUE,&tmpminfo);
    mn4=tmpminfo.hSubMenu;
    printf("GetMenuItemInfo: fok 1=%d mainmenu=0x%08x mn4=0x%08x\n",fok,mainmenu,mn4);
    if(!fok)
        return;

    if (fok)
    {
        memset(&tmpminfo,0,sizeof(MENUITEMINFO));
        tmpminfo.cbSize = sizeof(MENUITEMINFO);
        tmpminfo.fMask = MIIM_DATA+MIIM_TYPE+MIIM_ID;
        tmpminfo.fMask = MIIM_TYPE;
        tmpminfo.fType = MFT_STRING;
        tmpminfo.dwTypeData = tmpstr;
        tmpminfo.cch = 72;      // This is the key, cch must be buffer size
        memset(tmpstr,0,72);
        fok = GetMenuItemInfo(mn4,1,TRUE,&tmpminfo);

        printf("GetMenuItemInfo: fok 2=%d\n",fok);
        if (fok)
        {
    printf("  %8d cbSize\n",tmpminfo.cbSize);
    printf("0x%08x fMask\n",tmpminfo.fMask);
    printf("0x%08x fType\n",tmpminfo.fType);
    printf("0x%08x fState\n",tmpminfo.fState);
    printf("  %8d wID\n",tmpminfo.wID);
    printf("0x%08x hSubMenu\n",tmpminfo.hSubMenu);
    printf("0x%08x hbmpChecked\n",tmpminfo.hbmpChecked);
    printf("0x%08x hbmpUnchecked\n",tmpminfo.hbmpUnchecked);
    printf("0x%08x dwItemData\n",tmpminfo.dwItemData);
    printf("0x%08x dwTypeData\n",tmpminfo.dwTypeData);
    printf("  %8d cch\n",tmpminfo.cch);
        tmpstr[72]=0;
        printf("<%s>\n",tmpstr);
        }
        else
            MessageLastError("Not OK 2");
        GetMenuString(mn4,1,tmpstr,72,MF_BYPOSITION);
        tmpstr[72]=0;
        printf("<%s>\n",tmpstr);
    printf("\n");
    }
}
*/
static void setMenuname(HMENU hMenu, int menuId, char *newname)
{
    MENUITEMINFO tmpminfo;
    int fok;

    memset(&tmpminfo,0,sizeof(MENUITEMINFO));
    tmpminfo.cbSize = sizeof(MENUITEMINFO);
    tmpminfo.fMask = MIIM_TYPE;
    tmpminfo.fType = MFT_STRING;
    tmpminfo.dwTypeData = newname;
    tmpminfo.cch = 72;      // This is the key, cch must be buffer size
    fok = SetMenuItemInfo(hMenu,menuId,FALSE,&tmpminfo);
}

void AdjustMenu(int mm)
{
HMENU mainmenu;
HMENU mn4;
MENUITEMINFO tmpminfo;
int fok;
    char tmpstr[72];

    mainmenu = GetMenu(ghwndMain);
    memset(&tmpminfo,0,sizeof(MENUITEMINFO));
    tmpminfo.cbSize = sizeof(MENUITEMINFO);
    tmpminfo.fMask = MIIM_SUBMENU;
    fok = GetMenuItemInfo(mainmenu,3,TRUE,&tmpminfo);
    mn4=tmpminfo.hSubMenu;
    printf("GetMenuItemInfo: fok 1=%d mainmenu=0x%08x mn4=0x%08x\n",fok,mainmenu,mn4);
    setMenuname(mn4,ID_G0XF80000,inicfg.GF80000);
    setMenuname(mn4,ID_G0FLASH,inicfg.GFLASH);

    setMenuname(mn4,ID_USERSTR1,inicfg.userstr1);
    setMenuname(mn4,ID_USERSTR2,inicfg.userstr2);
    setMenuname(mn4,ID_USERSTR3,inicfg.userstr3);
    setMenuname(mn4,ID_USERSTR4,inicfg.userstr4);
}
#endif
