/*
*   FILE: symutils.c
*
*   Symbol unilities for mxvsp
*
*   COMMENTS:
*       Symbol table manipulations.  I spend a lot of time poking aroung
*       in the map files.  Some of the information should be available
*       "online" on this program.
*       We'll start with some simple things.
*       Start by reading the file
*
*/

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
#include "mxsymutil.h"

/*
**************************************************************************
*
*  Function: int LoadSymDef(FILE* fdef, t_symtabhdr* hdr)
*
*  Purpose: Read a symbol definition file and prepare the symbol table
*
*  Comments:
*   The definition file is an output from a companion utility
*   mksymbols.exe.  Given a map and a control file, use
*
*   mksymbols.exe -c bootnfctl.txt -m bootnf.map -o bootnf.sym -sa -nd -us
*
*   bootnfctl.txt is a list of symbol names
*   bootnf.map is the (valid) map file
*   bootnf.sym is the output
*   -sa sorts alphabetically (don't care)
*   -nd means output is decimal
*   -us means undefined symbols are suppressed
*
*   The output file (bootnf.sym) should be opened and the FILE* given
*   as the argument to this routine.
**************************************************************************
*/
int LoadSymDef(FILE* fdef, t_symtabhdr* hdr)
{
    int     k;
    t_symtabentry tmpsym;
    char*   pNextStr;
    t_symtabentry* pNextSym;
    char    tmpstr[96];
    char*   pChar;


    DiscardSymTab(hdr);

    // start by sizing the file in bytes. This will be
    // larger than the memory size we need but not by much so
    // we can use it for a malloc

    fseek(fdef,0,2);                            // seek to the end
    k = ftell(fdef);                            // get the end
    k = (k+7+512)&(~7);                             // round up 
    fseek(fdef,0,0);                            // seek to the beginning
    hdr->nmalloc = k;

    // we should do some error checking here but what the hey
    hdr->mbase = (char*)malloc(k);
    if(!hdr->mbase)
        FatalError("LoadSymDef: malloc failed");

    // start the string storage at mbase and the symbols at the end
    pNextStr = (char*)(hdr->mbase);
    pNextSym = (t_symtabentry*)(&(pNextStr[k]));
    hdr->nsym = 0;                              // no symbols

    // read in all symbols
    while(1)
    {

        //======= Replace with readNL
        k=(int)freadLn(tmpstr,96,fdef);         // read a line
        if(k == EOF)                            // end of file
            break;
        if(k <= 8)                              // short line
            continue;

        // format is string decimal <LF>
        pChar = strtok(tmpstr," \t\n\r");       // errors can only happen with the wrong file
        if(!pChar)
            break;
        k=strlen(pChar);                        // get the length
        strcpy(pNextStr,pChar);                 // copy the string

        pChar = strtok(NULL," \t\n\r");         // get the value
        if(!pChar)
            break;

        pNextSym -= 1;                          // symbol table pointer moves backwards

        pNextSym->symval = atoi(pChar);         // convert the value
        pNextSym->symname = pNextStr;           // string location
#if EASYWINDEBUG & 0
printf("LoadSymDef: 0x%08x %s\n",pNextSym->symval,pNextSym->symname);
#endif
        pNextStr += (k+1);                      // bump string storage up
        hdr->nsym += 1;
        if( ((char*)pNextStr) >= ((char*)pNextSym))
            FatalError("LoadSymDef: Table Overlap");
    }

    hdr->pSym = pNextSym;
    hdr->sorted = SYMSORTEDNONE;
#if EASYWINDEBUG & 0
printf("LoadSymDef: pNextStr=0x%08x pNextSym=0x%08x\n",pNextStr,pNextSym);
#endif

    // on exit check if we got ANY symbols
    if(hdr->nsym > 0)
        return(hdr->nsym);

    free(hdr->mbase);
    hdr->mbase = NULL;
    hdr->pSym = NULL;
    return(0);
}

/*
**************************************************************************
*
*  Function: int SortSymAlpha(t_symtabhdr* hdr)
*
*  Purpose: Sort symbols alphabetically
*
*  Comments:
*
**************************************************************************
*/
static int HelperAlphaSort(const void* p1, const void* p2)
{
    t_symtabentry* e1 = (t_symtabentry*)p1;
    t_symtabentry* e2 = (t_symtabentry*)p2;

    return(strcmpi(e1->symname,e2->symname));
}

void SortSymAlpha(t_symtabhdr* hdr)
{
    if(hdr->mbase == NULL)                      // emoty table?
        return;

    if(hdr->sorted == SYMSORTEDALPHA)           // is sorted?
        return;

    qsort(hdr->pSym, hdr->nsym, sizeof(t_symtabentry), HelperAlphaSort);

    hdr->sorted = SYMSORTEDALPHA;
}

/*
**************************************************************************
*
*  Function: int SortSymNum(t_symtabhdr* hdr)
*
*  Purpose: Sort symbols Numerically
*
*  Comments:
*
**************************************************************************
*/
static int HelperNumSort(const void* p1, const void* p2)
{
    t_symtabentry* e1 = (t_symtabentry*)p1;
    t_symtabentry* e2 = (t_symtabentry*)p2;

    return(e1->symval - e2->symval);
}

void SortSymNum(t_symtabhdr* hdr)
{
    if(hdr->mbase == NULL)                      // emoty table?
        return;

    if(hdr->sorted == SYMSORTEDNUM)           // is sorted?
        return;

    qsort(hdr->pSym, hdr->nsym, sizeof(t_symtabentry), HelperNumSort);

    hdr->sorted = SYMSORTEDNUM;
}

/*
**************************************************************************
*
*  Function: void DiscardSymTab(t_symtabhdr* hdr)
*
*  Purpose: Discard memory and null out a symbol table
*
*  Comments:
*   safe to call on a NULLed sumbol table (hdr->mbase == NULL)
*
**************************************************************************
*/
void DiscardSymTab(t_symtabhdr* hdr)
{
    if(hdr->mbase)
        free(hdr->mbase);

    memset(hdr,0,sizeof(t_symtabhdr));
}

/*
**************************************************************************
*
*  Function: int sprintSymFile(char* pDst, t_symtabhdr* hdr)
*
*  Purpose: print the symbols into a preallocated array
*
*  Comments:
*   Used to copy symbols into the internal editor
*
**************************************************************************
*/
int sprintSymFile(char* pDst, t_symtabhdr* hdr)
{
    int     k,m;
    int     kdst;
    t_symtabentry* ps;
    char    tmpstr[96];

    if(!hdr) return(0);

    kdst = 0;
    ps = hdr->pSym;
    for(m=0; m < hdr->nsym; m += 1)
    {
        
        sprintf(tmpstr,"0x%08x ; %s\r\n",ps[m].symval,ps[m].symname);
        k = strlen(tmpstr);
        strcpy(&(pDst[kdst]),tmpstr);
        kdst += k;
    }
    return kdst;
}

