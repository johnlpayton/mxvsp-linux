
/*******************************************************************************
*
*
*
* Window for line numbrs
* 
* Placed vertically (later include horizontal) it understands the fo;;owing
*   WM_FONT set the font size
*   WM_CREATE
*   WM_CLOSE
*   WM_DESTROY
*   LNUM_SETPARAMS ()
*   LNUM_GETPARAMS ()
*   LNUM_SETSTART ()
*   
* Lets start with globals then create perWindow extra data
*   number start
*   pixel offset
*   number step
*   pixel step
*   font handle
*   
*******************************************************************************/

#include <stdio.h>
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <fcntl.h>
#include "mttty.h"

struct S_LnumParam TheParams;

void JlpTraceMsg(int, int, int);

/*---------------------------------------------------------------------------
*  BOOL PaintLnum( HWND hWnd, static struct S_LnumParam *pParam )
*
*  Description:
*     Paints the rectangle determined by the paint struct of
*     the DC.
*
*  Parameters:
*     HWND hWnd
*        handle to TTY window (as always)
*
*  Comments
*      This paints the entire window
*
*  History:   Date       Author      Comment
*
*---------------------------------------------------------------------------*/

BOOL PaintLnum( HWND hWnd, struct S_LnumParam *pParam )
{
    RECT crect;
    RECT trect;
    POINT Pt;
    POINT PtXY;
    int lnum;
    int k,m;
    int Nchar; 
    HDC hdc;
    HFONT hOldFont;
    PAINTSTRUCT  ps ;
    char tmpChar[32];

    
    GetClientRect(hWnd,&crect);
    hdc = BeginPaint( hWnd, &ps );

    pParam->hFont = HTTYFONT( TTYInfo );
    pParam->PixelStep = TTYInfo.yChar;
    
    
    if(pParam->hFont )
        hOldFont = SelectObject( hdc, pParam->hFont ) ;
    SetTextColor( hdc, FGCOLOR( TTYInfo ) ) ;
    SetBkColor( hdc, RGB(128,128,128) ) ;
     
    lnum = pParam -> NumStart;
    crect = pParam->PrRect;
    trect = crect;
    trect.bottom -= 0;
    trect.right -= 0;
    m=0;
    do{
        /*
        ** use the common call to find the vertical
        */        
        Pt.x = 0;                       //Locate scree cords of Cell(lnum-1,0)
        Pt.y = lnum-1;
        k = Buf2ScrChar(hWnd, &TTYInfo, &(Pt),&Pt);
        if(k<0)                         // off screen, get out
            break;
        k=Scr2XYPixels(hWnd, &TTYInfo, &Pt, &PtXY );
        if(k<0)
            break;
        trect.bottom = PtXY.y;
        trect.top = trect.bottom - pParam->PixelStep;

        sprintf(tmpChar,"%4d",lnum);
        Nchar = strlen(tmpChar);
        trect.top = trect.bottom - pParam->PixelStep;
#if EASYWINDEBUG & 0
printf("(%d,%d,%d,%d) %d, <%s>\n",
    trect.top,trect.left,trect.bottom,trect.right,
    lnum,tmpChar);
#endif
        DrawText(hdc,tmpChar,Nchar,&trect,(UINT)(DT_RIGHT | DT_BOTTOM | DT_SINGLELINE));
        lnum = lnum + pParam->NumStep;
    }while(++m < 400);

    if(pParam->hFont )
        SelectObject( hdc, hOldFont ) ;
    EndPaint( hWnd, &ps ) ;
    return TRUE;   
}

BOOL CALLBACK _export LineNumbersProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{

    BOOL retval=TRUE;
    struct S_LnumParam *pParam;
    char tmpTxt[32];
    
#if EASYWINDEBUG & 0
TraceWinMessages("lNum",msg,wParam,lParam);
#endif

    pParam = &TheParams;
    
    switch  ( msg & 0xffff )
    {
        //==============
        case WM_CREATE:         // Initialize
        //==============
            FillMemory(pParam,sizeof(struct S_LnumParam),0);
            pParam->NumStart    = 1;
            pParam->NumStep     = 1;
            pParam->PixelStep   = 8;
            pParam->Hmargin     = TTYInfo.Margins.x;
            pParam->Vmargin     = TTYInfo.Margins.y;
            pParam->hFont       = GetStockObject(SYSTEM_FONT);
            pParam->CharSize.x  = 8;
            pParam->CharSize.y  = 12;
            pParam->PrRect.right  = 48;             // screen is not drawn until WM_SIZE
            pParam->PrRect.bottom  = 48;
            pParam->flags       = LNUM_BOT2TOP;
        
            retval=FALSE;
        break;
        
        //==============
        case WM_CLOSE:          // Hide ourselves
        //==============
 
         break;

        //==============
        case WM_SIZE:
        //==============
            InvalidateRect( hWnd, NULL, TRUE ) ;
        break;

        //==============
        case WM_SETFONT:
        //==============
            pParam->hFont = (HFONT)wParam;
            if(lParam)
            {
                PaintLnum(hWnd,pParam);
            }
            InvalidateRect( hWnd, NULL, TRUE ) ;
        break;

        //==============
        case WM_ACTIVATE:       // we're becomming active/inactive
        //==============
        break;

        //==============
        case LNUM_SETPARAMS:
        //==============
            *pParam = *(struct S_LnumParam *)lParam;
             PaintLnum(hWnd,pParam);
            InvalidateRect( hWnd, NULL, TRUE ) ;
        break;

        //==============
        case LNUM_GETPARAMS:
        //==============
            *(struct S_LnumParam *)lParam = *pParam;
        break;

        //==============
        case LNUM_SETSTART:
        //==============
            pParam->NumStart = lParam+1;
            PaintLnum(hWnd,pParam);
            InvalidateRect( hWnd, NULL, TRUE ) ;
        break;

        //==============
        case WM_PAINT:          // Redraw the window
        //==============
            PaintLnum(hWnd,pParam);
        break;

        //==============
        default:
        //==============
            retval = DefWindowProc(hWnd, msg, wParam, lParam);
        break;
    }
    return (retval);                  /* Didn't process a message    */

}
