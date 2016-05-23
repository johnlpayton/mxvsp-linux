#ifndef VSPTERM_H
#define VSPTERM_H
/*
**************************************************************************
*
*  FILE: vspterm.h
*
*  PURPOSE: terminal include file
*
*  Comments:
*
*
**************************************************************************
*/


//======== Prototypes
extern GtkWidget*  newvspterm(void);
extern int TextToVsp(void* v, void* txt, int ntxt);
//extern int VspToCopyBuf(void* v, void* txt, int ntxt);
extern int VspAction(void* pMapEntry, int cmd, long arg1, long arg2);
#endif
