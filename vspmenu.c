/*
**************************************************************************
*
*  FILE: vspmenu.c
*
*  PURPOSE: Make menus for mxvsp
*
*  Comments:
*
*
**************************************************************************
*/
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <vte/vte.h>
#include <gdk/gdkkeysyms.h>
#include <glade/glade.h>

#include "memlock.h"
#include "vdownload.h"
#include "vspmenu.h"
#include "muxctl.h"
#include "muxw2ctl.h"


extern gboolean MenuF12(gpointer pData, GtkWidget* pWidget);
extern gboolean EVBCmdEvent(gpointer pData, GtkWidget* pWidget);
extern gboolean EVBCmdEventRaw(gpointer pData, GtkWidget* pWidget);
extern gboolean EVBCmdEventQuit(gpointer pData, GtkWidget* pWidget);
extern GtkWidget* pMainWindow;                 // Main window
extern t_Dnload            sDNload;                     // Download structure
extern GtkWidget*          pDlBox;                      // Download dialod
extern GtkWidget*          pNotebook;                   // Notebook for the VSPs

static MenuDlSrec(gpointer pData, GtkWidget* pWidget);
static MenuULXmodem(gpointer pData, GtkWidget* pWidget);
static int MenudoCopy(gpointer pData, GtkWidget* pWidget);

/*
**************************************************************************
*  FUNCTION: static gboolean MenuDummyEvent(gpointer pData, GtkWidget* pWidget)
*
*  PURPOSE: Dummy event for menubar
*
*  COMMENTS:
*
**************************************************************************
*/
static gboolean MenuDummyEvent(gpointer pData, GtkWidget* pWidget)
{
//    vte_terminal_feed(VTE_TERMINAL(pMyVsp),"Hello\n",6);
    printf("MenuDummyEvent %s 0x%08x\n", (char*)pData, (U32)pWidget);
    return TRUE;
}

/*
**************************************************************************
*
*  FUNCTION: GtkWidget* makevspmenubar(void)
*
*  PURPOSE: Make a menu bar
*
*  COMMENTS:
*   Right now, we do this the hard way.  There should be some sort
*   of graphical program but Glade seems to crash
*
* File
*   Connect NA
*   Disconnect NA
*   Status
*   ----
*   Send SRec
*   Upload
*   ----
*   Exit
*
* Edit
*   Cut
*   Copy
*   Paste
*   Bold
*   ----
*   TTY
*   Map Alpha
*   Map Num
*
* TTY
*   FUNCTION 12 NA
*   ----
*   ClrSrc
*   ClrHist
*   ----
*   New TTY NA
*   Open TTY -> NA
*       VSP 0
*   Close TTY NA
*   ----
*   Show All NA
*   Hide All NA
*
* EVB
*   Reset
*   IRQ 7
*   ----
*   G 0xf80000
*   G Flash
*   ----
*   DL
*   S7 (End SRec)
*   VERSION
*   PS
*   ----
*   Bit Rate ->
*       9600
*       19200
*       38400
*       57600
*
* Window                NA
*   MDI stuff
*
* Help
*   About
**************************************************************************
*/

GtkWidget* makevspmenubar(GtkWidget* win, t_hMenuBar* phBar)
{
    GtkWidget*          menubar;

    menubar = gtk_menu_bar_new();
//    gtk_container_add (GTK_CONTAINER(win), menubar);

    //======== File menu
    {
        GtkWidget*  filemenu;
        GtkWidget*  itemmenu;

        filemenu = gtk_menu_new ();
        phBar->File.Self = filemenu;

        itemmenu = gtk_menu_item_new_with_label ("Connect");
        phBar->File.Connect = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (filemenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "File.Connect");

        itemmenu = gtk_menu_item_new_with_label ("Disconnect");
        phBar->File.Disconnect = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (filemenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "File.Disconnect");

        itemmenu = gtk_separator_menu_item_new();
        gtk_menu_shell_append (GTK_MENU_SHELL (filemenu), itemmenu);

        itemmenu = gtk_menu_item_new_with_label ("Status");
        phBar->File.Status = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (filemenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "File.Status");

        itemmenu = gtk_menu_item_new_with_label ("SendSRec");
        phBar->File.SendSRec = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (filemenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDlSrec),
                            (gpointer)&sDNload);

        itemmenu = gtk_menu_item_new_with_label ("XModemUP");
        phBar->File.Upload = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (filemenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuULXmodem),
                            (gpointer)&sDNload);

        itemmenu = gtk_separator_menu_item_new();
        gtk_menu_shell_append (GTK_MENU_SHELL (filemenu), itemmenu);

        itemmenu = gtk_menu_item_new_with_label ("Quit");
        phBar->File.Close = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (filemenu), itemmenu);
        // rare case where we don't want the data pointer to be the first argument'
        g_signal_connect (itemmenu, "activate",
                            G_CALLBACK (EVBCmdEventQuit),
                            NULL);

        // create the label and submenu
        itemmenu = gtk_menu_item_new_with_label ("File");
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (itemmenu), filemenu);
        // append to the menu bar
        gtk_menu_shell_append (GTK_MENU_SHELL (menubar), itemmenu);
    }

    //======== Edit menu
    {
        GtkWidget*  editmenu;
        GtkWidget*  itemmenu;

        editmenu = gtk_menu_new ();
        phBar->Edit.Self = editmenu;

        itemmenu = gtk_menu_item_new_with_label ("Copy");
        phBar->Edit.Copy = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (editmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenudoCopy),
                            (gpointer) "Edit.Copy");

        itemmenu = gtk_menu_item_new_with_label ("Cut");
        phBar->Edit.Cut = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (editmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "Edit.Cut");

        itemmenu = gtk_menu_item_new_with_label ("Paste");
        phBar->Edit.Paste = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (editmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "Edit.Paste");

        itemmenu = gtk_menu_item_new_with_label ("Bold");
        phBar->Edit.Bold = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (editmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "Edit.Bold");

        itemmenu = gtk_separator_menu_item_new();
        gtk_menu_shell_append (GTK_MENU_SHELL (editmenu), itemmenu);

        itemmenu = gtk_menu_item_new_with_label ("TTY");
        phBar->Edit.TTY = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (editmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "Edit.TTY");

        itemmenu = gtk_menu_item_new_with_label ("Map Alpha");
        phBar->Edit.MapA = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (editmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "Edit.MapA");

        itemmenu = gtk_menu_item_new_with_label ("Map Num");
        phBar->Edit.MapN = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (editmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "Edit.MapN");

        // create the label and submenu
        itemmenu = gtk_menu_item_new_with_label ("Edit");
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (itemmenu), editmenu);
        // append to the menu bar
        gtk_menu_shell_append (GTK_MENU_SHELL (menubar), itemmenu);
    }

    //======== TTY menu
    {
        GtkWidget*  ttymenu;
        GtkWidget*  itemmenu;

        ttymenu = gtk_menu_new ();
        phBar->TTY.Self = ttymenu;

        itemmenu = gtk_menu_item_new_with_label ("FUNCTION 12");
        phBar->TTY.FUNCTION_12 = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (ttymenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuF12),
                            (gpointer) "TTY.FUNCTION_12");

        itemmenu = gtk_separator_menu_item_new();
        gtk_menu_shell_append (GTK_MENU_SHELL (ttymenu), itemmenu);


        itemmenu = gtk_menu_item_new_with_label ("ClrScr");
        phBar->TTY.ClrScr = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (ttymenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "TTY.ClrScr");

        itemmenu = gtk_menu_item_new_with_label ("ClrHist");
        phBar->TTY.ClrHist = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (ttymenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "TTY.ClrHist");

        itemmenu = gtk_separator_menu_item_new();
        gtk_menu_shell_append (GTK_MENU_SHELL (ttymenu), itemmenu);


        itemmenu = gtk_menu_item_new_with_label ("New TTY");
        phBar->TTY.New_TTY = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (ttymenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "TTY.New_TTY");

#if 0
        //======== VSP sub menu
        {
            GtkWidget*  vspmenu;
            GtkWidget*  itemmenu;

            vspmenu = gtk_menu_new ();
            phBar->TTY.Open_TTY.Self = vspmenu;

            itemmenu = gtk_menu_item_new_with_label ("VSP0");
            phBar->TTY.Open_TTY.VSP0 = itemmenu;
            gtk_menu_shell_append (GTK_MENU_SHELL (vspmenu), itemmenu);
            g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "TTY.Open_TTY.VSP0");

        // create the label and submenu
        itemmenu = gtk_menu_item_new_with_label ("Open TTY");
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (itemmenu), vspmenu);

        gtk_menu_shell_append (GTK_MENU_SHELL (ttymenu), vspmenu);
        }
        //======== THIS IS NOT WORKING
#endif
        itemmenu = gtk_menu_item_new_with_label ("Close TTY");
        phBar->TTY.Close_TTY = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (ttymenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "TTY.Close_TTY");


        itemmenu = gtk_menu_item_new_with_label ("Show All");
        phBar->TTY.Show_All = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (ttymenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "TTY.Show_All");

        itemmenu = gtk_menu_item_new_with_label ("Hide All");
        phBar->TTY.Hide_All = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (ttymenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "TTY.Hide_All");

        // create the label and submenu
        itemmenu = gtk_menu_item_new_with_label ("TTY");
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (itemmenu), ttymenu);
        // append to the menu bar
        gtk_menu_shell_append (GTK_MENU_SHELL (menubar), itemmenu);
    }

    //======== EVB menu
    {
        GtkWidget*  evbmenu;
        GtkWidget*  itemmenu;

        evbmenu = gtk_menu_new ();
        phBar->EVB.Self = evbmenu;

        itemmenu = gtk_menu_item_new_with_label ("Reset");
        phBar->EVB.Reset = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (evbmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (EVBCmdEvent),
                            (gpointer) "RESET");


        itemmenu = gtk_menu_item_new_with_label ("IRQ7");
        phBar->EVB.IRQ7 = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (evbmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (EVBCmdEventRaw),
                            (gpointer) "\034");

        itemmenu = gtk_separator_menu_item_new();
        gtk_menu_shell_append (GTK_MENU_SHELL (evbmenu), itemmenu);

        itemmenu = gtk_menu_item_new_with_label (inicfg.GF80000);
        phBar->EVB.G_0xf80000 = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (evbmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (EVBCmdEvent),
                            (gpointer) inicfg.GF80000);


        itemmenu = gtk_menu_item_new_with_label (inicfg.GFLASH);
        phBar->EVB.G_Flash = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (evbmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (EVBCmdEvent),
                            (gpointer)inicfg.GFLASH);

        itemmenu = gtk_separator_menu_item_new();
        gtk_menu_shell_append (GTK_MENU_SHELL (evbmenu), itemmenu);


        itemmenu = gtk_menu_item_new_with_label ("DL");
        phBar->EVB.DL = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (evbmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (EVBCmdEvent),
                            (gpointer) "DL");

        itemmenu = gtk_menu_item_new_with_label ("S7 (End DL)");
        phBar->EVB.S7 = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (evbmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (EVBCmdEvent),
                            (gpointer) "S7");


        itemmenu = gtk_menu_item_new_with_label ("VERSION");
        phBar->EVB.VERSION = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (evbmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (EVBCmdEvent),
                            (gpointer) "VERSION");

        itemmenu = gtk_menu_item_new_with_label ("PS");
        phBar->EVB.PS = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (evbmenu), itemmenu);
#if 1 // prefered, this sets the data to be the first argument
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (EVBCmdEvent),
                            (gpointer) "ps");
#else
        g_signal_connect (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "EVB.PS not swapped");
#endif

        itemmenu = gtk_separator_menu_item_new();
        gtk_menu_shell_append (GTK_MENU_SHELL (evbmenu), itemmenu);

        itemmenu = gtk_menu_item_new_with_label (inicfg.userstr1);
        phBar->EVB.VERSION = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (evbmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (EVBCmdEvent),
                            (gpointer) inicfg.userstr1);

        itemmenu = gtk_menu_item_new_with_label (inicfg.userstr2);
        phBar->EVB.VERSION = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (evbmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (EVBCmdEvent),
                            (gpointer) inicfg.userstr2);

        itemmenu = gtk_menu_item_new_with_label (inicfg.userstr3);
        phBar->EVB.VERSION = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (evbmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (EVBCmdEvent),
                            (gpointer) inicfg.userstr3);

        itemmenu = gtk_menu_item_new_with_label (inicfg.userstr4);
        phBar->EVB.VERSION = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (evbmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (EVBCmdEvent),
                            (gpointer) inicfg.userstr4);
        // create the label and submenu
        itemmenu = gtk_menu_item_new_with_label ("EVB");
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (itemmenu), evbmenu);
        // append to the menu bar
        gtk_menu_shell_append (GTK_MENU_SHELL (menubar), itemmenu);
    }


    //======== VSP menu
    {
        GtkWidget*  vspmenu;
        GtkWidget*  itemmenu;

        vspmenu = gtk_menu_new ();
        phBar->EVB.Self = vspmenu;

        itemmenu = gtk_menu_item_new_with_label ("Edit");
        phBar->VSP.Edit = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (vspmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "VSP.Edit");

        itemmenu = gtk_menu_item_new_with_label ("UnFramed");
        phBar->VSP.UnFramed = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (vspmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "VSP.UnFramed");

        itemmenu = gtk_separator_menu_item_new();
        gtk_menu_shell_append (GTK_MENU_SHELL (vspmenu), itemmenu);

        itemmenu = gtk_menu_item_new_with_label ("VSP0");
        phBar->VSP.VSP0 = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (vspmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "VSP.VSP0");

        itemmenu = gtk_menu_item_new_with_label ("VSP1");
        phBar->VSP.VSP0 = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (vspmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "VSP.VSP1");

        itemmenu = gtk_menu_item_new_with_label ("VSP2");
        phBar->VSP.VSP0 = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (vspmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "VSP.VSP2");

        itemmenu = gtk_menu_item_new_with_label ("VSP3");
        phBar->VSP.VSP0 = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (vspmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "VSP.VSP3");

        itemmenu = gtk_menu_item_new_with_label ("VSP4");
        phBar->VSP.VSP0 = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (vspmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "VSP.VSP4");

        itemmenu = gtk_menu_item_new_with_label ("VSP5");
        phBar->VSP.VSP0 = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (vspmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "VSP.VSP5");

        itemmenu = gtk_menu_item_new_with_label ("VSP6");
        phBar->VSP.VSP0 = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (vspmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "VSP.VSP6");

        itemmenu = gtk_menu_item_new_with_label ("VSP7");
        phBar->VSP.VSP0 = itemmenu;
        gtk_menu_shell_append (GTK_MENU_SHELL (vspmenu), itemmenu);
        g_signal_connect_swapped (itemmenu, "activate",
                            G_CALLBACK (MenuDummyEvent),
                            (gpointer) "VSP.VSP7");

        // create the label and submenu
        itemmenu = gtk_menu_item_new_with_label ("Window");
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (itemmenu), vspmenu);

        // append to the menu bar
        gtk_menu_shell_append (GTK_MENU_SHELL (menubar), itemmenu);

    }
    return(menubar);
}


/*
**************************************************************************
*
*  FUNCTION: static MenuDlSrec(gpointer pData, GtkWidget* pWidget)
*
*  PURPOSE: Handle Menu download
*
*  COMMENTS:
*
**************************************************************************
*/
static int MenuDlSrec(gpointer pData, GtkWidget* pWidget)
{
    pDlBox = makeDlDialog(GTK_WINDOW(pMainWindow), &sDNload, "Send S-Rec");
    gtk_window_set_deletable(GTK_WINDOW(pDlBox),FALSE);
    gtk_window_set_decorated(GTK_WINDOW(pDlBox),FALSE);
    gtk_window_resize(GTK_WINDOW(pDlBox),400,64);
    gtk_widget_show_all(pDlBox);
}

/*
**************************************************************************
*
*  FUNCTION: static MenuDlSrec(gpointer pData, GtkWidget* pWidget)
*
*  PURPOSE: Handle Menu download
*
*  COMMENTS:
*
**************************************************************************
*/
static int MenuULXmodem(gpointer pData, GtkWidget* pWidget)
{
    pDlBox = makeUlDialog(GTK_WINDOW(pMainWindow), &sDNload, "X-Modem Upload");
    gtk_window_set_deletable(GTK_WINDOW(pDlBox),FALSE);
    gtk_window_set_decorated(GTK_WINDOW(pDlBox),FALSE);
    gtk_window_resize(GTK_WINDOW(pDlBox),400,64);
    gtk_widget_show_all(pDlBox);
}
/*
**************************************************************************
*
*  FUNCTION: static MenudoCopy(gpointer pData, GtkWidget* pWidget)
*
*  PURPOSE: Handle Menu Copy command
*
*  COMMENTS:
*
**************************************************************************
*/
static int MenudoCopy(gpointer pData, GtkWidget* pWidget)
{
    int tabId;
    int wIdx;

    tabId = gtk_notebook_get_current_page(GTK_NOTEBOOK (pNotebook));
    if(tabId < 0)
        return tabId;
    wIdx = LookUpWinByTAB(tabId);
    if(wIdx < 0)
        return wIdx;

    TTYWindows[wIdx].doAction(&TTYWindows[wIdx],W2CMD_COPY,0,0);
}
