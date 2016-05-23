#ifndef MXSYMUTIL_H
#define MXSYMUTIL_H 1

#include <stdio.h>

/*
-----------------------------------------------------------------------------
*   FILE: mxsymutil.h
*
*   Contains equates for the symbol table maniputations
*   for now, all symbols will be U32 nominally addresses
*
-----------------------------------------------------------------------------
*/
#define SYMSORTEDNONE 0
#define SYMSORTEDALPHA 1
#define SYMSORTEDNUM 2

// symbol is a name pointer and a U32
typedef struct S_symtabentry{
    char*           symname;                    // name
    int             symval;                     // value
}t_symtabentry;

typedef struct S_symtabhdr{
    int             nsym;                       // number of symbols in this table
    int             nmalloc;                    // size hint from malloc
    t_symtabentry*  pSym;                       // pointer to the symbol table
    char*           mbase;                      // pointer to the base memory from malloc
    char            sorted;                     // sorted by
    char            name[64-4*4-1];             // GUI name for the table
}t_symtabhdr;

extern int LoadSymDef(FILE* fdef, t_symtabhdr* hdr);
extern void SortSymAlpha(t_symtabhdr* hdr);
extern void SortSymNum(t_symtabhdr* hdr);
extern void DiscardSymTab(t_symtabhdr* hdr);
extern int sprintSymFile(char* pDst, t_symtabhdr* hdr);

#endif
