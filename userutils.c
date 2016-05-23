
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <math.h>
#include <conio.h>
#include "cproto.h"
#include <alloc.h>
#include <time.h>
#include <shlobj.h>
#include <fcntl.h>
#include <io.h>
#include <dir.h>


int MyWInit(int argc, char ** argv);
int MyInit(int argc, char ** argv);
int MyDraw(int ContinueFlag);
int MyPaint(void);

/*
**************************************************************************
*
*  Function: UserMain(int TheCommand);
*
*  Purpose: Provides a graphical and a text window for simple programs
*
*  Comments:
*    Anaylsis of weight with LPC
*
*
**************************************************************************
*/

#define     ONESECOND 1
#define     ONEMINUTE 60
#define     ONEHOUR (ONEMINUTE*60)
#define     ONEDAY (ONEHOUR*24)

int ReadWeightData(void);
int DSample1(long* pTRaw, float* pSRaw, int nRaw, float* pSample, int nSample, float tStart);
static int StringToSDate(char *str_in, struct tm *tout);
static int StringToSTime(char *str_in, struct tm *tout);
static int PlotXX(
    float*  ydata,                  // array
    int     nmPlot,              // number of samples
    float   ymin, float ymax,          // y bounds
    float   xpixels, float ypixels,      // scale factor
    int     ix0, int iy0,             // offsets
    int     colour, int marker
    );


/*======================================================================*/
// Add your functions here.
/*======================================================================*/
/*
**************************************************************************
*
*  Function: int StringToSDate(char *str_in, char *str_out);
*
*  Purpose: Date recognizer
*
*  Comments:
*
*  Lifted from tinyedit
*
* This is a real pain because there are so many possibilities
* An overview: the details are in the code
*
* Divide into two groups
*  1) has a textual month
*  2) only numerics
*
* First group (with text)
*  The text must match the full or abbreviated month name
*  and this must be the only text characters in the string
*       errors -9 and -10
*
*  if there is no number or more than two in the string fail
*       error -1
*
*  if there is exactly one number, it must be the year
*  and we get mm/1/yyyy
*
*  if there are 2 numbers if either one is larger than 31
*  it must be the year ant the day must be less or equal to 31
*
*  if both are less_equal 31 it is ambigouous except that if
*  one is exactly 00, it becomes 2000 and the other the day
*  The ambiguity is resolved by assuming the last number is the
*  year
*
* Second group (no text)
*
*   A single number becomes the year and we get 1/1/yyyy  
*
*   exactly 2 numbers are month/year. We find the one that is
*   larger than 12 and call it the year. Otherwise
*   it is ambigious. No special chack for the year 2000 is done
*   because something like 04-00 can hardly be considered
*   a date (one can argue that apr-00 is ok)
*
*   with 3 numbers it is harder
*   if the first is larger than 31 we force yy mm dd.
*   the format yy-dd-mm is illegal.
*  otherwise we look at the two cases
*  mm-dd-yy (USA) and dd-mm-yy (European). When
*  both mm and dd candidates are <= 12, we give up.
*
*  finall we check the numbers against the range
*  
*
**************************************************************************
*/
const char *moStr[]={
"january","february","march",
"april","may","june",
"july","august","september",
"october","november","december",
"jan","feb","mar","apr","may","jun",
"jul","aug","sep","oct","nov","dec"
};

static int StringToSDate(char *str_in, struct tm *tout)
{
    int dd,mm,yy;       // build date in these
    int numbers[4];     // converted numbers
    int numpos[4];        // positions of numbers
    int mpos;           // txt month position (if any)
    int nnums;          // number of converted numbers [0..3]
    int i,j,k;          // general use
    int flag;
    int d_state;
    char *s1,*s2;
    int retval;

/*
*---
* First check, lets see if it has a text month
*---
*/
    mm=0;
    mpos=-1;
    d_state=0;
    for(i=0;i<24;i++)
    {

        s1=strstr(str_in,(char*)moStr[i]);
        if(s1)
        {
            mm=i+1;         // yep this is the month
            mpos=(s1-str_in);         // the position is here
            break;
        }
    }

        // count alpha
    flag=0;
    s1=str_in;
    while(*s1)
    {
        if(isalpha(*s1))
            flag +=1;
        s1+=1;
    }
#if EASYWINDEBUG & 0
printf("flag %d mm %d\n",flag,mm);
#endif

    
    if( (flag>0) && (mm==0) )
    {
        return(-9); // no match and there are ascii characters
    }
    else if(mm>0)
    {
        if(flag!=(int)strlen(moStr[mm-1]))
            return(-10);    // the total ascii characters is different from the matched
        
    }

    if(mm>12)       // adjust for the long month group
        mm -=12;

/*
*---
* Moving along, lets find all numbers and their positions
*---
*/
    nnums=0;
    s1=str_in;
    j=1;
    while(*s1 && nnums<3)
    {
        numbers[nnums]=-1;
        numpos[nnums]=-1;
            // find a digit
        while(!isdigit(*s1) && (*s1))
        {
            ++s1;
            ++j;
        }
        if(!*s1)    
            break;      // ran out of string

        numpos[nnums]=j;
            // find a not digit
        s2=s1;
        while(isdigit(*s2) && (*s2))
        {
            ++s2;
            ++j;
        }
            // convert
        numbers[nnums]=atoi(s1);
        nnums +=1;
        s1=s2;
    }

#if EASYWINDEBUG & 0
printf("%s mm %d nnums %d %d %d %d\n",str_in,mm,nnums,numbers[0],numbers[1],numbers[2]);
#endif

/*
*---
* Start the logic checks. First divide between a fully numeric
* date (3/4/1988) and one that has a text month somewhere in the string
*---
*/
    yy = -1;
    dd = -1;
    if(mm)      
    {
        switch(nnums)   // text month is true
        {
            case 0:     // can't have a month only
            case 3:     // can't have 3 digits
                return(-1);
//            break;    

            case 1:     // single number, must be a year
                dd=1;
                yy=numbers[0];
            break;

            case 2:     // two numbers
                if( (numbers[0]==0) && (numbers[1]>0))
                {
                    yy=numbers[0];  // special for the year 2000
                    dd=numbers[1];
            
                }
                else if( (numbers[0]>0) && (numbers[1]==0))
                {
                    yy=numbers[1];
                    dd=numbers[0];  // special for the year 2000
            
                }
                else if( (numbers[0] >31) && (numbers[1] <=31) )
                {
                    yy=numbers[0];
                    dd=numbers[1];
                }
                else if( (numbers[0] <= 31) && (numbers[1] > 31) )
                {
                    yy=numbers[1];
                    dd=numbers[0];
                }
                else if( (numbers[0] <= 31) && (numbers[1] <= 31) )
                {
                    if( (numpos[0]<mpos) && (numpos[1]>mpos))  // nn-aug-mm
                    {
                        dd=numbers[0];  // default to year last in all cases
                        yy=numbers[1];
                    }
                    else
                        return(-106);
                }
                else
                    return(-3);     // (99 66) illegal
                    
            break;

            default:
            break;
        }
    }
    else // text month is false. 
    {
        switch(nnums)   // number of digits without a text month
        {
            case 0:     // trivial, nothing here
                return(-4);
//            break;    

            case 1:     // single number, must be a year
                dd=1;
                mm=1;
                yy=numbers[0];
            break;

            case 2:     // two numbers force a month year pair
                dd=1;   // default to the first day of the month
                if( (numbers[0] >12) && (numbers[1] <=12) )
                {
                    yy=numbers[0];
                    mm=numbers[1];
                }
                else if( (numbers[0] <= 12) && (numbers[1] > 12) )
                {
                    yy=numbers[1];
                    mm=numbers[0];
                }
                else if( (numbers[0] <= 12) && (numbers[1] <= 12) )
                {
                    return(-105);     // (03 04) ambigous
                }
                else
                    return(-6);     // (99 66) illegal
                    
            break;

            case 3:     // this is hard 3 digits in any order

                if(numbers[0] > 31)   // this will be yyyy mm dd
                {
                    yy=numbers[0]; 
                    mm=numbers[1];
                    dd=numbers[2];
                }
                else
                {
                    yy=numbers[2];      // force the year to the end
                    if((numbers[0]>12) && (numbers[1]<=12))
                    {
                        dd=numbers[0];  // European format dd/mm/yy (13..30/mm/yy)
                        mm=numbers[1];
                    
                    }
                    else if((numbers[0]<=12) && (numbers[1]>12))
                    {
                        dd=numbers[1];  // USA format mm/dd/yy (mm/13..30/yy)
                        mm=numbers[0];
                    }
                    else if((numbers[0]<=12) && (numbers[1]<=12))
                    {
                        dd=numbers[1];  // ambigious, don't error
                        mm=numbers[0];  // force USA format
                    }
                    else
                        return(-5);     // illgal, we have at least a bad month
                }
            break;

            default:
            break;
        }
    }

/*
*---
* Final checks, day amd month have to be in range
*---
*/
#if EASYWINDEBUG & 0
    printf("mm=%2d dd= %2d yy=%4d\n",mm,dd,yy);
#endif


    if( (dd<1) || (dd>31) )
        return(-5);

    if( (mm<1) || (mm>12) )
        return(-7);

    /*
    ** year checks
    */
    if(yy<50)                               // small 2 digit year probably 20yy
    {
        yy +=2000;
    }
    else if(yy<1000)                        // 3 digit year probably 1900+yyy
    {
        yy +=1900;
    }
//    else                                  // full 4 digit year

    tout->tm_mon = mm;
    tout->tm_mday = dd;
    tout->tm_year = yy;

    return(0);

}


/*
**************************************************************************
*
*  Function: int StringToSTime(char *str_in, char *str_out);
*
*  Purpose: Time recognizer
*
*  Comments:
*
*   This primarily is used when 12hr .OR. 24hr units are allowed
*
*    3      03:00   3AM
*   3:29    03:29   3:29AM
*   13:01   13:01   1:01PM
*   3PM
*   3AM
*   12AM    00:00
*   12:30AM 00:30
*   12PM    12:00
*
*   The program is modulo 24. It will not add a day for the
*   values 12AM .. 12:59AM
*
*   -10 illegal
*   -11 no tokens
*   -12 bad number token
*/
static const char PuncDate[]="\r\n:,/";

static int StringToSTime(char *str_in, struct tm *tout)
{
    int k,n;
    int ntoks=0;                        // number of tokens
    int hh,mm;
    char *s1,*s2,*s3;

/*
*---
* Get 1 or 2 fields (hh:mm)
*---
*/

    s1=strtok(str_in,PuncDate);
    if(s1)
    {
        ntoks = 1;
    }
    else
    {
        return(-11);                    // no token
    }
    s2=strtok(NULL,PuncDate);           // is there a second field
    if(s2)
    {
        ntoks = 2;
    }

#if EASYWINDEBUG & 0
printf("<%s> <%s>\n",s1,s2);
#endif

    hh = 0;
    k=sscanf(s1,"%d",&hh);              // convert 1st
    if(k<=0)                
        return(-12);                    // can't -> error

    mm = 0;
    if(ntoks >= 2)
    {
        s3=s2;
        while(strpbrk (s3,"0123456789"))
            s3 += 1;
#if EASYWINDEBUG & 0
printf("s3=%x\n",*s3);
#endif

        if(s3)                        // digits
        {
            switch(*s3)
            {
        		case 'p':
        		case 'P':
                    hh = (hh%12) + 12;
        		break;

        		case 'a':
        		case 'A':
                    hh = (hh%12);
        		break;

                default:
                break;
            }

            *s3=0;                    // terminate 
            sscanf(s2,"%d",&mm);
        }
    }

    tout->tm_min = mm%60;
    tout->tm_hour = hh%24 ;

return(0);
}


