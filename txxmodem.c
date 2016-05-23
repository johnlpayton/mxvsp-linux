/* ========================================================================== */
/*                                                                            */
/*   Filename.c                                                               */
/*   Send a block with Xmodem                                                 */
/*                                                                            */
/*   Description                                                              */
/*                                                                            */
/* ========================================================================== */

#define USEEBV 0
#define USEGDK 0
#define USEWIN32 0
#define USEMXVSP 1

#if USEEBV
#include "kernel.h"
#include "kuart5235.h"
#include "k_iproto.h"
#endif

#if USEMXVSP
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "muxvttyW.h"
#include "muxctl.h"
#include "framer.h"
#include "mttty.h"
#endif

#define XMODEMDEBUG 0


#define CHARSOH 0x01
#define CHAREOT 0x04
#define CHARACK 0x06
#define CHARNAK 0x15
#define CHARCAN 0x18
#define CHARSUB 0x1a
#define STATENEWBLOCK 1
#define STATESENDBLOCK 2
#define STATEWAITACK 3
#define STATESENDEOT 4
#define STATEEOTSENT 5
#define STATEABORT 6
#define STATEINIT 7
#define BLKSIZE (128+4)
#define NRETRY 5


#if 0 // Here is the EVB send
int XmodemMakeBlk(char* pMem, int nb, int blkno, char* blk)
{
    int k,kopy,csum;

    memset(blk,CHARSUB,BLKSIZE);    // fill with ^Z
    blk[0] = CHARSOH;               // SOH
    blk[1] = blkno;                 // BLK#
    blk[2] = 0xff^blkno;            // ~BLK#
    kopy = nb;                      // validate data
    if(kopy > 128)                  // larger than packet
        kopy = 128;                 // clamp
    if(kopy > 0)                    // positive
        memcpy(&(blk[3]),pMem,kopy); // copy
    csum = 0;                       // checksum packet
    for(k=0;k<128;k++)
        csum += blk[k+3];
    blk[BLKSIZE-1]=csum;            // is last byte
    return(kopy);                   // return data in packet (-1 is none)
}

int TXxmodem(char* MemST, int nBytes, int idDev, int HyperMode)
{
    int blkno;                   // block count
    int state;                  // state
    int nsent;                  // counts characters in a buffer
    int retry;                  // retry (NAK) counter
    int nnack;
    int garbagec;               // garbage input counter
    int k;
    char buf[128+8];
    char bufin[4];
    char tmpstr[64];

    blkno = 1;                  // starting block is #1
    garbagec = 1024;            // garbage character check
    retry = NRETRY+1;           // retry limit + first handshake
    nnack = 0;                  // count nacks we get
    nsent = XmodemMakeBlk(MemST,nBytes,blkno,buf); // prepare a buffer
    if(nsent <= 0)
        state = STATESENDEOT;   // no data, do an EOT
    else
        state = STATEWAITACK;   // wait for the receiver to request

    /* THIS uses blocking mode */
    /* we probably should make it into a character loop */
    while(1)
    {
#if XMODEMDEBUG
sprintf(tmpstr,"\r\n(%4d,%4d) ",state,blkno);
k_write(2,tmpstr,strlen(tmpstr));
#endif

        switch(state)
        {

            //========
            case STATEINIT:     // prior to sending, make a new block
            //========
            {
                k_read(idDev,bufin,1);
                buf[0] = CHARNAK;
#if XMODEMDEBUG
sprintf(tmpstr,"wrote EOT \r\n");
k_write(2,tmpstr,strlen(tmpstr));
#endif
                k_write(idDev,buf,1);
                state = STATEWAITACK;

            }

            //========
            case STATENEWBLOCK:     // prior to sending, make a new block
            //========
            {
                blkno += 1;         // New block number
                nsent = XmodemMakeBlk(MemST,nBytes,blkno,buf); // prepare a buffer
                state = STATESENDBLOCK;
                break;
            }

            //========
            case STATESENDBLOCK:     // send or resend a block
            //========
            {
                k=k_write(idDev,buf,BLKSIZE);
#if XMODEMDEBUG
sprintf(tmpstr,"Wrote (%4d) %02x %02x\r\n ",k,255&buf[1],255&buf[2]);
k_write(2,tmpstr,strlen(tmpstr));
#endif
                state = STATEWAITACK;
                break;
            }

            //========
            case STATEWAITACK:     // wait for an ACK
            //========
            {
                k_read(idDev,bufin,1);
#if XMODEMDEBUG
sprintf(tmpstr,"<%02x>",bufin[0]&255);
k_write(2,tmpstr,strlen(tmpstr));
#endif
                switch(bufin[0]&0x7f)
                {
                    //========
                    case CHARACK:   //x06
                    //========
                    {
                        MemST += nsent;
                        nBytes -= nsent;
                        if(nBytes > 0)
                            state = STATENEWBLOCK;              // new buffer
                        else
                            state = STATESENDEOT;              // to EOT

                    }
                    break;

                    //========
                    case 0x46:      // 'F' seems to be a hyperterm oddity sends 0x86 or 0xc6
                    case 0x66:      // 'f' also
                    case 0x26:      // '&'  add for fun
                    //========
                    {
                        if(!HyperMode)
                            break;
                        MemST += nsent;
                        nBytes -= nsent;
                        if(nBytes > 0)
                            state = STATENEWBLOCK;              // new buffer
                        else
                            state = STATESENDEOT;              // to EOT

                    }
                    break;

                    //========
                    case CHARNAK:   //0x15
                    //========
                    {
                        retry -= 1;
                        nnack += 1;
                        if(retry > 0)
                            state = STATESENDBLOCK;              // send again
                        else
                            state = STATEABORT;              //abort
                    }
                    break;

                    //========
                    case 0x43:          // 'C' Sense for CRC mode hyperterm
                    case 0x63:          // 'c' Sense for CRC mode hyperterm
                    //========
                    {
                        retry -= 1;
                        nnack += 1;
                        if(retry > 0)
                            state = STATESENDBLOCK;              // send again
                        else
                            state = STATEABORT;              //abort
                    }
                    break;

                    //========
                    case CHARCAN:
                    case CHARSUB:
                    //========
                    {
                        state = STATEABORT;                  // abort
                    }
                    break;

                    //========
                    default:
                    //========
                    {
                        garbagec -= 1;
                        if(garbagec < 0)
                            state = STATEABORT;              // abort
                        else
                            state = STATEWAITACK;   // stay here
                    }
                    break;
                }
                break;
            }

            //========
            case STATESENDEOT:     // send an EOT
            //========
            {
                buf[0] = CHAREOT;
#if XMODEMDEBUG
sprintf(tmpstr,"wrote EOT \r\n");
k_write(2,tmpstr,strlen(tmpstr));
#endif
                k_write(idDev,buf,1);
                state = STATEEOTSENT;
                break;
            }

            //========
            case STATEEOTSENT:
            case STATEABORT:     // exit or abort
            //========
            {
                break;
            }
        }
        if((state == STATEABORT) || (state == STATEEOTSENT))
            break;
    }
#if XMODEMDEBUG
sprintf(tmpstr,"Done: state %d nnack = %d\r\n ",state,nnack);
k_write(2,tmpstr,strlen(tmpstr));
#endif

    return state;
}
#endif