#ifndef VSPMENU_H
#define VSPMENU_H
/*
**************************************************************************
*
*  FILE: vspmenu.h
*
*  PURPOSE: menu include file
*
*  Comments:
*
*
**************************************************************************
*/

//======== Menu handles
typedef struct {
    struct {
        GtkWidget*  Self;
        GtkWidget*  Connect;
        GtkWidget*  Disconnect;
        GtkWidget*  Status;
        GtkWidget*  SendSRec;
        GtkWidget*  Upload;
        GtkWidget*  Close;
    } File;
    struct {
        GtkWidget*  Self;
        GtkWidget*  Copy;
        GtkWidget*  Cut;
        GtkWidget*  Paste;
        GtkWidget*  Bold;
        GtkWidget*  TTY;
        GtkWidget*  MapA;
        GtkWidget*  MapN;
    } Edit;
    struct {
        GtkWidget*  Self;
        GtkWidget*  FUNCTION_12;
        GtkWidget*  ClrScr;
        GtkWidget*  ClrHist;
        GtkWidget*  New_TTY;
        GtkWidget*  Close_TTY;
        GtkWidget*  Show_All;
        GtkWidget*  Hide_All;
    } TTY;
    struct {
        GtkWidget*  Self;
        GtkWidget*  Reset;
        GtkWidget*  IRQ7;
        GtkWidget*  G_0xf80000;
        GtkWidget*  G_Flash;
        GtkWidget*  DL;
        GtkWidget*  S7;
        GtkWidget*  VERSION;
        GtkWidget*  PS;
        GtkWidget*  Bit_Rate;
    } EVB;
    struct {
        GtkWidget*  Self;
        GtkWidget*  Edit;
        GtkWidget*  UnFramed;
        GtkWidget*  VSP0;
        GtkWidget*  VSP1;
        GtkWidget*  VSP2;
        GtkWidget*  VSP3;
        GtkWidget*  VSP4;
        GtkWidget*  VSP5;
        GtkWidget*  VSP6;
        GtkWidget*  VSP7;
    } VSP;
} t_hMenuBar;


extern t_hMenuBar hMenubar;

//======== Prototypes
extern GtkWidget* makevspmenubar(GtkWidget* win, t_hMenuBar* phBar);
extern GtkWidget* makeDlDialog(GtkWindow* win, t_Dnload* dlgCtl, const char* name);
extern GtkWidget* makeUlDialog(GtkWindow* win, t_Dnload* dlgCtl, const char* name);

#endif
