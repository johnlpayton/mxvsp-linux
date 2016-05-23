/*
**************************************************************************
* FILE: framer.c
*
* Contains frame management functions
*
*   int StuffAFrame(t_framer* fp)
*       Execute DLE-STX..DLE-ETX stuffing
*   int UnStuffFrame(t_framer* fp)
*       Undo DLE-STX..DLE-ETX stuffing
*
*
**************************************************************************
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
#include "muxctl.h"
#include "framer.h"

extern void FatalError(char*);

/*
*****************************************************************************
*  int StuffAFrame(struct S_framer* fp)
*
*   From an input buffer, make a transmit frame with the frame bytes and
*   DLE character stuffing for transparency. All characters in the
*   buffer are assembled and framed as a single frame.
*   In addition to the framing, two characters are added from the
*   data structure for routing and sequencing. So the frame looks
*   like:
*       DLE-STX-ID-SEQ-...-DLE-ETX
*
*   Additionally some characters used at layer 1 (UART) are escaped
*       DC3 (XOFF)  -> DLE-s
*       DC1 (XON)   -> DLE-q
*       ^|  (0x1c)  -> DLE-|
*       DLE         -> DLE-DLE
*
*   Stuffing begins after the first DLE-STX so the ID,SEQ characters remain
*   transparent.
*
*   The operation is transparent.  This implementation does a complete
*   frame so there must be sufficient output space.  Later we might generalize
*   it to break frames across buffers but right now, I don't see why one would
*   want that.
*
*   Add allowing empty buffers
*****************************************************************************
*/
// declare the helper so the compilier is happy
   static void StuffHelper(char c, struct S_framer* fp);

int StuffAFrame(t_framer* fp)
{
    char c;

    fp->cBufout = 0;                        // start a new frame

    /* Mod to allow a 0 length buffer*/
    if( (fp->cBufin > fp->szBufin) || (fp->cBufout >= fp->szBufOut-6)) // buffer checks
    {
        return(RTNC);
    }

    fp->Bufout[fp->cBufout++]=XDLE;         // Frame header
    fp->Bufout[fp->cBufout++]=XSTX;
    StuffHelper(fp->id,fp);                 // frame id
    StuffHelper(fp->seq,fp);                // frame sequence

    while( (fp->cBufin < fp->szBufin) && (fp->cBufout < fp->szBufOut-4))
    {
        StuffHelper(fp->Bufin[fp->cBufin++],fp);  // next char
    }

    fp->Bufout[fp->cBufout++]=XDLE;         // Frame trailor
    fp->Bufout[fp->cBufout++]=XETX;
    return(fp->cBufout);
}
// this is the helper function declared above. It does one byte
static void StuffHelper(char c, t_framer* fp)
{
    switch(c)
    {
        case XDLE:
            fp->Bufout[fp->cBufout++]=XDLE;
            fp->Bufout[fp->cBufout++]=XDLE;
        break;

        case XOFF:
            fp->Bufout[fp->cBufout++]=XDLE;
            fp->Bufout[fp->cBufout++]='s';
        break;

        case XON:
            fp->Bufout[fp->cBufout++]=XDLE;
            fp->Bufout[fp->cBufout++]='q';
        break;

        case XIRQ7:
            fp->Bufout[fp->cBufout++]=XDLE;
            fp->Bufout[fp->cBufout++]='|';
        break;

#if 0
        case XNULL:
            fp->Bufout[fp->cBufout++]=XDLE;
            fp->Bufout[fp->cBufout++]=' ';
        break;
#endif
        default:
            fp->Bufout[fp->cBufout++]=c;
    }

}

/*
*****************************************************************************
*  int UnStuffFrame(struct S_framer* fp)
*
*   A buffer may have parts of a frame and also unframed date.
*   This routine will unstuff any DLE sequences and return
*   on a frame boundry with perphaps leftover characters.
*   it is up to the higher level caller to assemble frames
*   and route them.
*
*   This is visible outside this file so it can be used inside
*   the router of the window process. The comments made for
*   StuffAFrame apply here as well.
*
*****************************************************************************
*/
int UnStuffFrame(t_framer* fp)
{
    int kin;
    int kout;
    char c;

    while( (fp->cBufin < fp->szBufin) && (fp->cBufout < fp->szBufOut))
    {
        c=fp->Bufin[fp->cBufin++];
#if (EASYWINDEBUG>5) && 0
printf("UnStuffFrame: %02x\n",c);
#endif
        switch(fp->state)
        {
            //========
            case 0:  // new character
            //========
            {
                if(c == XDLE)
                {
                    fp->state = 1;
                }
                else
                {
                    fp->Bufout[fp->cBufout++]=c; // raw char
                }
            }
            break;

            //========
            case 1:  // DLE inside a frame
            //========
            {
                fp->state = 0;                 // return to inside a frame
                switch(c)
                {
                    case XDLE:
                        fp->Bufout[fp->cBufout++]=XDLE; // raw char
                    break;

                    case XETX:
                        return(XETX);                   // frame end
                    //break;

                    case XSTX:
                    //    return(XSTX);                 // frame start ignore
                    break;

                    case 's':
                        fp->Bufout[fp->cBufout++]=XOFF;
                    break;

                    case 'q':
                        fp->Bufout[fp->cBufout++]=XON;
                    break;

                    case '|':
                        fp->Bufout[fp->cBufout++]=XIRQ7;
                    break;
#if 0
                    case ' ':
                        fp->Bufout[fp->cBufout++]=XNULL;
                    break;
#endif
                    default:
                        fp->Bufout[fp->cBufout++]=XDLE;
                        fp->Bufout[fp->cBufout++]=c;
                    break;
                }
            }
            break;

        }
    }

//    return(RTNC);                          // ran out of buffer
    if(fp->cBufout >= fp->szBufOut)
        return(XEOF);                          // ran out of output buffer
    else if(fp->state)
        return(XDLE);                          // ran out of buffer in a DLE
    else
        return(RTNC);                          // ran out of in buffer
}

#if 0
/*
*****************************************************************************
*  int MonitorFrame(t_framer* fp)
*
*   This routine will monitor a stream and re-copy it to
*   buffer.  The 3 return conditions are
*       XSTX a DLE-STX pait has been detected
*       XETX a DLE-ETX pair has been detected
*       RTNC The buffer has ended
*       XDLE The buffer is empty but a DLE is pending
*       XEOF The output buffer is full
*   characters are always copied into the output buffer.
*   The variable "inframe" is tracked and is valid after
*   DLE-STX or DLE-ETX
*
*   Unlike UnStuffFrame, MonitorFrame is designed to work across
*   input buffers.  It works on a stream and the caller is responsible
*   to assemble frames using this routine as a tool.  It does not
*   modify the frame.  One rare case that needs to be understood is
*   that if an input stream buffer ends on a DLE, the internal
*   state machine will not place it into the output buffer.  Instead,
*   it gets placed into the following input buffer.  This delay is needed
*   for the two character DLE-STX and DLE-ETX checks.   In practice the
*   this means that when either flag is true, at least two characters
*   are in the current buffer
*
*   The caller of the routine should also be aware that the returned
*   information for a frame boundry (XSTX and XETX) is valid after the two
*   character code is placed into the buffer.  This is important when the
*   between frame data must be handled (tossed of sent elsewhere). It ends
*   two characters before the XSTX is detected and starts with the character
*   the XETX detection flag.
*
*   To ease the caller's work, we keep an "inframe" detector which is
*   inc/dec. Outside the frame it is zero. When a DLE-STX comes in it is
*   incremented. A DLE-ETX does a decrement.  So it should be 0 or 1. Any
*   other value means some garbage came in and the caller needs to take
*   care of things
*
*****************************************************************************
*/
int MonitorFrame(t_framer* fp)
{
    int kin;
    int kout;
    char c;

    while( (fp->cBufin < fp->szBufin) && (fp->cBufout < fp->szBufOut-2))
    {
        c=fp->Bufin[fp->cBufin++];              // get the new character
#if (EASYWINDEBUG>5) && 0
printf("MonitorFrame: c=0x%0x fp->Bufin 0x%08x,fp->Bufout 0x%08x\n",c,fp->Bufin,fp->Bufout);
#endif

        switch(fp->state)                       // 2-state check
        {
            //========
            case 0:  // new character
            //========
            {
                if(c == XDLE)
                {
                    fp->state = 1;              // DLE escape state change
                }
                else
                {
                    fp->Bufout[fp->cBufout++]=c;// Copy
                }
            }
            break;

            //========
            case 1:  // DLE inside a frame
            //========
            {
                fp->state = 0;                 // return to inside a frame
                switch(c)
                {
                    case XETX:
                        fp->inframe -= 1;       // track for any caller
                        fp->Bufout[fp->cBufout++]=XDLE;// insert the pending DLE
                        fp->Bufout[fp->cBufout++]=c;// Copy
                        return(XETX);           // frame end
                    //break;

                    case XSTX:
                        fp->inframe = +1;       // track for any caller
                        fp->Bufout[fp->cBufout++]=XDLE;// insert the pending DLE
                        fp->Bufout[fp->cBufout++]=c;// Copy
                        return(XSTX);           // frame start
                    //break;

                    default:
                        fp->Bufout[fp->cBufout++]=XDLE;// insert the pending DLE
                        fp->Bufout[fp->cBufout++]=c;// Copy
                    break;
                }
            }
            break;

        }
    }

    // return codes
    if(fp->cBufout >= fp->szBufOut)
        return(XEOF);                          // ran out of output buffer
    else if(fp->state)
        return(XDLE);                          // ran out of buffer in a DLE
    else
        return(RTNC);                          // ran out of buffer
}
#endif

/*
**************************************************************************
*
*  Function: static int CopyNextDiCharA(t_framer* fp)
*
*  Purpose: Copy character that could have DLE stuffing
*
*   Arguments:
*       fp      Pointer to the framer structure
*       peekf   If true processes the character but does not copy or change DLE flag
*
*   Return:  In order of test (for a tie break rule)
*
*       RTNC    Input buffer ended
*       RTNO    No room in output buffer
*       XSTX    DLE-STX encountered
*       XETX    DLE-ETX encountered
*       XDLE    DLE encountered
*       RTCHAR  Character processed
*
*  Comments:
*
*   Workhorse to process escape sequences. Generally testing for ties
*   is listed order.
*       1) No input to process
*       2) No output room
*       3) STX|ETX|DLE
*       4) Char
*
*   When there is no room, pointers and state changes are not done. That is,
*   neither input nor output pointers move and the DLE state remains as it
*   was upon entry
*
**************************************************************************
*/
int CopyNextDiCharA(t_framer* fp)
{
    int     c;
    int     retval;

    if(fp->cBufin >= fp->szBufin)               // check for input buffer underrun
        return RTNC;                            // return so cBufin does not change

    c = 0xff & fp->Bufin[fp->cBufin];           // look at the new character

    if(fp->state)                               // are we in a DLE escape?
    {
        if( (fp->cBufout + 2) >= fp->szBufOut)
            return RTNO;                        // return so cBufin does not change

        fp->state = 0;                          // out of escape
        fp->Bufout[fp->cBufout++]=XDLE;         // insert the pending DLE
        fp->Bufout[fp->cBufout++]=c;            // Copy

        switch(c)
        {
            //========
            // case XDLE:  // DLE-DLE
            case XSTX:  // DLE-STX
            case XETX:  // DLE-ETX            
            //========
            {
                retval = c;                     // c == escape code
#if (EASYWINDEBUG>3) && 0
if(inicfg.prflags & PRFLG_OTHER)
{
printf("CopyNextDiCharA: Frame: %d\n",retval);
}
#endif
            }
            break;


            //========
            default:
            //========
            {
                retval = RTCHAR;                // character
            }
            break;

        }
    }
    else                                        // Not in DLE mode
    {
        if( (fp->cBufout + 1) >= fp->szBufOut)
            return RTNO;                        // return so cBufin does not change

        if(c == XDLE)
        {
            fp->state = 1;
            retval = XDLE;
        }
        else
        {
            fp->Bufout[fp->cBufout++]=c;        // Copy
            retval = RTCHAR;
        }
    }
    fp->cBufin += 1;                            // got this far, bump the input index

    return retval;
}

/*
**************************************************************************
*
*  Function: static int CopyNextUsCharA(t_framer* fp)
*
*  Purpose: From.
*
*   Arguments:
*       fp      Pointer to the framer structure
*
*   Return:  In order of test (for a tie break rule)
*
*       RTNC    Input buffer ended
*       RTNO    No room in output buffer
*       RTCHAR  Character processed
*
*  Comments:
*
*   Used for ADDR|SEQ bytes. This baiscally is a raw character copy
*   with the buffer check. No DLE sequences are examined
*
* <<< These bytes are not allowed to be ASCII control characters >>>
* <<< Specifically STX ETX DLE XON XOFF ^| NUL                   >>>
*
**************************************************************************
*/
int CopyNextUsCharA(t_framer* fp)
{
    if(fp->cBufin >= fp->szBufin)               // check for input buffer underrun
        return RTNC;                            // return so cBufin does not change

    if( (fp->cBufout + 1) >= fp->szBufOut)
        return RTNO;                            // return so cBufin does not change

    fp->Bufout[fp->cBufout++] = fp->Bufin[fp->cBufin++];

    return RTCHAR;
}
/*
**************************************************************************
*
*  Function: int DoMonStreamA(char *bufIn, int nIn, int srcID, t_ctldatapkt** CurrMonFrame, t_framer* fp)
*
*  Purpose: Reads a stream and reassembles on frame boundries.
*
*   Arguments:
*       bufin:      pointer to the input buffer
*       nIn:        number of characters in the input bffer
*       srcID:      ID to tag frames with (EVB,TCP,LOCAL)
*       CurrMonFrame: pointer to hold the (t_ctldatapkt*) we are assemblong
*       fp:         pointer to the framer structure
*
*  Comments:
*   This is a rather complex routine.  Each stream input (UART,TCP) is assumed
*   to be a pure stream that does not honor our internal packet boundries.  Hence
*   blocks of data need to be monitored and broken (or aggragated) into the
*   internal packets.  The major complexity comes from the need to handle
*   data that exists between valid frames (UnFramed data).  In this application,
*   only the EVB port mixes framed and unframed data.  But by solving the
*   general problem, the same routine can be used on the TCP streams that carry
*   packetized data but not necessarily on internal buffer boundries.
*
*   This routine is now state oriented to track where in a frame it expects to be.
*   It is also self initializing. The variable "CurrMonFrame" should be set
*   to NULL at the beginning.  This routine will set up internal variables
*   and get a working packet buffer.  At each call it wants a new input buffer
*   and will process all characters in the buffer.  But leave the framer alone.
*   Upon shutdown, the working packet should be released back to the
*   system but otherwise, leave that alone as well.
*
*   New version.  The old one have problems when unframed data hit the buddef
*   limit.  It was not designed for packets that were larger of equal to the
*   buffer sizes. One problem was that a large packet was broken up. The first part
*   was framed then the second part was reframed leading to the DLE-ETX being
*   treated as a data.  I'm not sure of the actual mechanisms but it appears that
*   the TCP monitor was reframing the end of a packet because the server got
*   correct data but the client was screwed up.
*
*   This monitors the stream, it does not deframe it.  All input characters
*   are copied to the current output buffer.  It does look for frame boundries
*   and "reassemble" packets so the start of a packet is also the start
*   of a frame. Unframed data are "ReFramed" into special packets with TID_UNFRAMED.
*   The result is that any internal packet is a frame. There is some logic for
*   handling frames longer than an internal buffer sizes.  If assembling an
*   UNFRAMED packet, the data is ReFramed so this is not an issue. If the buffers
*   overflow when assembling a framed packet, the data is shipped off wo the
*   ending <DLE><ETX> in hope that the other side handles it. We don't really have
*   a good way to test this because PC buffers are larger than EVB (and will reamin so)
*
**************************************************************************
*/
#if 1
int DoMonStream(char *bufIn, int nIn, int srcID, t_ctldatapkt** CurrMonFrame, t_framer* fp)
{
    int k;
    int cpyret;
    int loopf;
    t_ctldatapkt* ThisFrame;

    // CurrMonFrame normally is valid. We'll check and allocate one if
    // needed. Expectation is that this will only be done once when we
    // fire up forgetting to initialize
    ThisFrame = *CurrMonFrame;
    if(!ThisFrame)                              // check exist and allocate
    {
        ThisFrame = AllocDataPacket(MAXPACKETSIZE, &gRouterCS);
        if(!ThisFrame)
            FatalError("DoMonStream:AllocDataPacket fail 1");
        *CurrMonFrame=ThisFrame;
        fp->Bufout = ThisFrame->pData;          // reset framer stuff
        fp->szBufOut = ThisFrame->nData;
        fp->cBufout = 0;
        fp->state = 0;
        fp->inframe = FRM_OUTSIDE;
        fp->id = TID_UNFRAMED;
        fp->seq = SEQNUM_DEFAULT;
#if (EASYWINDEBUG>2) && 1
if(inicfg.prflags & PRFLG_OTHER)
{
printf("DoMonStream:first ThisFrame 0x%08x pCurrMonFrame 0x%08x\n",ThisFrame,CurrMonFrame);
}
#endif
    }
#if (EASYWINDEBUG>2) && 1
if(inicfg.prflags & PRFLG_OTHER)
{
printf("(srcID=0x%04x) nIn=%d\n",srcID,nIn);
jlpDumpFrame("DoMonStream",bufIn,nIn);
//jlpChkDataPktHdr("DoMonStream",ThisFrame);
}
#endif
    //
    // Ok, that out of the way, let's start the real work

    fp->Bufin = bufIn;                          // set up input buffer
    fp->szBufin = nIn;
    fp->cBufin = 0;
    loopf = 1;
    while(loopf)
    {
#if (EASYWINDEBUG>3) && 1
if(inicfg.prflags & PRFLG_OTHER)
{
printf("DoMonStream:fp->inframe %d, fp->state %d\n",fp->inframe,fp->state);
}
#endif
        switch(fp->inframe)
        {
            //========
            default:  // get address byte            
            //========
                FatalError("Who killed inframe");
                loopf=0;
            break;

            //========
            case FRM_ADDR:  // get address byte            
            //========
            {
                cpyret = CopyNextUsCharA(fp);        // Copy raw character
                switch(cpyret)
                {
                    //========
                    case RTCHAR:    // copied one
                    //========
                        fp->id = fp->Bufout[fp->cBufout - 1];
                        fp->inframe = FRM_SEQ;
                    break;

                    //========
                    case RTNC:      // no input character
                    case RTNO:      // no output buffer (cant't happen)
                    //========
                        loopf = 0;              // break out for the next buffer
                    break;

                }
            }
            break;

            //========
            case FRM_SEQ:  // get sequence byte            
            //========
            {
                cpyret = CopyNextUsCharA(fp);        // Copy raw character
                switch(cpyret)
                {
                    //========
                    case RTCHAR:    // copied one
                    //========
                        fp->seq = fp->Bufout[fp->cBufout - 1];
                        fp->inframe = FRM_INSIDE;
                    break;

                    //========
                    case RTNC:      // no input character
                    case RTNO:      // no output buffer (cant't happen)
                    //========
                        loopf = 0;              // break out for the next buffer
                    break;

                }
            }
            break;

            //========
            case FRM_OUTSIDE:  // Outside a frame, want DLE-STX            
            //========
            {
                cpyret = CopyNextDiCharA(fp);        // Copy DiCharacter
#if (EASYWINDEBUG>3) && 1
if(inicfg.prflags & PRFLG_OTHER)
{
printf("FRM_OUTSIDE: cpyret %d\n",cpyret);
}
#endif
                switch(cpyret)
                {
                    //========
                    case RTCHAR:    // copied one 
                    case XDLE:      // hit a DLE 
                    //========
                                    // continue processing
                    break;

                    //========
                    case RTNC:      // no input character
                    //========
                    {
                        SendUnFramed(fp->Bufout,fp->cBufout,srcID);
                        fp->cBufout=0;              // continue processing
                        loopf = 0;              // break out for the next buffer
                    }
                    break;

                    //========
                    case RTNO:      // no output buffer
                    //========
                    {
                        SendUnFramed(fp->Bufout,fp->cBufout,srcID);
                        fp->cBufout=0;              // continue processing
                    }
                    break;

                    //========
                    case XSTX:      // Found a frame start
                    //========
                    {
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_OTHER)
{
jlpDumpFrame("DoMonStreamXSTX",fp->Bufout,fp->cBufout-2);
}
#endif
                        SendUnFramed(fp->Bufout,fp->cBufout-2,srcID);
                        fp->cBufout=0;              // continue processing
                        fp->Bufout[fp->cBufout++]=XDLE; // insert the skipped DLE
                        fp->Bufout[fp->cBufout++]=XSTX; // insert the skipped STX
                        fp->inframe = FRM_ADDR;
                    }
                    break;

                    //========
                    case XETX:      // How does this happen? random data?
                    //========
                                    // continue processing SendUnFramed will escape it
                    break;
                }
            }
            break;

            //========
            case FRM_INSIDE:  // Outside a frame, want DLE-ETX            
            //========
            {
                cpyret = CopyNextDiCharA(fp);        // Copy DiCharacter
                switch(cpyret)
                {
                    //========
                    case RTCHAR:    // copied one 
                    case XDLE:      // hit a DLE 
                    //========
                    break;

                    //========
                    case RTNC:      // no input character
                    //========
                        loopf = 0;              // break out for the next buffer
                    break;

                    //========
                    case RTNO:      // no output buffer
                    //========
                    {
                        ThisFrame->SrcId = fp->id;        // copy address
                        ThisFrame->seqNum = fp->seq;      // copy sequence
                        ThisFrame->nData = fp->cBufout;   // get byte count
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_OTHER)
{
jlpDumpFrame("DoMonStreamRTXOa",fp->Bufout,fp->cBufout);
printf("CurrMonFrame 0x%08x ThisFrame 0x%08x\n",CurrMonFrame,ThisFrame);
}
#endif
                        SendFrameToRouter(ThisFrame,srcID+0x200); // ship it
                        ThisFrame = AllocDataPacket(MAXPACKETSIZE, &gRouterCS);
                        if(!ThisFrame)
                            FatalError("DoMonStream:AllocDataPacket fail 2");
                        *CurrMonFrame = ThisFrame;

                        fp->Bufout = ThisFrame->pData;     // reset framer stuff
                        fp->szBufOut = ThisFrame->nData;
                        fp->cBufout = 0;
                    }
                    break;

                    //========
                    case XETX:      // Found a frame end
                    //========
                    {
                        ThisFrame->SrcId = fp->id;        // copy address
                        ThisFrame->seqNum = fp->seq;      // copy sequence
                        ThisFrame->nData = fp->cBufout;   // get byte count
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_OTHER)
{
jlpDumpFrame("DoMonStreamXETXa",fp->Bufout,fp->cBufout);
printf("CurrMonFrame 0x%08x ThisFrame 0x%08x\n",CurrMonFrame,ThisFrame);
}
#endif
                        SendFrameToRouter(ThisFrame,srcID+0x200); // ship it

                        ThisFrame = AllocDataPacket(MAXPACKETSIZE, &gRouterCS);
                        if(!ThisFrame)
                            FatalError("DoMonStream:AllocDataPacket fail 2");
                        *CurrMonFrame = ThisFrame;

                        fp->Bufout = ThisFrame->pData;     // reset framer stuff
                        fp->szBufOut = ThisFrame->nData;
                        fp->cBufout = 0;
                        fp->id = TID_UNFRAMED;
                        fp->seq = SEQNUM_DEFAULT;
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_OTHER)
{
jlpDumpFrame("DoMonStreamXETXb",fp->Bufout,fp->cBufout);
printf("fp->Bufout 0x%08x ThisFrame->pData 0x%08x\n",fp->Bufout,ThisFrame->pData);
}
#endif
                        fp->inframe = FRM_OUTSIDE;
                    }
                    break;

                    //========
                    case XSTX:      // Framing error
                    //========
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_OTHER)
{
//jlpDumpFrame("DoMonStreamXSTXb",fp->Bufout,fp->cBufout);
printf("Found XSTX inside a frame\n");
}
#endif
                        fp->cBufout = 0;
                        fp->id = TID_UNFRAMED;
                        fp->seq = SEQNUM_DEFAULT;
                        fp->inframe = FRM_OUTSIDE;
                    break;
                }
            }
            break;
        }
    }
#if (EASYWINDEBUG>5) && 1
if(inicfg.prflags & PRFLG_OTHER)
{
printf("fp->inframe %d cpyret %02x fp->cBufin %d fp->cBufout %d\n",fp->inframe,cpyret&255,fp->cBufin,fp->cBufout);
}
#endif
    return fp->inframe;
}
#endif

/*
**************************************************************************
*
*  Function: t_ctldatapkt* ReframeData(char* buf, int nBuf, int faddr, int fseq)
*
*  Purpose: Build a data packet from data
*
*  Comments:
*   Common routine to take a buffer of unframed data and build a
*   data packet.  The control fields of the packet are defaulted
*   and must be put in. The sequence number and address are in the
*   argument list because these are part of the frame itself.
*
*   This routine allocates a data packet that must be released elsewhere .
*
*   If packet allocation fails, a NULL is returned
**************************************************************************
*/
t_ctldatapkt* ReframeData(char* buf, int nBuf, int faddr, int fseq)
{
    t_ctldatapkt*   pPayload;
    t_framer    fr;

    if(nBuf < 0)                                // allow 0 length packets
        return(0);                              // nothing to do

    pPayload = (t_ctldatapkt*)AllocDataPacket(MAXPACKETSIZE, &gRouterCS);
    if(!pPayload)
        return(NULL);

    // set up framer structure
    fr.Bufin = buf;                             // input buffer
    fr.szBufin = nBuf;                          // size of input
    fr.cBufin = 0;                              // current index
    fr.Bufout = pPayload->pData;                // output buffer
    fr.szBufOut = MAXPACKETSIZE;                // size of output (max)
    fr.cBufout = 0;                             // current index
    fr.id = faddr;                              // id for a frame
    fr.seq = fseq;                              // sequence count for a frame
    fr.state = 0;                               // state machine variable
    fr.inframe = FRM_OUTSIDE;                   // in frame or not

    // build the frame
    StuffAFrame(&fr);
    pPayload->nData =  fr.cBufout;
    pPayload->SrcId = faddr;
    pPayload->seqNum = fseq;

#if (EASYWINDEBUG>5) && 0
printf("ReframeData: fr.szBufin %d -> fr.cBufout %d id = %d\n",fr.szBufin,fr.cBufout,faddr);
#endif

    return pPayload;
}


/*
**************************************************************************
*
*  Function: int SendFramed(char* buf, int nBuf)
*
*  Purpose: Send Unframed data to router
*
*  Comments:
*   When data that is not in a frame comes into the UART, it is
*   recognized by DoMonUart and this routine is called. This
*   routine builds a frame with address ADDRUNFRAMED and ships
*   it off. The endpoint should recognize this address and either
*   discard it of do other. For this implementation, unframed data
*   is probably MACSBUG and the viewer(s) plan to have a mxvtty window
**************************************************************************
*/
int SendFramed(char* buf, int nBuf, int srcID, int vspID, int seqNo)
{
    t_ctldatapkt*   pPayload;

    if(nBuf <=0)
        return(0);                              // nothing to do

#if (EASYWINDEBUG>5) && 1
if(1 || (srcID==ROUTESRCEVB))
    jlpDumpFrame("SendFramed.1",buf,nBuf);
#endif

    pPayload = ReframeData(buf, nBuf, vspID, seqNo);
    if(!pPayload)
        FatalError("SendFramed:AllocDataPacket fail 1");

#if (EASYWINDEBUG>5) && 1
printf("SendFramed len %d\n",pPayload->nData);
jlpDumpFrame("SendFramed.2",pPayload->pData,pPayload->nData);
#endif

    SendFrameToRouter(pPayload, srcID+0x300);
    return 1;
}

/*
**************************************************************************
*
*  FUNCTION: int SendUnFramed(char* buf, int nBuf, int srcID)
*
*  Purpose: Send UnFramed data to router
*
*  Comments:
*   This is a useful combination of ReframeData and  SendFrameToRouter
*   that takes a buffer, frames it and sends it to the router. It calls
*   SendFramed with a destination of TID_UNFRAMED.  Data to the EVB will
*   be unstuffed so control characters will appear at the UART. To escape
*   control characters, they must be sent to a VSP instead of TID_UNFRAMED.
*
**************************************************************************
*/
int SendUnFramed(char* buf, int nBuf, int srcID)
{
#if (EASYWINDEBUG>5) && 1
printf("SendUnFramed\n");
#endif
    return SendFramed(buf, nBuf, srcID, TID_UNFRAMED, SEQNUM_DEFAULT);
}
/*
**************************************************************************
*
*  FUNCTION: t_ctldatapkt*  DeFramePacket(t_ctldatapkt* pkIn, int* faddr, int* fseq)
*
*  PURPOSE: Build a new packet with the framing removed
*
*  COMMENTS:
*
*   This is the inverse of ReFrame.  It is used by muxrte to send buffers
*   to the tty windows, by muxevb to send unframed data and for deframing
*   of control data.  The output is stored in a packet buffer that must
*   be released after use.  However the contents of the buffer is just
*   user data with no stuffing bytes or framing left.  We could have
*   specified a data pointer instead but the packet is a convient memory
*   management thing.  The first byte of the output packet (buf[0]) is
*   the first byte of data, not a framing code.
*
*   The routine uses the local packet management routine to allocate
*   memory. On success, two packets exist, the input and the new output.
*   On failure, only the input packet remains.
*
*   DeFraming assumes the input packet is a valid frame. It skips the
*   first 4 bytes (<DLE><STX><ADDR><SEQ>), sets the state variable to
*   "INFRAME" and processes the remainder. There is no checking on the
*   input frame for being valid.  The user has to ensure only a fully framed
*   packet is the input
*
*   This routine allocates a data packet that must be released elsewhere .
*
*   If packet allocation fails, a NULL is returned
**************************************************************************
*/
t_ctldatapkt*  DeFramePacket(t_ctldatapkt* pkIn, int* faddr, int* fseq)
{
        t_ctldatapkt*   pPayload;
        t_framer        fr;
        char*           pChar;
        int             retval;

        if(pkIn->nData < 6)                     // a valid packet has at least 6 frameing bytes
            return(NULL);
            
        pChar = (char*)pkIn->pData;
        if(faddr)
            *faddr = pChar[2] & 255;
        if(fseq)
            *fseq = pChar[3] & 255;

        pPayload = (t_ctldatapkt*)AllocDataPacket(MAXPACKETSIZE, &gRouterCS);
        if(!pPayload)
            return(NULL);

        // set up framer structure
        fr.Bufin = pChar+4;                 // input buffer
        fr.szBufin = pkIn->nData-4;               // size of input
        fr.cBufin = 0;                          // current index
        fr.Bufout = pPayload->pData;            // output buffer
        fr.szBufOut = MAXDATASIZE+MAXDATAOVER+2;  // size of output (max data + control + DLE-ETX
        fr.cBufout = 0;                         // current index
        fr.id = 0;                              // id for a frame
        fr.seq = 0;                             // sequence count for a frame
        fr.state = 0;                           // state machine variable
        fr.inframe = FRM_INSIDE;               // in frame or not

        // unstuff the frame
        retval = UnStuffFrame(&fr);
#if EASYWINDEBUG & 0
printf("DeFramePacket: retval %d fr.cBufout %d fr.cBufin %d\n",retval,fr.cBufout,fr.cBufin);
if( (retval == 3) && (fr.cBufout < 10) && 0)
  jlpDumpFrame("DeFramePacket.1",pkIn->pData,pkIn->nData);
#endif
        switch(retval)
        {
            //========
            case XETX:
            //========
                pPayload->nData =  fr.cBufout; // found DLE-ETX (it was not stored)
            break;
            
            //========
            default:
            case RTNC:
            //========
                pPayload->nData =  fr.cBufout;  // buffer end
            break;
        }
        if(pPayload->nData<0)                   // this should never happen but trap anyway
            pPayload->nData=0;
        pPayload->SrcId = pkIn->SrcId;          // copy from
        pPayload->seqNum = pkIn->seqNum;        // copy seq

    return pPayload;
}

/*
**************************************************************************
*
*  Function: int ExtractAddrSeq(t_ctldatapkt* pkIn, int* pAddr, int* pSeq)
*
*  Purpose: Extract the address and sequence bytes from a framed packet
*   This routine extracts the first two data bytes from a framed
*   packet.
*
*   Parameters
*       t_ctldatapkt*   pkIn    Data packet
*       int*            Addr    Pointer to int for address
*       int*            Seq     Pointer to int for data
*
*   Return
*       Returns number of bytes to the offset of the remaining data
*       .OR. -1 for an error
*
*   Comments
*     A call to the deframer is made.  This is a few extra cycles but
*     it keeps the escape character code in a single place.
*
*     pAddr and pseq can be NULL if this is used only to count
*     the bytes to skip
*
*   If packet allocation fails, a NULL is returned
**************************************************************************
*/
int ExtractAddrSeq(char* bufin, int* pAddr, int* pSeq)
{
    t_framer    fr;
    char        buf[8];

    memset(buf,255,8);
    fr.Bufin = bufin;                       // input buffer
    fr.szBufin = 8;                         // size of input (>4+2)
    fr.cBufin = 0;                          // current index
    fr.Bufout = buf;                        // output buffer
    fr.szBufOut = 2;                        // size of output (max)
    fr.cBufout = 0;                         // current index
    fr.id = 0;                              // id for a frame
    fr.seq = 0;                             // sequence count for a frame
    fr.state = 0;                           // state machine variable
    fr.inframe = FRM_OUTSIDE;               // in frame or not
    // unbuild the frame
    UnStuffFrame(&fr);
    
    if(pAddr) *pAddr=0xff&buf[0];
    if(pSeq) *pSeq=0xff&buf[1];
#if EASYWINDEBUG & 0
{ int a,b,c;
printf("DoWCtlReqAHdr 0x%08x 0x%08x 0x%08x\n",pCpkt,pDpkt,pFrame);
jlpDumpFrame("DoWCtlReqADmp",pFrame,pDpkt->nData);
printf("ExtractAddrSeq a=0x%x, b=0x%x, c=%d\n",a,b,c);
}
#endif
    
    return(fr.cBufout);
}
