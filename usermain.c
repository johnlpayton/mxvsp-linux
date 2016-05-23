#define USELAPACK 1
#define USEUNIXTIME 1

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "cproto.h"

#if USELAPACK
#include "f2c.h"
#include "blaswrap.h"
#include "clapack.h"
#endif

#if ISWIN32
#include <windows.h>
#include <conio.h>
#include <alloc.h>
#include <time.h>
#include <shlobj.h>
#include <fcntl.h>
#include <io.h>
#include <dir.h>
#endif

#if USEGDK
#define PS_SOLID            0
#define PS_DASH             1
#define PS_DOT              2
#endif

static int MyWInit(int argc, char ** argv);
static int MyInit(int argc, char ** argv);
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

#if USEUNIXTIME
typedef time_t UTIME;
#define     TICSPERSEC 1
#else
typedef S64 UTIME;
#define     TICSPERSEC 10000000L
#endif

#define     ONESECOND (1)
#define     ONEMINUTE (60*ONESECOND)
#define     ONEHOUR (ONEMINUTE*60)
#define     ONEDAY (ONEHOUR*24)

    // linear interpolation types
#define LIN_NORM        1 //Normal Linear
#define LIN_Y1          2 //Hold the first
#define LIN_Y2          3 //Hold the last
#define LIN_AVG         4 //Hold the average (y1+y2)/2
#define LIN_MIN         5 //Hold the mimum
#define LIN_MAX         6 //Hold the max
#define LIN_PULSE       7 //Single pulse
#define LIN_INTEGL      8 //Integration left ( y1 square wave out)
#define LIN_INTEGR      9 //Integration right (y2 square wave out)


struct S_PXY
{
    float   xmin,ymin,xmax,ymax;            // data limits
    int     ix0,iy0;                        // lower left
    int     ixdelta,iydelta;                // box size
    int     ixmin,iymin,ixmax,iymax;        // computed limits
    float   xscl,yscl;
};

#if USEGDK
int _daylight=0;
#endif

double  SecPerSample = ONEHOUR/2;                       // Rate 2 samples per hour (1800 sec/sample)
double  SampPerSec;
double  SampPerDay;                                     // number of samples per day
double  SampPerHour;
double  TicPerSamp;
double  SampPerTic;
UTIME     TSamp1Start;                                    // start of sampling time (sec)
UTIME     TSamp1End;                                      // end of sampling time (sec)

struct S_PXY PltCtl1;                                   // Plot control block
struct S_PXY PltCtl2;                                   // Plot control block
struct S_PXY PltCtl3;                                   // Plot control block

#define MAXRAWDATA 2048
int     nRawData;                                       // number of raw samples
int     nDayStarts;                                     // number of day starts (<=actual daya)
int     nFirsts;                                        // same
UTIME   RawTime[MAXRAWDATA];                            // raw times
double  RawWeight[MAXRAWDATA];                          // raw weight points
double  RawUrine[MAXRAWDATA];                           // raw urine samples
int     DayStarts[MAXRAWDATA];                          // index (into Raw... }of day starts

char    RawDataFile[128]="RawWeight.txt";

// dynamic arrays sized with malloc
int     nMaxSample = 0;
int     nSample = 0;                                    // Number of primary samples
float*  Ftmp = 0;
float*  LinFit=0;                                       // General array to hold selected results
float*  Index1=0;                                       // [0,1,2,...] useful array

float*  SampHeld=0;                                    // last good sample (osolete?)
float*  Weight1Filt=0;                                 // Main weight array
float*  MaxWeight=0;                                   // Max weight array

float*  FirstDaily=0;                                  // First daily sample held over the day
float*  FirstDailyFilt=0;                              // First daily sample held over the day

float*  UrineDaily=0;                                  // Urine pulse
float*  UrineDailyAvg=0;                               // Urine averaged Daily

float*  WeightLVP=0;                                   // LVP Square wave
float*  WeightBounds=0;                                 // Boundry for LVP
UTIME   *tFirsts=0;                                     // Day starts array
double  *xFirsts=0;                                     // Values at starts

#define FITORDMAX 9
double   fitmatrix[(FITORDMAX+1)*(FITORDMAX+1)];         // matrix for the fit
double   fitrhs[FITORDMAX];                              // RHS of the fit wquation
double   fitcorr[FITORDMAX*2+1];                         // correllation products
double   fitwork[FITORDMAX*2];
float    polycoef[FITORDMAX+1];                          // Polynomial coefficients

float    RangeLo = 35;                                   // Range for poly fit
float    RangeHi = 40;
float    AvgWeight=0;                                    // average
float    AvgWeightM=0;                                    // average
float    AvgWGain=0;                                     // Linear slope = weoght gain
float    PlotStart = 0;                                  // Plot start day
float    PlotEnd = -1;                                   // Plot end day
float    ThrPain=123.0;                                  // Pain threshold
int      InterpFOD=0;                                    // Selector for FOD interpolation
int      InterpWeight=1;                                 // Selector for weight interpolation
int      MaxWeightWindow=48;                              // Window for the max weight
UTIME    tNextLVP;                                       // Time of next LVP
int      FitOrd = 1;                                     // Polynomial fit order
int      ConstUrine = 1;                                 // Selector for urine averaging
int      ConstWeight = 0;                                // Selector for weight averaging
int      ConstPlot = 1;                                  // Selector for extras plots
int      ConstLPV=2;                                     // Selector for LVP adjustment
float    SzWindow = 4;                                   // depreciated
int      DelayWeight = 0;                                // plotting delay
int      DelayUrine = 0;


static const char MM_seperator[] = " /:-.\t\r\n";       // Parsing tokens
static const char MM_white[] = " \t\r";
static const char MM_digits[] = "0123456789";

#define LVPSON 1                                        // 1 LEAVE THIS ON
#define NLVP 3                                          // length of LVP array
char    *lvpdates[NLVP]=                                // LVP array
{
"10/ 1/2010   10:01AM ",
"10/19/2010   11:43AM ",
"11/3/2010     3:35PM "
};
double   lvplbs[NLVP]={13.6,11.5,10.0};
UTIME    lvpbreaks[NLVP];

#define NBOUNDS 4                                          // length of LVP array
char    *lvpBdates[NBOUNDS]=                                // LVP array
{
" 1/ 1/2010   10:01AM ",
"10/19/2010   11:43AM ",
"11/ 3/2010    3:35PM ",
" 2/ 2/2011    1:00AM "
};

double   lvpBlbs[NBOUNDS]={123.0 ,123.0, 123.0, 125.0};
UTIME    lvpBbreaks[NBOUNDS];

/*=====Prototypes here==================================================*/

int ReadWeightData(void);
U32 GetSampleIdx(UTIME t);
int MakeDayStarts(UTIME* pTimes, int nTimes, int* DayStarts, int* nDayStarts);
int VSamp2TSeries(UTIME* pTRaw, F64* pSRaw, int nRaw, float* pSample, int nSample, unsigned long tStart);
int DSampleHold1(float* pSample, float* pHold, int nSample,
                UTIME* tfirsts, F64* xfirsts, int* nfirsts, float firstvalue);
int DSampleHold2(float* pSample, float* pHold, int nSample, float first);
int AdjLVP( F64* pfRaw, UTIME* plRaw, int nRaw,
            F64* pLVPSamp, UTIME* LVPtm, int nLVP);
int LVP2TSeries(UTIME* pTRaw, F64* pSRaw, int nRaw, float* pSample, int nSample, unsigned long tStart);
int LinInterp(int op, float* dst, float dststep, int Ndst, UTIME* x, F64* y, int Nxy, UTIME* pTS);

int PolySqRaw(UTIME* t, double* w, float RL, float RH, float* a, int nPoly);
int PolySqWin(float* w, int iRL, int iRH, float* a, int nPoly);
float PolyVal(float x, float* w, int nPoly);
int MoveAverage(float* src, int len, int ntot, float* dst);
int MakeHanning(float *dst, int N);
double macpm(float* x, float* y, int n);
int SubXY(float* dst, float* x, float* y, int n);
int ScaleX(float* dst, float scalef, float* src, int n);
int BiasX(float* dst, float* x, float bias, int n);
int ConditionBiasX(float* dst, float* x, float* y, int n);
double Xmax(float* x, int n);
double Xmin(float* x, int n);
int Conv(float* a, int na, float* b, int nb, float *dst);
int Iir(float* dst, float* src, int n, const double* a, const double*b, double* dly, int ntaps);
float NRSolvePoly(float X, float* P, int nP, float x0);
int BinSearchL(long x, long* tab, int Ntab);
int Date2Index(struct tm* date, int idx);
UTIME Pixel2Date(struct tm* date, int idx, struct S_PXY* pltctl);

F64 UtimeToSec(UTIME* utime64);
U32 UtimeToSamp(UTIME* utime64);
int UtimeToTM(UTIME* utime64, struct tm* sTime);
UTIME SecToUtime(F64* sec);
UTIME SampToUtime(S32* utime64);
UTIME TMToUtime(UTIME* utime64, struct tm* sTime);

static int StringToSDate(char *str_in, struct tm *tout);

static int PlotXYCommon(struct S_PXY* PltCtl);
static int GetPlotPoints(float x, float y, int *ix, int *iy, struct S_PXY*   PltCtl);
static int PlotXX(
    float*  ydata,                          // array
    int     nmPlot,                         // number of samples
    float   ymin, float ymax,               // y bounds
    float   xpixels, float ypixels,         // scale factor
    int     ix0, int iy0,                   // offsets
    int     colour, int marker
    );
static int PlotXY(
    float*          xdata,
    float*          ydata,                  // array
    int             nmPlot,                 // number of samples
    int             colour,
    int             marker,
    struct S_PXY*   PltCtl
    );
static int GridXY(
    float   xstep,
    float   ystep,                          // array
    int     colour,
    struct S_PXY*   PltCtl
    );
extern const double IIRTaps[];


    // Argument Array template
    // use caution when typing this. A small mistake will generate lots of compile errors

#if 1
int     DummySpacer=0;
extern t_selection SelectWindow;            // see filter coefficients section
extern t_selection SelectUrine;

t_selection SelectLVP=
{
   &ConstLPV,                  // selection variable
    {"None",
    "Pre-Adjust",
    "Pre and Post Adjust",
    0}
};

t_selection SelectPlot=
{
   &ConstPlot,                  // selection variable
    {"None",
    "Polynomial, Raw Data",
    "Weight Gain, Raw Data",
    "Polynomial FirstInDay",
    "Weight Gain FirstInDay",
    "Daily Weight Swing",
    "Maximum Daily Weight",
    "SecondOfDay Weight Gain",
    "SecondOfDay",
    "Threshold",
    0}
};

t_selection SelInterW=
{
   &InterpWeight,                  // selection variable
    {"Default",
    "Linear",
    "Left",
    "Right",
    "Mid",
    "Top",
    "Bottom",
    "Impulse",
    0}
};

t_selection SelInterFOD=
{
   &InterpFOD,                  // selection variable
    {"Default",
    "Linear",
    "Left",
    "Right",
    "Mid",
    "Top",
    "Bottom",
    "Impulse",
    0}
};

extern const int SmoothDelays[];


t_argv args[]={
{   "Plot Extra",                       F_SELECT, &SelectPlot,},
{   "Weight Averageing)",               F_SELECT, &SelectWindow,},
{   "Urine Averageing",                 F_SELECT, &SelectUrine,},
{   "LVP Mode Select",                  F_SELECT, &SelectLVP,},
{   "Order of Fit",                     F_INT,    &FitOrd,},
{   "Pain Threshold",                   F_FLOAT,  &ThrPain,},
{   ".....",                     F_INT,           &DummySpacer,},
{   "Plot Start (day)",                 F_FLOAT,  &PlotStart,},
{   "Plot End (day)",                   F_FLOAT,  &PlotEnd,},
{   "Fit Range LO (days)",              F_FLOAT,  &RangeLo,},
{   "Fit Range HI (days)",              F_FLOAT,  &RangeHi,},
{   "Interpolate Weight",               F_SELECT, &SelInterW,},
{   "Interpolate FirstOfDay",           F_SELECT, &SelInterFOD,},
{   "Primary (Sec/Sample)",             F_DOUBLE, &SecPerSample,},
{   "MaxWeightWindow",                  F_INT,    &MaxWeightWindow,},
{   "Raw Data File",                    F_STRING, RawDataFile,},
{   ".....",                     F_INT,           &DummySpacer,},
{   "Avg Weight Gain",                  F_FLOAT,  &AvgWGain,},
{   "Avg Weight",                       F_FLOAT,  &AvgWeight,},
{   "Avg FOD Weight",                   F_FLOAT,  &AvgWeightM,},
{    NULL,0,NULL} }; /* end marker */
#else
// prototype for copy-paste
int SelVarb=0;
t_selection SelectStruct=               // selection control structure
{
   &SelVarb,                            // selection variable [0..]
    {"Select 0",                                // selection text
    "Select 1",                                 // selection text
    "Select 2",                                 // selection text
    0}
};
t_argv args[]={
{    "Float Variable",          F_FLOAT,    &VarbFloat,},       // float is 32 bits
{    "Double Variable",         F_DOUBLE,   &VarbDouble,},      // double is 64 bits
{    "Integer Variable",        F_INT,      &VarbInt,},         // int is 32 bits
{    "Long Variable",           F_LONG,     &VarbLong,},        // long is also 32 bits
{    "String Variable",         F_STRING,   VarbString,},       // string is k*8 bits
{    "Selection Variable",      F_SELECT,   &SelectStruct,},    // selection needs the structure above

{    NULL,0,NULL} }; /* end marker */
#endif

/*
**************************************************************************
*
*  Function: int UserMain(int argc, char ** argv, int TheCommand)
*
*  Purpose: The main interface to the shell
*
*  Comments:
*
*   All commands come here.
*
**************************************************************************
*/

int UserMain(int argc, char ** argv, int TheCommand)
{
    int RetVal;
    int k,j,m,n;
    FILE *fout;

#if EASYWINDEBUG & 0
printf("Command=%d\n",TheCommand);
#endif


    RetVal=USERNULL;

/*===========Common Error checks ======================================*/
    if(nSample>0)
    {
        if(RangeHi >= nSample)RangeHi=nSample-1;
        if(RangeHi < 0)RangeHi=0;
        if(RangeLo >= nSample)RangeLo=nSample-1;
        if(RangeLo < 0)RangeLo=0;
    }

    switch(TheCommand)
    {
/*===========Initialization once at power on===========================*/
        case CM_G_WINIT:
            RetVal= MyWInit(argc,argv);
        break;

/*===========Initialization each time program is run===================*/
        case CM_G_INIT:
           RetVal= MyInit(argc,argv);
        break;

/*===========Paint Command When windows move, repaint the current graph */
        case CM_G_PAINT:
#if EASYWINDEBUG & 0
printf("WM_PAINT \n");
#endif

            RetVal=MyPaint();
            RetVal=USERNULL;
        break;

/*===========Draw command, first run of a program, Compute =============*/
        case CM_G_DRAW:
            newscreen();
            MyDraw(0);
            RetVal=USERCONTINUE | USERPAINT;
        break;

/*===========Continue command, continue after the first run=============*/
        case CM_G_CONTINUE:
            MyDraw(1);
            RetVal=USERCONTINUE | USERPAINT;
        break;

/*===========Close command, shut the program down and prepare to run again=*/
        case CM_G_CLOSE: /* Quit, release memory, close files etc... */
//            InitTimer(0,1000);
            RetVal=USERDONE;
        break;

/*===========Timer command for periodic events==========================*/
        case CM_G_TIMER:
            RetVal=USERNULL;
        break;

/*===========Mouse click in our Window, do whatever=====================*/
        case CM_G_MOUSED:
        {
            char tmpstr[32];
            struct tm currdate;
            char* pChar;

            if( !Weight1Filt ) return -1;

            Pixel2Date(&currdate,mouseX,&PltCtl1);
            strftime(tmpstr, sizeof(tmpstr), "%I:%M%p %a-%b-%d", &currdate);
            k=strlen(tmpstr);
            newpen(RUSTTEXT,PS_DOT+(2<<4));
            moveto(mouseX,mouseY);
            lineto(mouseX,mouseY-16);
            if(mouseX+k*8 < VDrawMax)
                drawtext(mouseX,mouseY,tmpstr,JTEXT_LEFT+JTEXT_BELOW+JTEXT_ERASE);
            else
                drawtext(mouseX,mouseY,tmpstr,JTEXT_RIGHT+JTEXT_BELOW+JTEXT_ERASE);
            RetVal=USERNULL;
        }
        break;

/*===========Function key F1 =====================================*/
        case CM_G_F1:   //
        {
            float tmid,twin;
            tmid = (RangeHi+RangeLo)/2;
            twin = RangeHi-RangeLo-1;             // window
            if(twin<1)twin=1;
            RangeHi = tmid+twin/2;
            RangeLo = tmid-twin/2;
// beef this up
            if(RangeLo<0)RangeLo=0;
            if(RangeHi>=nSample/SampPerDay)RangeHi=nSample/SampPerDay;

            newscreen();
            MyDraw(1);
            RetVal=USERCONTINUE | USERPAINT;
        }
        break;

/*===========Function key F2 =====================================*/
        case CM_G_F2:
        {
            float tmid,twin;
            tmid = (RangeHi+RangeLo)/2;
            twin = RangeHi-RangeLo+1;             // window
            if(twin<1)twin=1;
            RangeHi = tmid+twin/2;
            RangeLo = tmid-twin/2;
// beef this up
            if(RangeLo<0)RangeLo=0;
            if(RangeHi>=nSample/SampPerDay)RangeHi=nSample/SampPerDay;

            newscreen();
            MyDraw(1);
            RetVal=USERCONTINUE | USERPAINT;
        }
        break;

/*===========Function key F3 =====================================*/
        case CM_G_F3:   //INC range (RANGE LEFT)
        {
            float twin;
            twin = RangeHi-RangeLo;             // window
            if(twin > 0)
            {
                if((RangeLo-1) < 0)
                {
                    RangeLo=0;
                }
                else
                {
                    RangeLo -= 1;
                }
                RangeHi = RangeLo + twin;
            }
            newscreen();
            MyDraw(1);
            RetVal=USERCONTINUE | USERPAINT;
        }
        break;

/*===========Function key F4 =====================================*/
        case CM_G_F4:   // DEC range (RANGE RIGHT)
        {
            float twin;

            twin = RangeHi-RangeLo;             // window
            if(twin > 0)
            {
                if((RangeHi+1) >= nSample/SampPerDay)
                {
                    RangeHi=nSample/SampPerDay;
                }
                else
                {
                    RangeHi += 1;
                }
                RangeLo=RangeHi-twin;
                if(RangeLo < 0)
                    RangeLo=0;
            }
            newscreen();
            MyDraw(1);
            RetVal=USERCONTINUE | USERPAINT;
        }
        break;

/*===========Function key F5 =====================================*/
        case CM_G_F5:   // set range to window
        {
            RangeLo = PlotStart;
            RangeHi = PlotEnd;
            if(    (RangeHi >= nSample/SampPerDay)
                || (RangeHi < 0))
            {
                RangeHi=nSample/SampPerDay-1;
        }
            RetVal=USERCONTINUE;
        }
        break;

/*===========Function key F6 =====================================*/
        case CM_G_F6:
        {
            RetVal=USERNULL;
        }
        break;

/*===========Function key F7 =====================================*/
        case CM_G_F7:
        {
            RetVal=USERNULL;
        }
        break;

/*===========Function key F8 =====================================*/
        case CM_G_F8:
        {
            RetVal=USERNULL;
        }
        break;

/*===========Function key F9 == print matlab data ================*/
        case CM_G_F9:
        {
            //**********
            fout = fopen("rw.txt","w"); // time,weight,urine
            //**********
            for(k=0; k<nRawData; k+=1)
            {
                fprintf(fout,"%f %lf %lf\n",(float)((RawTime[k]-TSamp1Start)/SecPerSample),RawWeight[k],RawUrine[k]);
            }
            fclose(fout);

            //**********
            fout = fopen("sw.txt","w"); // data arrays
            //**********
            if(!fout)
                break;
            for(k=DelayWeight; k<nSample; k+=1)
            {
                fprintf(fout,"%f, %f, %f, %f, %f\n",
                    Weight1Filt[k],
                    FirstDailyFilt[k],
                    MaxWeight[k],
                    LinFit[k],
                    WeightLVP[k]
                );
            }
            fclose(fout);

            //**********
            fout = fopen("aw2d.txt","w");   // 2D daily referenced
            //**********

            for(k=0; k<nDayStarts-1; k+=1)
            {
                m=UtimeToSamp(&RawTime[DayStarts[k]]);
                // try to qualify days wil few samples
//                j=UtimeToSamp(&RawTime[DayStarts[k] + 1 ]);
//                if( (j-m) > 6) continue;

                n=UtimeToSamp(&RawTime[DayStarts[k+1]]);
                if( (n-m) > (int)SampPerDay-1)
                    n = m + (int)SampPerDay-1;
                j=0;
                while( (n > (m)) )
                {
                    fprintf(fout,"%5.1f, ",Weight1Filt[m]);
                    m += 1;
                    j += 1;
                }
                while( (j < (int)SampPerDay-1) )
                {
                    fprintf(fout,"%5.1f, ",Weight1Filt[m]);
                    j += 1;
                }
                fprintf(fout,"%5.1f\n",Weight1Filt[m]);

            }
            fclose(fout);

            RetVal=USERNULL;
        }
        break;

/*===========Function key F9 =====================================*/
        case CM_G_F10:
        {
            RetVal=USERNULL;
        }
        break;

#if ISWIN32 // Only implemented in Win32
/*===========User Menu selection 1 write data files ====================*/
        case CM_G_USER+1:   // Major graphs
        {
            fout = fopen("RW.TXT","w");
            for(k=0; k<nRawData; k+=1)
            {
                fprintf(fout,"%f %lf %lf\n",(float)((RawTime[k]-TSamp1Start)/SecPerSample),RawWeight[k],RawUrine[k]);
            }
            fclose(fout);

            fout = fopen("SW.TXT","w");
            if(!fout)
                break;
            for(k=DelayWeight; k<nSample; k+=1)
            {
                fprintf(fout,"%f, %f, %f, %f, %f\n",
                    Weight1Filt[k],
                    FirstDailyFilt[k],
                    MaxWeight[k],
                    LinFit[k],
                    WeightLVP[k]
                );
            }
            fclose(fout);

            RetVal=USERNULL;
         }
        break;

/*===========User Menu selection 2 =====================================*/
        case CM_G_USER+2:       // debuf
        {

            UTIME uTm1;
            struct tm sTm1;

            fout = fopen("DBG.TXT","w");
            for(k=0; k<nRawData; k+=1)
            {
                uTm1=RawTime[k];
                UtimeToTM(&uTm1, &sTm1);
                fprintf(fout,"%d %ld %lf %s",
                (int)k,
                (UTIME)uTm1,
                RawWeight[k],
                asctime(&sTm1));
            }
            fclose(fout);

        }
        break;

/*===========User Menu selection 3 =====================================*/
        case CM_G_USER+3:
            RetVal=USERNULL;
        break;

/*===========User Menu selection 4 =====================================*/
        case CM_G_USER+4:
            RetVal=USERNULL;
        break;

/*===========User Menu selection 5 =====================================*/
        case CM_G_USER+5:
            RetVal=USERNULL;
        break;

/*===========User Menu selection 6 =====================================*/
        case CM_G_USER+6:
            RetVal=USERNULL;
        break;

/*===========User Menu selection 7 =====================================*/
        case CM_G_USER+7:
            RetVal=USERNULL;
        break;

/*===========User Menu selection 8 =====================================*/
        case CM_G_USER+8:
            RetVal=USERNULL;
        break;

/*===========User Menu selection ... more if needed=====================*/
#endif // of ISWIN32

    }
    return(RetVal);
}


/*
**************************************************************************
*
*  Function: static int MyWInit(int argc, char ** argv)
*
*  Purpose: One time call when we start
*
*  Comments:
*
*   This is called when the program starts. You should initialize
*   One time variables. Typically the UserCommand menu is loaded up
*   here as well.
*
**************************************************************************
*/

static int MyWInit(int argc, char ** argv)
{
    int RetVal=1;
    int k;
    struct tm Jtm;

    SetArgv(args);                          // point to the argument table
#if ISWIN32
    AddUserCommand("Write Data");           // (t-t0,weight(t))
    AddUserCommand("Debug");                     //  held(n)
#if 0                                       // Add text for user commands
    AddUserCommand("User 4");
    AddUserCommand("User 5");
    AddUserCommand("User 6");
    AddUserCommand("User 7");
    AddUserCommand("User 8");
#endif

    SetHelpFile("WeightHelp.txt");
#endif // of ISWIN32

        // set the LVP break points
    for(k = 0; k < NLVP; k+=1)
    {
        memset(&Jtm,0,sizeof(struct tm));
        StringToSDate(lvpdates[k],&Jtm);
        Jtm.tm_year -= 1900;
        Jtm.tm_mon -= 1;
        Jtm.tm_isdst = _daylight;
        lvpbreaks[k] = TMToUtime(&RawTime[nRawData],&Jtm);
#if EASYWINDEBUG & 0
{
char* p;
p = asctime(&Jtm);
printf("lvpbreaks %s\n",p);
}
#endif

    }
        // set the bounds break points
    for(k = 0; k < NBOUNDS; k+=1)
    {
        memset(&Jtm,0,sizeof(struct tm));
        StringToSDate(lvpBdates[k],&Jtm);
        Jtm.tm_year -= 1900;
        Jtm.tm_mon -= 1;
        Jtm.tm_isdst = _daylight;
        lvpBbreaks[k] = TMToUtime(&RawTime[nRawData],&Jtm);
#if EASYWINDEBUG & 0
{
char* p;
p = asctime(&Jtm);
printf("lvpBbreaks %s\n",p);
}
#endif

    }


    nRawData = 0;
    nSample = 0;

    RetVal=1;
    return(RetVal);
}

/*
**************************************************************************
*
*  Function: static int MyInit(int argc, char ** argv)
*
*  Purpose: Call to init or reinit
*
*  Comments:
*
*  This can be called from the menus to initialize for a run
*  of the program. The program itself has 3 modes of operation
*
*  1) Init
*  2) Draw
*  3) Continue
*
*   This is what reads the file and computes the file dependant stuff
*   The space-bar can be used to change some displays without reading
*   in the file each time.
*
**************************************************************************
*/

static int MyInit(int argc, char ** argv)
{
    int RetVal=USERCONTINUE;
    int k,m;
    struct tm tmptm;

    ReadWeightData();

    // Basic contants
    SampPerSec = 1.0/SecPerSample;              // time costamts
    TicPerSamp = TICSPERSEC*SecPerSample;       // tics in a sample
    SampPerTic = 1.0/TicPerSamp;                // samples in a tic

    SampPerDay = ONEDAY*SampPerSec;             // samples per day
    SampPerHour = ONEHOUR*SampPerSec;           // samples in 1 hr

    UtimeToTM(&RawTime[0],&tmptm);              // first sample time
    tmptm.tm_hour = 0;                          // floor to day boundry
    tmptm.tm_min = 0;
    TMToUtime(&TSamp1Start, &tmptm);            //
    TSamp1End = (UTIME)RawTime[nRawData-1];
    nSample = (TSamp1End - TSamp1Start)/TicPerSamp + 1; // number of samples

#if EASYWINDEBUG & 0
printf("rate %f Samp/Day %f start %lf end %lf total %d\n",
(float)SecPerSample,(float)SampPerDay,(double)TSamp1Start,(double)TSamp1End,nSample);
#endif


#if 1
    if(Ftmp)free(Ftmp);
    if(LinFit)free(LinFit);
    if(Index1)free(Index1);

    if(SampHeld)free(SampHeld);
    if(Weight1Filt)free(Weight1Filt);
    if(MaxWeight)free(MaxWeight);

    if(FirstDaily)free(FirstDaily);
    if(FirstDailyFilt)free(FirstDailyFilt);
    if(UrineDaily)free(UrineDaily);
    if(UrineDailyAvg)free(UrineDailyAvg);

    if(WeightLVP)free(WeightLVP);
    if(WeightBounds)free(WeightBounds);

    if(tFirsts)free(tFirsts);
    if(xFirsts)free(xFirsts);                            // Values at starts

    Ftmp = (float*)calloc(nSample+1, sizeof(float));
    LinFit = (float*)calloc(nSample+1, sizeof(float));
    Index1 = (float*)calloc(nSample+1, sizeof(float));

    SampHeld = (float*)calloc(nSample+1, sizeof(float));
    Weight1Filt = (float*)calloc(nSample+1, sizeof(float));
    MaxWeight = (float*)calloc(nSample+1, sizeof(float));

    FirstDaily = (float*)calloc(nSample+1, sizeof(float));
    FirstDailyFilt = (float*)calloc(nSample+1, sizeof(float));
    UrineDaily = (float*)calloc(nSample+1, sizeof(float));
    UrineDailyAvg = (float*)calloc(nSample+1, sizeof(float));

    WeightLVP = (float*)calloc(nSample+1, sizeof(float));
    WeightBounds = (float*)calloc(nSample+1, sizeof(float));
    tFirsts = (UTIME*)calloc(nRawData+1, sizeof(UTIME));
    xFirsts = (double*)calloc(nRawData+1, sizeof(double));
#endif

    for(k=0; k < nSample; k++)
        Index1[k] = k;

    // LVP pre-adjustment asked for? Use Raw Data
    if(ConstLPV>0)
        AdjLVP( RawWeight, RawTime, nRawData,lvplbs, lvpbreaks, NLVP);
    // LVP stepped array special, integrate inside the call
    LVP2TSeries(lvpbreaks,lvplbs,NLVP, WeightLVP,nSample, TSamp1Start);
    LinInterp(LIN_Y1, WeightBounds, TicPerSamp, nSample, lvpBbreaks,lvpBlbs,NBOUNDS,&TSamp1Start);

    // day starts
    MakeDayStarts(RawTime, nRawData, DayStarts, &nDayStarts);
    nFirsts=nDayStarts;                                     // used as an alias
    // copy FOD data into arrays for later use
    for(k=0;k < nDayStarts; k+= 1)
    {
        tFirsts[k] = RawTime[DayStarts[k]];
        xFirsts[k] = RawWeight[DayStarts[k]];
    }

    //interpolation for FOD
    if(InterpFOD)
        LinInterp(InterpFOD,FirstDaily, TicPerSamp, nSample, tFirsts, xFirsts, nFirsts,&TSamp1Start);
    else
        LinInterp(LIN_Y1,FirstDaily, TicPerSamp, nSample, tFirsts, xFirsts, nFirsts,&TSamp1Start);
    // linear interoplation for weight
    if(InterpWeight)
        LinInterp(InterpWeight, SampHeld, TicPerSamp, nSample, RawTime, RawWeight, nRawData,&TSamp1Start);
    else
        LinInterp(LIN_NORM, SampHeld, TicPerSamp, nSample, RawTime, RawWeight, nRawData,&TSamp1Start);

    // Urine imulse stream
    LinInterp(LIN_PULSE, UrineDaily, TicPerSamp, nSample, RawTime, RawUrine, nRawData,&TSamp1Start);


#if EASYWINDEBUG & 0  // debug BinSearchL
{
int n1;
int nx;
nx =  RawTime[nRawData-4]+10000;
n1=BinSearchL(nx,RawTime,nRawData);
if(n1 < 0)
{
printf("case n1<0: nx %lx)\n",nx);
}
else if(n1 == 0)
{
printf("case n1==0: nx: %ld index %ld Times (%ld %ld)\n",
nx,n1,RawTime[n1],RawTime[n1+1]);
}
else if( n1 > 0)
{
printf("case n1>0: nx: %ld index %ld Times (%ld %ld %ld)\n",
nx,n1,RawTime[n1-1],RawTime[n1],RawTime[n1+1]);
}
else
{
printf("error n1 %ld \n",
n1);
}
}
#endif

    return(RetVal);
}

/*
**************************************************************************
*
*  Function: int MyDraw(int flagContinue)
*
*  Purpose: Called to draw
*
*  Comments:
*
*  This is the main program. It the flag flagContinue is not set, this is the
*  first time through. If set, it is to continue. The program must
*  return USERCONTINUE if it can continue, USERDONE is complete
*  and may return a flag USERPAINT if it wants to refresh the
*  screen. So USERPAINT+USERCONTINUE will cause the screen to
*  be painted then the program can cpntinue
*
*   This computes things that can be changed without having to read the
*   file again.
*
*   Urine Average
*   Linear curve fit
*
*Later: add optional filtering to the two basic weight curves
*
**************************************************************************
*/

int MyDraw(int ContinueFlag)
{

    int retval=USERCONTINUE & USERPAINT;
    int     k,n,m;
    float   t;
    char    tmpstr[32];
    UTIME  Jtime;
    struct  tm *tmptime;
    const double *A, *B;

    // compute the array ranges
    if(RangeHi<RangeLo)
    {
        t=RangeHi;
        RangeHi=RangeLo;
        RangeLo=t;
    }



    // Average Weightd
    // done each time so we can use <Space> to repaint the screen
    // without doing all the calculations
    if(nSample>0)
    {
        if((ConstWeight < 0))
        {
            ConstWeight = 0;
        }
        if((ConstWeight > 9))
        {
            ConstWeight = 9;
        }
        A= &IIRTaps[ConstWeight*9*2];
        B= &IIRTaps[ConstWeight*9*2+9];
        DelayWeight = SmoothDelays[ConstWeight];
        memset(Ftmp,0,sizeof(double)*9);
        Iir(Weight1Filt, SampHeld, nSample, A, B, (double*)Ftmp, 8);
        memset(Ftmp,0,sizeof(double)*9);
        Iir(FirstDailyFilt, FirstDaily, nSample, A, B, (double*)Ftmp, 8);
    }

    // Max Weight over a day

    {
        float wmax;
        int k,n,m;

        m=0;
        for(n=0;n < nDayStarts-1;n += 1)
        {
            wmax=0;
            for(k = DayStarts[n];k < DayStarts[n+1];k += 1)
            {
                if(wmax < RawWeight[k])
                {
                    wmax = RawWeight[k];
            }
        }
            xFirsts[n] = wmax;
        }
        xFirsts[n] = wmax;
        LinInterp(LIN_Y1, MaxWeight, TicPerSamp, nSample, tFirsts, xFirsts, nFirsts,&TSamp1Start);
        memset(Ftmp,0,sizeof(double)*9);
        Iir(MaxWeight, MaxWeight, nSample, A, B, (double*)Ftmp, 8);
    }


    // Average Urine

    if(nSample>0)
    {
        if((ConstUrine < 0))
        {
            ConstUrine = 0;
        }
        if((ConstUrine > 9))
        {
            ConstUrine = 9;
        }
        A= &IIRTaps[ConstUrine*9*2];
        B= &IIRTaps[ConstUrine*9*2+9];
        DelayUrine = SmoothDelays[ConstUrine];
        memset(Ftmp,0,sizeof(double)*9);
        Iir(UrineDailyAvg, UrineDaily, nSample, A, B, (double*)Ftmp, 8);
        ScaleX(UrineDailyAvg, (float) SampPerDay, UrineDailyAvg, nSample);
    }

    memset(LinFit,0,sizeof(LinFit));            // zero
    SzWindow=RangeHi-RangeLo;
    switch(ConstPlot)
    {
        //==========
        case 0: // None
        //==========
        break;

        //==========
        case 1: //Polynomial
        //==========
//              PolySqRaw(RawTime,RawWeight,RangeLo,RangeHi, polycoef, FitOrd);
                PolySqWin(Weight1Filt, RangeLo*SampPerDay,RangeHi*SampPerDay, polycoef, FitOrd);
            for(k=0; k<nSample; k+=1)
            {
                LinFit[k] = PolyVal(k,polycoef,FitOrd+1);
            }
        break;

        //==========
        case 2: //Weight Gain linear fit
        //==========
#if 1
        {
            int szwin;
            szwin = SzWindow*SampPerDay;
            for(k=0;k<nSample-szwin;k++)
            {
                PolySqWin(Weight1Filt, k, k+szwin-1, polycoef, 1);
                LinFit[k+szwin/2]=SampPerDay*polycoef[0];
            }
        }
#endif
        break;

        //==========
        case 3: //Polynomial first of day
        //==========
            PolySqWin(FirstDailyFilt, RangeLo*SampPerDay,RangeHi*SampPerDay, polycoef, FitOrd);
            for(k=0; k<nSample; k+=1)
            {
                LinFit[k] = PolyVal(k,polycoef,FitOrd+1);
            }
        break;

        //==========
        case 4: //Weight Gain First in day see case 2
        //==========
#if 1
        {
            int szwin;
            szwin = SzWindow*SampPerDay;
            for(k=0;k<nSample-szwin;k++)
            {
                PolySqWin(FirstDailyFilt, k, k+szwin-1, polycoef, 1);
                LinFit[k+szwin/2]=SampPerDay*polycoef[0];
            }
        }
#endif
        break;

        //==========
        case 5: //MaxWeight - FirstInDay Weight
        //==========
#if 1
        {
            SubXY(LinFit,MaxWeight,FirstDailyFilt,nSample);
        }
#endif
        break;

        //==========
        case 6: //MaxWeight
        //==========
#if 1
        {
            for(k=0;k<nSample;k+=1)
            {
                LinFit[k] = MaxWeight[k];
            }
        }
#endif
        break;

        //==========
        case 7: //First Daily Weight Gain
        //==========
#if 1
        {
            const double *A, *B;
            for(k=0;k<nDayStarts;k+=1)
            {
                tFirsts[k] = (RawTime[DayStarts[k]+1]/2) + (RawTime[DayStarts[k]]/2);
                xFirsts[k] = (RawWeight[DayStarts[k]+1] - RawWeight[DayStarts[k]]);
#if 0 // the slope: biased because of time between weiging
                xFirsts[k] = (RawWeight[DayStarts[k]+1] - RawWeight[DayStarts[k]])
                            /(RawTime[DayStarts[k]+1] - RawTime[DayStarts[k]])
                            *TicPerSamp * SampPerDay;
#endif

#if EASYWINDEBUG & 0
printf("%lf %lf %lf\n",RawWeight[DayStarts[k]+1],RawWeight[DayStarts[k]],xFirsts[k]);
#endif
            }
            A= &IIRTaps[ConstWeight*9*2];
            B= &IIRTaps[ConstWeight*9*2+9];
            LinInterp(InterpFOD,LinFit, TicPerSamp, nSample, tFirsts, xFirsts, nFirsts,&TSamp1Start);
            memset(Ftmp,0,sizeof(double)*9);
            Iir(LinFit, LinFit, nSample, A, B, (double*)Ftmp, 8);
        }
#endif
        break;

        //==========
        case 8: //Second of Day
        //==========
#if 1
        {
            const double *A, *B;
            for(k=0;k<nDayStarts;k+=1)
            {
                xFirsts[k] = (RawWeight[DayStarts[k]+1]);
                tFirsts[k] = (RawTime[DayStarts[k]+1]);

#if EASYWINDEBUG & 0
printf("%lf %lf %lf\n",RawWeight[DayStarts[k]+1],RawWeight[DayStarts[k]],xFirsts[k]);
#endif
            }
            A= &IIRTaps[ConstWeight*9*2];
            B= &IIRTaps[ConstWeight*9*2+9];
            LinInterp(InterpFOD,LinFit, TicPerSamp, nSample, tFirsts, xFirsts, nFirsts,&TSamp1Start);
            memset(Ftmp,0,sizeof(double)*9);
            Iir(LinFit, LinFit, nSample, A, B, (double*)Ftmp, 8);
        }
#endif
        break;

        //==========
        default:
        //==========
        break;
    }



    // Measurements
//    if (1)
    {
        int ix1,ix2;
        double  sumy,sumd;
        float dleft;
        UTIME  currtime;
        struct tm* currdate;
        char* pChar;

        // (re-)Compute poly coeff
        switch(ConstPlot)
        {
            case 0:
            case 1:
            case 2:
            case 5:
            case 6:
            default:
//              PolySqRaw(RawTime,RawWeight,RangeLo,RangeHi, polycoef, FitOrd);
                PolySqWin(Weight1Filt, RangeLo*SampPerDay,RangeHi*SampPerDay, polycoef, FitOrd);
            break;

            case 3:
            case 4:
                PolySqWin(FirstDailyFilt, RangeLo*SampPerDay,RangeHi*SampPerDay, polycoef, FitOrd);
            break;
        }

        // find the threshold intersection
        dleft = NRSolvePoly(ThrPain,polycoef,FitOrd,nSample);
        ix1 = (int)(dleft+.5);
        tNextLVP = SampToUtime(&ix1);

        // Display and measurement adjustments
#if 1
        if(ConstLPV >= 2)
        {
            SubXY(FirstDailyFilt,FirstDailyFilt,WeightLVP,nSample);
            SubXY(Weight1Filt,Weight1Filt,WeightLVP,nSample);
            SubXY(MaxWeight,MaxWeight,WeightLVP,nSample);

            if((ConstPlot == 1)||(ConstPlot == 3))  // polynomial fits
                SubXY(LinFit,LinFit,WeightLVP,nSample);

            if((ConstPlot == 6))                                // MaxWeight was copied before adjust
                SubXY(LinFit,LinFit,WeightLVP,nSample);
            if((ConstPlot == 8))                                // Second of Day
                SubXY(LinFit,LinFit,WeightLVP,nSample);
        }
        // (re-)Compute poly coeff
#else
        switch(ConstPlot)
        {
            default:
            case 0:                 // None
            break;

            case 2:                 // Weight gain
            case 4:                 // FirstInDay Weight gain
            case 5:                 // Diff Weight-FirstInDay Weight
                SubXY(FirstDailyFilt,FirstDailyFilt,WeightLVP,nSample);
                SubXY(Weight1Filt,Weight1Filt,WeightLVP,nSample);
            break;

            case 1:                 // Poly
            case 3:                 // FirstInDay Poly
                SubXY(FirstDailyFilt,FirstDailyFilt,WeightLVP,nSample);
                SubXY(Weight1Filt,Weight1Filt,WeightLVP,nSample);
                SubXY(LinFit,LinFit,WeightLVP,nSample);
            break;
        }
#endif
        // stats for the polynomial fit range
        sumy=sumd=0;
        ix1 = RangeLo * SampPerDay;
        ix2 = RangeHi * SampPerDay;
        for(k=ix1;k<=ix2;k+=1)
        {
            sumy += Weight1Filt[k];
            sumd += FirstDailyFilt[k];
        }
        sumy /= (ix2-ix1+1);
        AvgWeight = sumy;
        sumd /= (ix2-ix1+1);
        AvgWeightM = sumd;
    }

return(retval);
}

/*
**************************************************************************
*
*  Function: int MyPaint()
*
*  Purpose: Called to paint the screen
*
*  Comments:
*
*  Normally you can draw directly into a window and it is displayed.
*  However, if the user moves something over your drawing (say
*  another window), the pixels are lost. Windows keeps track
*  of which window needs to refreshed and will send this message.
*  If you want a "nice" effect, you can keep what do drew into arrays
*  and redraw it. Otherwise, just don't erase the drawing by
*  mousing windows over it!
*
*   IMPORTANT
*   Windows will WM_PAINT before some other things have been set up.
*   This means you might get a WM_PAINT while the program is trying
*   to initialize (multi-tasking). It is important to not assume
*   things may be set up or the program will (and does) hang badly.
*
*   It probably would be good to init in CM_G_INIT and set a flag. We
*   haven't done this yet. Maybe cproto might hold off all paints
*   with a flag of sorts.
*
**************************************************************************
*/

int MyPaint()
{
    int retval=USERCONTINUE;
    int     k,n,m;
    int     ix,iy;
    int     x,y;
    float   xmin,ymin,xmax,ymax;            // data limits
    int     ix0,iy0;                        // lower left
    int     ixdelta,iydelta;                // box size
    struct tm sTM;
    UTIME   utime;

    if(nSample <= 0) return(-1);
    if( !Weight1Filt ) return -1;

    if(PlotStart>0)
        PltCtl1.xmin = PlotStart*SampPerDay;
    else
        PltCtl1.xmin = 0;
    if(PlotEnd>PlotStart)
        PltCtl1.xmax = PlotEnd*SampPerDay;
    else
        PltCtl1.xmax = nSample;

    // need to Auto Set these
    if(ConstLPV==1)
    {
        PltCtl1.ymin = 120;
        PltCtl1.ymax = 160;
    }
    else
    {
        PltCtl1.ymin = 110;
        PltCtl1.ymax = 130;
    }
#if 1
        PltCtl1.ymin = Xmin(&Weight1Filt[DelayWeight],nSample-DelayWeight);
        PltCtl1.ymax = Xmax(&Weight1Filt[DelayWeight],nSample-DelayWeight);
#endif

    PltCtl1.ix0 = 40;                                       // room for labels
    PltCtl1.iy0 = 24;
    PltCtl1.ixdelta = HDrawMax-PltCtl1.ix0;                 // go to far right
    PltCtl1.iydelta = VDrawMax-96-PltCtl1.iy0;              // allow 96 pixels for plot 2
    PlotXYCommon(&PltCtl1);

    PltCtl2 = PltCtl1;                                      // start with a copy
    PltCtl2.ymin = 0;                                       // 0 tp
    PltCtl2.ymax = 1500;                                    // 1500 ml
    PltCtl2.iy0 = PltCtl1.iydelta+PltCtl1.iy0+16;
    PltCtl2.iydelta=96-16;
    PlotXYCommon(&PltCtl2);

    PltCtl3 = PltCtl1;                                      // start with a copy
    PltCtl3.ymin = Xmin(LinFit,nSample);                    // auto scaled
    PltCtl3.ymax = Xmax(LinFit,nSample);

    PltCtl3.ymax = 2;                                       // well no, [-2,2]
    PltCtl3.ymin = -2;
    PlotXYCommon(&PltCtl3);

    //============================= hash grid
    GridXY(ONEDAY/SecPerSample*7,1,BLACKTEXT,&PltCtl1);
    drawtext(PltCtl1.ixmin+64, PltCtl1.iy0+PltCtl1.iydelta-24, "WEIGHT", HTEXT_ABOVE);

    //=============================Pain threshold should adjust for LVP in mode 1
    newpen(REDTEXT,PS_DOT+(2<<4));
    n = (int)((ThrPain - PltCtl1.ymin) * PltCtl1.yscl + 0.5 + PltCtl1.iy0);
    x = 0; y=0;
    GetPlotPoints(0,ThrPain,NULL,&iy,&PltCtl1);
    moveto(0,iy);
    lineto(HDrawMax,iy);
    newpen(BLACKTEXT,PS_SOLID);

    //============================= Mark LVP
    newpen(RUSTTEXT,PS_DOT+(2<<4));
    for(k=0;k<NLVP;k++)
    {
        x=GetSampleIdx(lvpbreaks[k]);
        y=ThrPain;
        GetPlotPoints(x-DelayWeight,y,&ix,&iy,&PltCtl1);
        moveto(ix,iy-8);
        lineto(ix,iy+8);
    }


    //============================= Mark Range of fit
    newpen(GREENTEXT,PS_SOLID+(2<<4));
    x=(float)RangeLo * SampPerDay;
    y=PltCtl1.ymax;
    GetPlotPoints(x-DelayWeight,y,&ix,&iy,&PltCtl1);
    moveto(ix,iy-2);
    x=(float)RangeHi * SampPerDay;
    y=PltCtl1.ymax;
    GetPlotPoints(x-DelayWeight,y,&ix,&iy,&PltCtl1);
    lineto(ix,iy-2);
    newpen(BLACKTEXT,PS_SOLID);

#if USEUNIXTIME
    utime = time(NULL);
#else
    GetSystemTimeAsFileTime((FILETIME*)&utime);                         //This is today in internel format
#endif

    ix = UtimeToSamp((UTIME*) &utime);             //Today in samples
    iy = UtimeToSamp((UTIME*) &tNextLVP);                   // next LVP Date in samples
    y = (iy-ix)/SampPerDay;                                 // days remaininb
    UtimeToTM(&tNextLVP, &sTM);
#if EASYWINDEBUG & 0
printf("ix %d iy %d\n",ix,iy);
#endif
    {
        char tmpstr[132];                                   // annotate
        char tmpstr1[64];

        strftime(tmpstr1, sizeof(tmpstr1), "%a-%b-%d", &sTM);
        sprintf(tmpstr,"LVP at %s W= %5.1f  WG/yr= %5.2f DaysLeft= %5.1f",
                tmpstr1,(float)AvgWeight,(float)AvgWGain*365.25,(float)y);
        drawtext(PltCtl1.ixmin+16, PltCtl1.iymax,tmpstr, HTEXT_ABOVE);
    }

    //====================== Filtered waveform
#if 1
    {
        PlotXY(
            Index1,
            &Weight1Filt[DelayWeight],
            nSample-DelayWeight,
            RGB(64,128,128),
            (0<<8+0),
            &PltCtl1
            );
    }
#endif


    //====================== First sample Fiktered in the day waveform
#if 1
    PlotXY(
        Index1,
        &FirstDailyFilt[DelayWeight],
        nSample-DelayWeight,
        BLUETEXT,
        (1<<8+0),
        &PltCtl1
        );
#endif


    //======================  Draw the extras fit
    {
    switch(ConstPlot)
    {
        //==========
        case 0: // None
        case 1: // polynomial
        case 3: // polynomial first in day
        case 6: // Max weight
        case 8: // Second of Day
        //==========
            PltCtl3 = PltCtl1;
        break;

        //==========
        case 2: //Weight Gain
        case 4: //Weight Gain First in day
        //==========
        {
            char tmptxt[16];
            newpen(RUSTTEXT,PS_SOLID);
            PltCtl3 = PltCtl1;
            PltCtl3.ymin = Xmin(LinFit,nSample);
            PltCtl3.ymax = Xmax(LinFit,nSample);

            PltCtl3.ymax = 2;                   // [-2..2]lbs
            PltCtl3.ymin = -2;

            PlotXYCommon(&PltCtl3);
            GetPlotPoints(0,0,&ix,&iy,&PltCtl3);
            k=0;
            moveto(ix,iy-1);
            lineto(PltCtl3.ixmax,iy-1);
            moveto(ix,iy);
            lineto(PltCtl3.ixmax,iy);
            sprintf(tmptxt,"%2d lb",k);
            drawtext(PltCtl3.ixmax-8,iy,tmptxt,JTEXT_RIGHT+JTEXT_ABOVE+JTEXT_ERASE);

            GetPlotPoints(0,1.0,&ix,&iy,&PltCtl3);
            k=1;
            moveto(ix,iy);
            lineto(PltCtl3.ixmax,iy);
            sprintf(tmptxt,"%2d lb",k);
            drawtext(PltCtl3.ixmax-8,iy,tmptxt,JTEXT_RIGHT+JTEXT_ABOVE+JTEXT_ERASE);

            GetPlotPoints(0,-1.0,&ix,&iy,&PltCtl3);
            k= -1;
            moveto(ix,iy);
            lineto(PltCtl3.ixmax,iy);
            sprintf(tmptxt,"%2d lb",k);
            drawtext(PltCtl3.ixmax-8,iy,tmptxt,JTEXT_RIGHT+JTEXT_ABOVE+JTEXT_ERASE);

            newpen(BLACKTEXT,PS_SOLID);
        }
        break;

        case 5: // Adjusted Weight - FirstInDay weight (daily swing)
        case 7: // Second minus FOD weight
        //==========
        {
            char tmptxt[16];
            float xy;
            newpen(RUSTTEXT,PS_SOLID);
            PltCtl3 = PltCtl1;
            PltCtl3.ymax = 6;                   // [-1..6]lbs
            PltCtl3.ymin = -1;
            PlotXYCommon(&PltCtl3);
            for(xy=-1.0; xy <= PltCtl3.ymax; xy += 1.0)
            {
                GetPlotPoints(0,xy,&ix,&iy,&PltCtl3);
                moveto(ix,iy-1);
                lineto(PltCtl3.ixmax,iy-1);
                moveto(ix,iy);
                lineto(PltCtl3.ixmax,iy);
                sprintf(tmptxt,"%3.0f lb",xy);
                drawtext(PltCtl3.ixmax-8,iy,tmptxt,JTEXT_RIGHT+JTEXT_ABOVE+JTEXT_ERASE);
            }

            newpen(BLACKTEXT,PS_SOLID);
        }
        break;

        //==========
        case 9: // LVP (test)
        //==========
        {
            char tmptxt[16];
            float xy;
            newpen(RUSTTEXT,PS_SOLID);
            PltCtl3 = PltCtl1;
//            PltCtl3.ymax = 100;                   // [0..120]lbs
//            PltCtl3.ymin = 0;
            PlotXYCommon(&PltCtl3);
#if 0
            for(xy=0.0; xy <= PltCtl3.ymax; xy += 20.0)
            {
                GetPlotPoints(0,xy,&ix,&iy,&PltCtl3);
                moveto(ix,iy-1);
                lineto(PltCtl3.ixmax,iy-1);
                moveto(ix,iy);
                lineto(PltCtl3.ixmax,iy);
                sprintf(tmptxt,"%3.0f lb",xy);
                drawtext(PltCtl3.ixmax-8,iy,tmptxt,JTEXT_RIGHT+JTEXT_ABOVE+JTEXT_ERASE);
            }
#endif
            memcpy(LinFit,WeightBounds,sizeof(float)*nSample);
//            memcpy(LinFit,WeightLVP,sizeof(float)*nSample);
            newpen(BLACKTEXT,PS_SOLID);
        }
        break;

        //==========
        default:
        //==========
        break;
    }
    PlotXY(
        Index1,
        &LinFit[DelayWeight],
        nSample-DelayWeight,
        RUSTTEXT,
        (0<<8+0),
        &PltCtl3
        );
    }

    // Urine
    GridXY(0,500.,RUSTTEXT,&PltCtl2);
    drawtext(PltCtl2.ixmin+64, PltCtl2.iy0+PltCtl2.iydelta-24, "URINE", HTEXT_ABOVE);
    PlotXY(
        Index1,
        &UrineDailyAvg[DelayUrine],
        nSample-DelayUrine,
        RUSTTEXT,
        (0<<8+0),
        &PltCtl2
        );

return(retval);
}


/*======================================================================*/
// Add your functions here.
/*======================================================================*/


/*
**************************************************************************
*
*  Function: int ReadWeightData()
*
*  Purpose: Called to read the input weight data
*
*  Comments:
*   This has been upgraded to ease expansion.
*
*   Fields are left to right.
*   Fields are required and cannot be skipped.
*   To allow for limited expansion, the program can default empty
*   fields on the right that are missing. So you can add a new field
*   at the right without changing the data base DISCOURAGED
*
*   Comments: if the first non-white character on a line is from the set
*       [;#!$%] the line is ignored. The character ';' is prefered
*
*   Left to Right
*
* Date:     mm/dd/yyyy
* Time:     hh:mm[AP]      A or P appemded, no space allowed
* RelTime   www.w           (obsolete - in our data files but not used)
* Lbs:      www.w           Weight (lbs)
* Urine:    www.w           Urine (mL) - processed but the data files are 0.0
*
* The dates are checked "on the fly" for time order. If the order is
* not increasing, the line text is printed out and ignored
* This is a common mistake and is often due to AM/PM and date mismatch.
* Remember 12:00 AM is a new day for the 12 hour calendar
**************************************************************************
*/
int ReadWeightData(void)
{
    FILE*   fin;
    float   x;
    int     k,iChar,iEnd;
    int     kerr,lnum;
    UTIME  OldTime;
    char    line[132];
    char*   pToks[8];
    int     nToks;
    char    tmpstr[32];
    UTIME  Jtime;
    struct  tm *tmptime;
    struct  tm Jtm;


    fin=fopen(RawDataFile,"r");                             // Open the file
    if(!fin)
    {
        printf("Cant find input file <%s>\n",RawDataFile);
        return(-1);
    }

    nRawData = 0;                                           // zero some stuff
    OldTime = 0;
    lnum = 0;

    while(1)                                                // loop to read it
    {
        kerr = 0;
        k = (int)fgetNL(line,132,fin);
        if(!k)
            break;
        lnum += 1;
        // get the tokens with an error check
        iChar = 0;
        kerr = -10;                                 // where an error is if any

        iEnd=strlen(line)-1;
        if(iEnd == 0)
            continue;                               // empty line

        iChar = strspn(line,MM_white);                      // strip white at BOL
#if EASYWINDEBUG & 0
{
printf("line %d (%d..%d)\n",lnum,iChar,iEnd);
}
#endif

        if(iChar >= iEnd)
            continue;                                       // only white space get nrxt

        if( (line[iChar] == ';')                    // Comment check
          ||(line[iChar] == '*')
          ||(line[iChar] == '#')
          ||(line[iChar] == '!')
          ||(line[iChar] == '%')
        ) continue;                                         // yep skip to next

        memset(&Jtm,0,sizeof(struct  tm ));
        k = StringToSDate(&line[iChar],&Jtm);       // do the date
        if(k < 0)
        {
            printf("Line %d: Error %d %d parsing date/time <%s>\n",lnum,k,iChar,line);
            continue;
        }
        iChar += k;
        // create the composite time

        kerr = 0;
        Jtm.tm_year -= 1900;                                // internal format adjustments
        Jtm.tm_mon -= 1;
        Jtm.tm_isdst = _daylight;
        TMToUtime(&RawTime[nRawData],&Jtm);                 // get the binary time
        if(RawTime[nRawData]<= OldTime)                     // check against the previous entry
        {
            line[strlen(line-1)]=0;
            printf("Line %d: Out of order <%s>\n",lnum,line);
            continue;
        }
        OldTime = RawTime[nRawData];
        // read the raw weight

        memset(pToks,0,sizeof(pToks));                      // Tokenize remaining arguments
        nToks = 0;
        pToks[nToks] = strtok(&line[iChar]," \t");
        while((nToks<7) && (pToks[nToks]))
        {
            nToks += 1;
            pToks[nToks] = strtok(NULL," \t");
        }

        if(pToks[0])                                        // not used
            sscanf(pToks[0],"%f",&x);
        else
            x = 0;

        if(pToks[1])                                        // Weight
            sscanf(pToks[1],"%lf",&RawWeight[nRawData]);
        else
            RawWeight[nRawData] = 0;

        if(pToks[2])                                        // Urine (mostly obsolete)
            sscanf(pToks[2],"%lf",&RawUrine[nRawData]);
        else
            RawUrine[nRawData] = 0;

        // Add more when needed
#if EASYWINDEBUG & 0
{
    line[strlen(line)-1]=0;                 //for printf
    printf("scanned <%s>\n%f %lf %lf\n",
        &line[iChar],
        x,
        RawWeight[nRawData],
        RawUrine[nRawData]);
}
#endif
        nRawData += 1;
        if(nRawData >= MAXRAWDATA)                          // array size check
        {
            nRawData = MAXRAWDATA;                          // too much data
            printf(" nRawData >= MAXRAWDATA data limit\n"); // scream
        }

    }

    fclose(fin);


// some post processing debug
#if EASYWINDEBUG & 0
{
float t;
for(k=0; k<nRawData; k+=1)
{
t=(float)(RawTime[k]-RawTime[0]) ;
printf("%d %f %f %f\n",RawTime[k],t,t/SampleRate,RawWeight[k]);
}
}
#endif

return 0;
}


U32 GetSampleIdx(UTIME t)
{
    U32 k;
    k=UtimeToSamp(&t);
    return(k);
}
/*
**************************************************************************
*
*  Function: int VSamp2TSeries(float* pfRaw, long* plRaw, int nRaw, float* pfSamp, int* pnSamp)
*
*  Purpose: Called for the first downsample stage
*
*  Comments:
*
*   Data is entered as (t,w) tuples. t has precision of 1 second but we
*   collect data at much lower rate. This routine builds an array of
*   equally spaced sampling points and fills the sparse array with
*   data where available. Most samples are 0
*
*
*   Arguments:
**************************************************************************
*/
int VSamp2TSeries(UTIME* pTRaw, F64* pSRaw, int nRaw, float* pSample, int nSample, unsigned long tStart)
{

    int k,n,m;

    // clear sample arrays and set some params
    memset(pSample,0,nSample*sizeof(float));             // clear output

    for(n=0; n<nRaw; n+=1)
    {
#if EASYWINDEBUG & 0
printf("VSamp2TSeries Hi \n");
#endif
        k=GetSampleIdx(pTRaw[n]);
        if(k < nSample)
        {
            pSample[k] = pSRaw[n];
        }
    }

return 0;
}

/*
**************************************************************************
*
*  Function: int LVP2TSeries(float* pfRaw, long* plRaw, int nRaw, float* pfSamp, int* pnSamp)
*
*  Purpose: Called to create an adjustment array
*
*  Comments:
*
*   Data is entered as (t,w) tuples. t has precision of 1 second but we
*   collect data at much lower rate. This routine builds an array of
*   equally spaced sampling points An integrator creates a stepped
*   adjustment waveform
*
*
*   Arguments:
*   pTRaw       time of each sample
*   pSRaw       sample value
*   nRaw        number in the array
*   pSample     output pointer
*   nSample     number to create
*   tSTart      starting offset
*   mode        0 SAH, 1 Integrate
**************************************************************************
*/
int LVP2TSeries(UTIME* pTRaw, F64* pSRaw, int nRaw, float* pSample, int nSample, unsigned long tStart)
{

    int     k,n,m;
    float   x;
    // clear sample arrays and set some params
    memset(pSample,0,nSample*sizeof(float));            // clear output
    if(ConstLPV == 0)
        return 0;

    x = 0;                                              // integration
    n=0;
    m=0;

#if EASYWINDEBUG & 0
{                       // Print LPV epochs
UTIME Ut1;
struct tm tmptm;
int ltm;

    Ut1 = TSamp1Start;
    UtimeToTM(&Ut1,&tmptm);                     // first sample time
    tmptm.tm_hour = 0;                          // floor to day boundry
    tmptm.tm_min = 0;
    TMToUtime(&Ut1, &tmptm);
    ltm=GetSampleIdx(Ut1);                      // before LVP
    printf("%d %d %f\n",ltm,(int)(ltm+SampPerDay),x);
}
#endif

    while(m < nRaw)
    {
        k=GetSampleIdx(pTRaw[m]);
        if(k >= nSample)                        // clamp to the range
        {
            k=nSample;
        }

#if EASYWINDEBUG & 0
{                       // Print LPV epochs
UTIME Ut1;
struct tm tmptm;
int ltm;

    Ut1 = pTRaw[m];
    UtimeToTM(&Ut1,&tmptm);                     // first sample time
    tmptm.tm_hour = 0;                          // floor to day boundry
    tmptm.tm_min = 0;
    TMToUtime(&Ut1, &tmptm);
    ltm=GetSampleIdx(Ut1);                      // before LVP
    printf("%d %d %f\n",ltm,(int)(ltm+SampPerDay),x);
}
#endif


        while(n<k)
        {
            pSample[n++] = x;                   // fill
        }

        x += pSRaw[m];                          // integrate
        m +=1;
    }
    while(n<nSample)
    {
        pSample[n++] = x;                       // fill
    }

return 0;
}

/*
**************************************************************************
*
*  Function: int AdjLVP(float* pfRaw, long* plRaw, int nRaw,
*  float* pLVPSamp, int* LVPtm, int nLVP)
*
*  Purpose: Adjust Raw samples for LVP
*
*  Comments:
*
*   Arguments:
**************************************************************************
*/
int AdjLVP( F64* pfRaw, UTIME* plRaw, int nRaw,
            F64* pLVPSamp, UTIME* LVPtm, int nLVP)
{

    int k,n,m;
    int noff;
    float t,x;


    x = 0;                                              // integration
    n=0;
    m=0;

    while(m < nLVP)
    {
        while(plRaw[n]<LVPtm[m])
        {
            pfRaw[n++] += x;                 // fill
            if(n >= nRaw)
                break;
        }
        x += pLVPSamp[m];                      // integrate
        m +=1;
    }
    while(n<nRaw)
    {
        pfRaw[n++] += x;                 // fill
    }

return 0;
}
/*
**************************************************************************
*
*  Function: int DSampleHold1(float* pfRaw, long* plRaw, int nRaw, float* pfSamp, int* pnSamp)
*
*  Purpose: Called to create a samp/hold wave
*
*  Comments:
*
*   Called on the primary sample array, this holds the first sample
*   over each day
*   Logic
*   FirstFlag
*   0 disable
*   1 armed
*   >1 (n-2 == last sa)
**************************************************************************
*/
#if 1
#define ARMED 1
#define READY 2
#define BLIND 3

int DSampleHold1(float* pSample, float* pHold, int nSample,
                 UTIME* tfirsts, F64* xfirsts, int* nfirsts, float firstvalue)
{

    int k,n,m;
    struct tm ttm;
    UTIME   utm;
    float HoldValue;
    int HeldTime;
    int FirstFlag;                          // flag to cause the next non zero sample to hold


    // clear sample arrays and set some params
    memset(pHold,0,nSample*sizeof(float));             // clear output
    HoldValue = firstvalue;
    FirstFlag = ARMED;
    m=0;

    for(n=0; n<nSample; n+=1)
    {
        utm = n;
        utm = SampToUtime((S32*)&utm);
        UtimeToTM(&utm,&ttm);

        switch(FirstFlag)
        {
            //============
            case ARMED:
            //============
            {
                if(pSample[n] == 0)
                    break;
                FirstFlag = BLIND;                  // clear the flag
                HoldValue=pSample[n];
                if(tfirsts)                         // record the transitions?
                {
                    xfirsts[m]=HoldValue;
                    tfirsts[m]=utm ;//- TSamp1Start;
                }
                m +=1;                              // count firsts
#if EASYWINDEBUG & 0
{
char* pChar;
pChar=asctime(&ttm);
printf("ARMED -> BLIND %d %f %s",n,HoldValue,pChar);
}
#endif
            }
            break;

            //============
            case READY:
            //============
            {
                if((ttm.tm_hour >=5 ))
                {
                    FirstFlag = ARMED;
#if EASYWINDEBUG & 0
{
char* pChar;
pChar=asctime(&ttm);
printf("READY -> ARMED %d %f %s",n,HoldValue,pChar);
}
#endif
                }
            }
            break;

            //============
            case BLIND:
            //============
            {
                if((ttm.tm_hour == 4 ))
                {
                    FirstFlag = READY;
#if EASYWINDEBUG & 0
{
char* pChar;
pChar=asctime(&ttm);
printf("BLIND -> READY %d %f %s",n,HoldValue,pChar);
}
#endif
                }
            }
            break;

            //============
            default:
            //============
            break;

        }

        pHold[n] = HoldValue;

#if EASYWINDEBUG & 0
if(HoldValue==0)
printf("%d %d %5.1f %5.1f\n",n,FirstFlag,pSample[n],pHold[n]);
#endif
    }
    if(nfirsts)                         // record the transitions?
    {
        *nfirsts=m;
    }

return 0;
}

/*
**************************************************************************
*
*  Function: int MakeDayStarts(UTIME* pTimes, int nTimes, int* DayStarts, int* nDayStarts)
*
*  Purpose: Called to build the starts array
*
*  Comments:
*
*   Called at the beginning to build an intex array. Each
*   element of the array is an indwx (pointer) to the element
*   of the RawData array which should be the first weight of
*   a day.
*
*   This was origionally inline code. A little buggy, it used
*   a state machine. Sadly this wiw not work that well because
*   when the number of samples was 2 (or less) each day, things
*   could be lost. It is now 1 state and the computation is done
*   every input so to not miss one. Code from the previuos
*   implementations is still present but the machine cannot
*   leave the ARMED state. We'll get rid of it later
*
**************************************************************************
*/
int MakeDayStarts(UTIME* pTimes, int nTimes, int* DayStarts, int* nDayStarts)
{

    int         k,n,m;
    struct tm   ttm;
    UTIME       utm;
    UTIME       HeldTime;
    int         FirstFlag;                          // flag to cause the next non zero sample to hold
    int         hmod;

    FirstFlag = ARMED;
    HeldTime = 0;
    m=0;

    for(n=0; n<nTimes; n+=1)
    {
        utm = pTimes[n];
        UtimeToTM(&utm,&ttm);
        hmod = (ttm.tm_hour - 5);
        if(hmod<0) hmod += 24;

        switch(FirstFlag)
        {
            //============
            case ARMED:
            //============
            {
                if((ttm.tm_hour >= 4 )
                && (ttm.tm_hour < 12+3 )
                && ((utm - HeldTime)/TICSPERSEC >= 8*ONEHOUR )
                )
                {
//                    FirstFlag = BLIND;                  // clear the flag
                    DayStarts[m] = n;
                    HeldTime = utm;
                    m +=1;                              // count firsts
#if EASYWINDEBUG & 0
{
char* pChar;
pChar=asctime(&ttm);
printf("AR -> BL %d %d %s",n,(int)UtimeToSamp(&utm),pChar);
}
#endif
#if EASYWINDEBUG & 0
{
printf("%d %d\n",ttm.tm_mday,ttm.tm_hour);
}
#endif
                }
            }
            break;

            //============
            case READY:
            //============
            {
                if((ttm.tm_hour >=5 ))
                {
                    FirstFlag = ARMED;
#if EASYWINDEBUG & 0
{
char* pChar;
pChar=asctime(&ttm);
printf("  RE -> AR %d %d %s",n,(int)UtimeToSamp(&utm),pChar);
}
#endif
                }
            }
            break;

            //============
            case BLIND:
            //============
            {
                if( ((utm - HeldTime)/TICSPERSEC >= 6*ONEHOUR )
                  && 1 )             // 4 AM
                {
                    FirstFlag = ARMED;
#if EASYWINDEBUG & 1
{
char* pChar;
pChar=asctime(&ttm);
printf("    BL -> RE %d %s",n,pChar);
}
#endif
                }
            }
            break;

            //============
            default:
            //============
            break;

        }

#if EASYWINDEBUG & 0
if(HoldValue==0)
printf("%d %d %5.1f %5.1f\n",n,FirstFlag,pSample[n],pHold[n]);
#endif
    }
    *nDayStarts = m;
    return 0;
}

#undef ARMED
#undef READY
#undef BLIND
#endif

/*
**************************************************************************
*
*  Function: int DSampleHold2(float* pfRaw, long* plRaw, int nRaw, float* pfSamp, int* pnSamp)
*
*  Purpose: Called to create a samp/hold wave
*
*  Comments:
*
*   Called on the primary sample array, this holds last valid
*   sample
**************************************************************************
*/
#if 1
int DSampleHold2(float* pSample, float* pHold, int nSample, float toffset)
{

    int k,n;

    float HoldValue;
    int FirstFlag;                          // flag to cause the next non zero sample to hold


    // clear sample arrays and set some params
    memset(pHold,0,nSample*sizeof(float));             // clear output
    HoldValue = pSample[0];
    FirstFlag = 0;

    for(n=0; n<nSample; n+=1)
    {
        if((pSample[n] > 0))
        {
            FirstFlag = 0;                  // clear the flag
            HoldValue = pSample[n];         // grab the hold value
        }
        pHold[n] = HoldValue;

    }

return 0;
}
#endif

/*
**************************************************************************
*   Function: float NRSolvePoly(float x0, float* P, int nP)
*
*  Purpose: Newton-Rhapson solver
*
*
*   Arguments:
*   x       value to search for
*   P       polynomial
*   nP      polynomial order
*   x0      initial estimate
*
*   Returns
*       closest estimate if convergence
*       x0 if no convergence
*
*  Comments:
*   NR is an iterative algorithm. For well behaived functions
*   is generally converges quickley. In our case, we have
*   a disconous function because of the LVP intervals
*
*   NR recurses
*       x(n+1) = x(n)+F(x(n))/Fp(x(n))
*   where Fp(x) is the derivitive of F(x)
*   We approximate Fp(x) with [F(x+e)-F(x-e)]/(2e)
*
*   F(x) is = P(x)+WeightLVP(x)
*
*   WeightLVP is the discontinous LVP function stored as an array
*/
#if 1
float NRSolvePoly(float X, float* P, int nP, float x0)
{

    double y0,y1,y2;
    double  e,x,dx;
    int     k,n;

    e = .1;
    dx=100;
    x=x0;
    n=24;

    do
    {
        y2=PolyVal((float)(x+e), P, nP+1);      // derivative
        y1=PolyVal((float)(x-e), P, nP+1);

        if(ConstLPV > 0)                        // adjust for LVP steps
        {
            k = (int)(x);                       // yep
            if(k >= nSample)
                k = nSample-1;
            else if (k < 0)
                k = 0;
            y0=PolyVal((float)(x), P, nP+1) - WeightLVP[k] - X;
        }
        else
        {
            y0=PolyVal((float)(x), P, nP+1) - X; // nope, usually debug
        }

        if(y2 != y1)
        {
            dx = 2*e*y0/(y2-y1);                // iteration
            x = x -dx;
        }
        else
        {
            dx = 100;                           // prevent DIV 0
        }
#if EASYWINDEBUG & 0
//printf("P0 %f P1 %f P2 %f WeightLVP[k] %f\n",P[0],P[1],P[2],WeightLVP[k]);
printf("NR: x=%f y0=%f y1=%f y2=%f dx=%f\n",x/48,y0,y1,y2,dx);
#endif
        if((fabs(y0)<.001))                    // converged ?
            return(x);

    }while((--n >0) && (fabs(y0)>.001));

    return(x0);                                 // failed
}
#endif
/*
**************************************************************************
*   Function: int BinSearchL(long x, long* tab, int Ntab)
*
*  Purpose: Binary Search
*
*
*   Arguments:
*   x       value to search for
*   tab     table to search in
*   Ntab    number of entries in the table
*
*   Returns
*   The index of the closest point which is greater than x.
*   If x is less than the first point it returns 0. If
*   If x is greater than the last point it returns Ntab
*   If the table is not monotomic it retuurns -k
*
*  Comments:
*   Linear interpolation of [x,y] into fixed spacing
*
*/
int BinSearchL(long x, long* tab, int Ntab)
{
    int Nl,Nm,Nh;

    // initialize and check end,exception conditions
    Nl=0;
    Nh=Ntab-1;
    if(Nh <= 0)                     // empty table
        return(0x80000000);

    while(Nh-1 > Nl)
    {
        if(tab[Nl] >= tab[Nh])      // strict monotomic
            return(-Nh);

        Nm=(Nh+Nl) / 2;
#if EASYWINDEBUG & 0
printf("%d %d %d\n",
Nl,Nm,Nh);
#endif

        if(x >= tab[Nm])
            Nl = Nm;
        else
            Nh = Nm;
    }

    if((x - (tab[Nh]+tab[Nl])/2) < 0)
        Nm = Nl;
    else
        Nm = Nh;
#if EASYWINDEBUG & 0
printf("%ld return %d (%d %d) (%d %d) .. %d\n",
x, Nm, Nl,Nh, tab[Nl],tab[Nh],(tab[Nl]+tab[Nh])/2-x);
#endif
    return(Nm);
}
/*
**************************************************************************
*  Function: int LinInterp(int op, float* dst, float dststep, int Ndst, UTIME* x, F64* y, int Nxy, UTIME* ts)
*
*  Purpose: Linear interpolation
*
**
*   Arguments:
*   op
*       LIN_N0RM        Normal Linear
*       LIN_Y1          Hold the first
*       LIN_Y2          Hold the last
*       LIN_AVG         Hold the average (y1+y2)/2
*       LIN_MIN         Hold the mimum
*       LIN_MAX         Hold the max
*       LIN_PULSE       Single impulse
*   dst     pointer to the destination
*   Ndst    number of samples in the destination array
*   dstep   step, same units as x (NOT)
*   x       time array
*   y       value array
*   Nxy     Number of samples in the 2-tuple [x,y]
*   ts      Pointer to the start time
*
*  Comments:
*   Linear interpolation of [x,y] into fixed spacing
*
*   Elements of the dst array are computed by interpolation between
*   adjacent (x1,y1)..(x2,y2) salples in the 2-tuple arrays.
*   The time array must be increasing. The destination is computed
*   by:
*       Start with the first
*
*
**************************************************************************
*/
int LinInterp(int op, float* dst, float dststep, int Ndst, UTIME* x, F64* y, int Nxy, UTIME* ts)
{
    int     idx,n;
    UTIME    t,tt;
    float   a,b;
    float   xinteg = 0;

    a = 0;                          //slope = 0
    b = y[0];                       // intercept = first Y
    idx=0;                          //index to segments
    tt=x[idx]-*ts;                      // next t threshold
    t=0;                            // running t variable
    n = 0;                          // destination counter

    while(n<Ndst)
    {
        t = n*dststep;
#if EASYWINDEBUG & 0
printf("%ld %ld\n",t,tt);
#endif
        if((t > tt))                // compute a new linear seqment
        {
            UTIME x1,x2;              // tmps for readibility
            float y1,y2;            // optimizer will remove these

            if(idx+1 < Nxy)
            {
                x1  =x[idx]-*ts;
                x2  =x[idx+1]-*ts;
                y1  =y[idx];
                y2  =y[idx+1];

                switch(op)
                {
                    //===================
                    default:
                    case LIN_NORM:  // normal linear
                    //===================
                    {
                        a   = (y2-y1)/(x2-x1); //!! do a DIV0 check
                        b   = -a*x1 + y[idx];
                    }
                    break;

                    //===================
                    case LIN_Y1:
                    //===================
                    {
                        a=0;
                        b=y1;
                    }
                    break;

                    //===================
                    case LIN_Y2:
                    //===================
                    {
                        a=0;
                        b=y2;
                    }
                    break;

                    //===================
                    case LIN_AVG:
                    //===================
                    {
                        a=0;
                        b= (y1+y2)/2;
                    }
                    break;

                    //===================
                    case LIN_MIN:
                    //===================
                    {
                        a=0;
                        if(y1 < y2)
                            b=y1;
                        else
                            b=y2;
                    }
                    break;

                    //===================
                    case LIN_MAX:
                    //===================
                    {
                        a=0;
                        if(y1 > y2)
                            b=y1;
                        else
                            b=y2;
                    }
                    break;

                    //===================
                    case LIN_PULSE:
                    //===================
                    {
                        a=0;
                        b=y1;
                    }
                    break;

                    //===================
                    case LIN_INTEGL:
                    //===================
                    {
                        a=0;
                        xinteg += y1;
                        b=xinteg;
                    }
                    break;

                    //===================
                    case LIN_INTEGR:
                    //===================
                    {
                        a=0;
                        xinteg += y2;
                        b=xinteg;
                    }
                    break;
                }
                idx += 1;
                tt  = x2;

            }
            else
            {
                a   = 0;
                if((op == LIN_INTEGL) || (op == LIN_INTEGR))
                    b = xinteg;
                else
                b   = y[idx];
                tt  = 0x7ffffffe;
            }
#if EASYWINDEBUG & 0
printf("%f %f\n",a,b);
#endif

        }
        *dst++ = t*a + b;
        if(op == LIN_PULSE)
            b = 0;
        n += 1;
#if EASYWINDEBUG & 0
//printf("%ld %ld\n",t,n);
if(n>=8192)
{
printf("Trap (n>=8192) %d\n\n",n);
return -1;
    }
#endif
    }
    return 0;
}

/*
**************************************************************************
*  Function: int MoveAverage(float* src, int len, int ntot, float* dst)
*
*  Purpose: Moving agerage
*
*  Comments:
*
*   Add the previous len samples.
*
**************************************************************************
*/
int MoveAverage(float* src, int len, int ntot, float* dst)
{
    int k,n;
    float sum;

    // a little optimization at the beginning
    for(n=0; n<len; n+=1)
    {
        sum = 0;
        for(k=0; k<n; k+=1)
        {
            sum += src[n-k];
        }
        dst[n] = sum;
    }

    for(n=len; n<ntot; n+=1)
    {
        sum = 0;
        for(k=0; k<len; k+=1)
        {
            sum += src[n-k];
        }
        dst[n] = sum;
    }
    return 0;
}

/*
**************************************************************************
*  Function: int MakeHanning(float *dst, int N)
*
*  Purpose: Make a Hanning of length n
*
*  Comments:
*
*   Normilization to set the DC value to 0 dB
*
*   w = .5/(1+n)*(1 - cos(2*pi*(1:n)'/(n+1)))
**************************************************************************
*/
int MakeHanning(float *dst, int N)
{
    int k,n;
    float sum;
    double pi;
    double dc;

    pi = 4.0*atan(1.0)/(1+N);
    dc = 1.0/(double)(1+N);
    for(n=1;n <= N; n+=1)
    {
        *dst = dc*(1-cos(2*pi*n));
#if EASYWINDEBUG & 0
printf("Hanning %f %f\n",*dst,(float)(2*pi*n));
#endif
        dst += 1;
    }

return N;
}
/*
**************************************************************************
*  Function: int Iir(float* dst, float* src, int n, const double* a, const double*b, double* dly, int ntaps);
*
*  Purpose: Vector Primitives
*
*  Comments:
*   We don't know what the values (-inf..0) are. But, most of our tilters
*   are lowpass. So, what we will do, is to initialize the delay line
*   as if a DC value had been sent. If the same value were to be sent
*   again (DC), there would be no change. Let:
*       X = constant value in the delay line (DC)
*   Then the output is
*       Y = D * sum(forward taps)
*   Given Y (first output) compute D and fill the delay line.
*
*   This reduces the start transiant by an order of magnitude.
*
**************************************************************************
*/

double macpm(float* x, float* y, int n)
{
    int k;
    double sum = 0;
    for(k=0; k<n; k+=1)
    {
        sum += (*x++) * (*y--);
    }
    return sum;
}

int SubXY(float* dst, float* x, float* y, int n)
{
    int k;
    double sum = 0;
#if !HAVELAPACK
    for(k=0; k<n; k+=1)
    {
        (*dst++) = (*x++) - (*y++);
    }
#else
{
    int CONST_P1 = 1;
    float CONST_M1 = -1.0;
    saxpy_(&n,&CONST_M1,y,&CONST_P1,x,&CONST_P1);
}
#endif

    return n;
}

int ScaleX(float* dst, float scalef, float* src, int n)
{
    int k;
    double sum = 0;
    for(k=0; k<n; k+=1)
    {
        (*dst++) = scalef * (*src++);
    }
    return n;
}

int BiasX(float* dst, float* x, float bias, int n)
{
    int k;
    double sum = 0;
    for(k=0; k<n; k+=1)
    {
        (*dst++) = bias + (*x++);
    }
    return n;
}

int ConditionBiasX(float* dst, float* x, float* y, int n)
{
    int k;
    double sum = 0;
    for(k=0; k<n; k+=1)
    {
        if(*x == 0)
            *dst = *x ;
        else
            *dst = *x + *y;
        ++dst;
        ++x;
        ++y;
    }
    return n;
}


double Xmin(float* x, int n)
{
    int k;
    double sum = x[0];
    for(k=1; k<n; k+=1)
    {
        if(sum >= x[k])
            sum = x[k++];
    }
    return sum;
}

double Xmax(float* x, int n)
{
    int k;
    double sum = x[0];
    for(k=1; k<n; k+=1)
    {
        if(sum <= x[k])
            sum = x[k++];
    }
    return sum;
}

/*
**************************************************************************
*  Function: int Iir(float* dst, float* src, int n, const double* a, const double*b, double* dly, int ntaps);
*
*  Purpose: Recursive filter
*
*  Comments:
*   Restriction reverse and forward coefficients are the same
*   length because we use a type II implementation where the
*   delay line elements are shared (watch for numerical
*   precision). The coefficients ar MATLAB. The delay
*   elements a[0] is assumed to be 1.0
*
*
*   Might have to use double precision in the delay line
*
*   We could reverse the shift order to use a memcpy routine
*   which might be faster. But we also might want to examine BLAS
*   first. For now ...
*
**************************************************************************
*/
int Iir(float* dst, float* src, int n, const double* a, const double*b, double* dly, int ntaps)
{
    int k;
    double sumF,sumR,d;


    // assume the was DC (-inf..0;
    sumF = b[0];
    sumR = 0;                        //init
    for(k=1;k<ntaps+1;k+=1)                 // forward taps
    {
        sumF += b[k];
        sumR += a[k];
    }
    d = *src / sumF;
    for(k=0;k<ntaps+1;k+=1)                 // forward taps
        dly[k] = d;

    while(n-- > 0)
    {
        sumF = sumR = 0;                        //init
        for(k=1;k<ntaps+1;k+=1)                 // forward taps
            sumF += dly[k]*b[k];
        for(k=1;k<ntaps+1;k+=1)                 // backwards taps
            sumR += dly[k]*a[k];
        for(k = ntaps;k > 1;k -= 1)             // shift
            dly[k] = dly[k-1];
        dly[1] = (*src++) - sumR;               // recurse
        *dst++ = dly[1]*b[0]+sumF;              // forward and store
#if EASYWINDEBUG & 0
printf("iir %f %f %f\n",*(dst-1),*(src-1),dly[1]);
#endif
    }

   return 0;
}
/*
**************************************************************************
*  Function: int Conv(float* a, int na, float* b, int bn, float *dst)
*
*  Purpose: Convolution
*
*  Comments:
*   will create a dst of length na+nb-1
*   sum(a(k)*b(n-k))
*
*   This is crying for optimization However first we make it work
*   The first pass is indexed so we can print indicies.
*   A preamble creates a set of pointers such that the nominal
*   input nx > ny. The final opt version might duplicate code TBD
**************************************************************************
*/
int Conv(float* a, int na, float* b, int nb, float *dst)
{
    int k,n;
    double sum;
    double pi;
    float *px, *py;
    int nx,ny;
    int nl,nd;

    if(na > nb)
    {
        nx = na;
        ny = nb;
        px = a;
        py = b;
    }
    else
    {
        nx = nb;
        ny = na;
        px = b;
        py = a;
    }

    {
        n = 0;
        nl = 0;
        nd = 0;
        while(nd < ny)
        {
#if EASYWINDEBUG & 0
printf("%2d X %2d (%2d) -> %2d\n",
(0),(nl),nl+1,nd);
#endif
            dst[nd] = macpm(&px[0],&py[nl],nl+1);
            nd += 1;
            nl += 1;
        }
#if EASYWINDEBUG & 0
printf("\n");
#endif
        nl = 1;
        while(nd < nx)
        {
#if EASYWINDEBUG & 0
printf("%2d X %2d (%2d) -> %2d\n",
(nl),(ny-1),ny,nd);
#endif
            dst[nd] = macpm(&px[nl],&py[ny-1],ny);
            nd += 1;
            nl += 1;
        }
#if EASYWINDEBUG & 0
printf("\n");
#endif
        nl = ny-1;
        while(nd<nx+ny-1)
        {
#if EASYWINDEBUG & 0
printf("%2d X %2d (%2d) -> %2d\n",
(nx-nl),(ny-1),nl,nd);
#endif
            dst[nd] = macpm(&px[nx-nl],&py[ny-1],nl);
            nd += 1;
            nl -= 1;
        }
    }

#if 0   // speed up Use counters because many HW have them
        // assume na >= nb
    {
        px = pa;
        py = pb;
        nl = 1;
        n = nb;
        while(--n >= 0)
        {
            *dst++ = macpm(px,py++,nl++);
        }
//        px = &pa[1];
//        py = &pb[nb-1];
//        nl = nb;
        n = na-nb-1;
        while(--n >= 0)
        {
            *dst++ = macpm(px++,py,nl);
        }
//        px = &pa[na-nb]; No Change
//        py = &pb[nb-1];  No Change
        nl = nb
        n = nb;
        while(--n >= 0)
        {
            *dst++ = macpm(px++,py,nl--);
        }
    }
#endif

return 0;
}

/*
**************************************************************************
*  Function: int Pixel2Date(UTIME* date, int idx)
*
*  Purpose: Convert a graph pixel to a data
*
*  Comments:
*
**************************************************************************
*/
UTIME Pixel2Date(struct tm* date1, int idx, struct S_PXY* pltctl)
{
    U32     tmptime;
    UTIME   uttime;
    double  x;

    x = (double)(idx-pltctl->ixmin);                    // type conversion
    x = x/pltctl->xscl + pltctl->xmin;  // pixels to samples
//    x = x-DelayWeight;                  // offset for the filtered plots
                                          // I think the plot shift does this
    tmptime = x+0.5;                        // round
    uttime = SampToUtime((S32*)&tmptime);         // Samples to Utime
    if(date1)
        UtimeToTM(&uttime, date1);          // Get the date if asked
#if EASYWINDEBUG & 0
{
if(date1)
printf("x=%lf tmptime= %ld uttime= %ld %s\n",x,tmptime,uttime,asctime(date1));
else
printf("x=%lf tmptime= %ld uttime= %ld\n",x,tmptime,uttime);
}
#endif

    return uttime;
}

/*
**************************************************************************
*  Function: int Date2Index(UTIME* date, int idx)
*
*  Purpose: Convert a date to a graph index
*
*  Comments:
*   Shell for
*       TMToUtime
*       GetSampleIdx
*
**************************************************************************
*/
int Date2Index(struct tm* date1, int idx)
{
    UTIME   utime64;

    TMToUtime(&utime64, date1);
    return GetSampleIdx(utime64);
}
void dumpf(double* f, int cols, int tot)
{
    int k1,k2;

    while(tot > 0)
    {
        for(k1=0;k1<cols;k1++)
        {
            printf("%lf ",*f++);
        }
        printf("\n");
        tot -= cols;
    }
}


/*
**************************************************************************
*  Function: int PolySqRaw(long* t, double* w, float RL, float RH, float* a, int nPoly)
*
*  Purpose: Polynomial fit
*
*  Comments:
*   Times are converted to days for the calculation
*
*
**************************************************************************
*/
int PolySqRaw(UTIME* t, double* w, float RL, float RH, float* a, int nPoly)
{
    int     n;
    int     k;
    float   x,y;
    int c1,c2;
    int pivot[FITORDMAX];
    float x1,y1;
    float*  pFloat;

    if(FitOrd < 1) FitOrd = 1;
    if(FitOrd > FITORDMAX) FitOrd = FITORDMAX;
    memset(fitmatrix,0,sizeof(fitmatrix));
    memset(fitrhs,0,sizeof(fitrhs));
    memset(fitcorr,0,sizeof(fitcorr));

    n=0;
    for(k=0; k<nRawData; k+=1)                  // Compute sums
    {
        y = 0;
//        x = UtimeToSamp(&t[k]);
//        x=(t[k] - TSamp1Start) * SampPerTic;
        x=(t[k] - TSamp1Start)/TICSPERSEC;
        if( (x<RH*SampPerDay) && (x>RL*SampPerDay))     // check range
        {
            n += 1;                             // yep, inc count
            y = w[k]-WeightLVP[k];                           // get weight

#if EASYWINDEBUG & 0
            GetPlotPoints(x,y,&c1,&c2,&PltCtl1);
            moveto(c1-2,c2-2);
            lineto(c1+3,c2-2);
            lineto(c1+3,c2+3);
            lineto(c1-2,c2+3);
            lineto(c1-2,c2-2);
            moveto(c1,c2);
#endif

            x1=1;                               // compute RHS Yn * Xn^M
            for(c1 = 0; c1 <= FitOrd; c1 += 1)
            {
                fitrhs[FitOrd-c1] += y*x1;     // store in decreasing order
                x1 *= x;
            }

            x1=1;                                // compute matrix elements Xn^M
            y1=1;
            for(c1 = 0; c1 <= 2*FitOrd-1; c1 += 1)
            {
                fitcorr[2*FitOrd-c1-1] += x*x1; // store in decreasing order
                x1 *= x;
            }
        }
    }
    fitcorr[2*FitOrd]=n;   // shouldn't have to do this!!!!

    if(n <= FitOrd)                       // none in the range
    {
        memset(a,0,sizeof(float)*(FitOrd+1));
        return(-2);
    }

    k=0;
    for(c1 =0; c1 <= FitOrd; c1+=1)        // fill matrix eg[{4,3,2}{3,2,1}{2,1,0}]
    {
        for(c2 =0; c2 <= FitOrd; c2+=1)
        {
            fitmatrix[c1*(FitOrd+1)+c2] = fitcorr[k+c2];
        }
        k += 1;
    }

// slope = (n*sumxy - sumx*sumy)/(n*sumxx-sumx*sumx);
    {
    double  sumxy, sumxx, sumx, sumy, sum0;
    int     lastmat;
    lastmat = (FitOrd+1)*(FitOrd+1)-1;
    sum0    = fitmatrix[lastmat];
    sumx    = fitmatrix[lastmat-(FitOrd+1)];
    sumxx   = fitmatrix[lastmat-(FitOrd+1)-1];
    sumy    = fitrhs[FitOrd];
    sumxy   = fitrhs[FitOrd-1];
    AvgWGain  = (sum0*sumxy - sumx*sumy)/(sum0*sumxx-sumx*sumx)*SampPerDay;
//    AvgWeight = sumy/sum0;  //Not good this has been adjusted for LV
                            // Only valid when LVP is off
#if EASYWINDEBUG & 0
printf("%f %f %f %f %f \n",
    sumxy, sumxx, sumx, sumy, sum0);
#endif

#if EASYWINDEBUG & 0
printf("PolySqRaw: Data size n=%d, AvgWGain=%f\n",(int)sum0,AvgWGain);
#endif

#if EASYWINDEBUG & 0
printf("Solve  k %d\n",k);
dumpf(fitrhs,FitOrd+1,(FitOrd+1));
dumpf(fitmatrix,FitOrd+1,(FitOrd+1)*(FitOrd+1));
printf("\n");
#endif
#if USELAPACK
    {
        int forder = 1+FitOrd;      // readability temps
        int nsolutions = 1;
        int dimA = forder;
        int dimR = forder;
        int lwork = FITORDMAX*2;
        char cstore = 'U';

        k = 0;
#if 0   // general matrix
        dgesv_( (integer *)&forder,            // Order
                (integer *)&nsolutions,        // number of rhs (multiple solutions)
                (doublereal*)fitmatrix,          // matrix
                (integer*)&dimA,              // leading dimension of A (order)
                (integer*)pivot,              // pivoting results
                (doublereal*)fitrhs,             // right-hand-sides (1 or more)
                (integer *)&dimR,              // leading dimension of R (order)
                (integer *)&k);                // return value (0 is OK)
#endif

#if 1   // symmetric matrix
        k=0;
        dsysv_( (char*)&cstore,
                (integer *)&forder,            // Order
                (integer *)&nsolutions,        // number of rhs (multiple solutions)
                (doublereal*)fitmatrix,          // matrix
                (integer *)&dimA,              // leading dimension of A (order)
                (integer*)pivot,              // pivoting results
                (doublereal*)fitrhs,             // right-hand-sides (1 or more)
                (integer *)&dimR,              // leading dimension of R (order)
                (doublereal*)fitwork,            // working area
                (integer *)&lwork,
                (integer *)&k);                // return value (0 is OK)
#endif

    }
    for(n=0;n<(1+FitOrd);n+=1)
    {
        a[n]=fitrhs[n];
    }
#endif //USELAPACK
#if ! USELAPACK
    FitOrd = 1;
    a[0] = (sum0*sumxy - sumx*sumy)/(sum0*sumxx-sumx*sumx);
    a[1] = (sumy - a[0]*sumx)/sum0;
#endif
    }

#if EASYWINDEBUG & 0
printf("Result  k %d %lf\n",k,fitwork[0]);
dumpf(fitrhs,FitOrd+1,(FitOrd+1));
dumpf(fitmatrix,FitOrd+1,(FitOrd+1)*(FitOrd+1));
printf("\n");
#endif

    return 1;

}

/*
**************************************************************************
*  Function: int PolySqWin(long* t, float* w, float RL, float RH, float* a, int nPoly)
*
*  Purpose: Polynomial fit
*
*  Comments:
*   This is basically the same as PolySqRaw except that it assumes equal
*   sample spacing. We use it on the interpolated data to compute a sliding
*   window.
*
*   The arguments are a little different
*   float*      w       sampled array
*   int         iRL     start index
*   int         iRH     end index (inclusive)
*   float*      a       destination poly
*   int         nPoly   poly order
*
*
**************************************************************************
*/
int PolySqWin(float* w, int iRL, int iRH, float* a, int nPoly)
{
    int     n;
    int     k,loopcount;
    float   x,y;
    int     c1,c2;
    int     pivot[FITORDMAX];
    double  x1,y1;
    float*  pFloat;

    if(nPoly < 1) nPoly = 1;
    if(nPoly > FITORDMAX) nPoly = FITORDMAX;
    memset(fitmatrix,0,sizeof(fitmatrix));
    memset(fitrhs,0,sizeof(fitrhs));
    memset(fitcorr,0,sizeof(fitcorr));

    n=0;
    for(loopcount=iRL; loopcount<=iRH; loopcount+=1)                          // Compute sums
    {
        y = 0;
        x = loopcount;
        {
            n += 1;                                 // yep, inc count
//            y = w[loopcount]-WeightLVP[loopcount];                  // get weight
            y = w[loopcount];                      // get weight

#if EASYWINDEBUG & 0
if( ((loopcount>384) && (loopcount<420)) )
{
    printf("loopcount %d x %f y %f\n",loopcount,x,y);
}
#endif
            x1=1;                                   // compute RHS Yn * Xn^M
            for(c1 = 0; c1 <= nPoly; c1 += 1)
            {
                fitrhs[nPoly-c1] += y*x1;           // store in decreasing order
                x1 *= x;
            }

            x1=1;                                   // compute matrix elements Xn^M
            y1=1;
            for(c1 = 0; c1 <= 2*nPoly-1; c1 += 1)
            {
                fitcorr[2*nPoly-c1-1] += x*x1;      // store in decreasing order
                x1 *= x;
            }
        }
    }
    fitcorr[2*nPoly]=n;   // shouldn't have to do this!!!!

    if(n <= nPoly)                                  // none in the range
    {
#if EASYWINDEBUG & 1
printf("too few samples in PolySqWin %d\n",n);
#endif
        memset(a,0,sizeof(float)*(nPoly+1));
        return(-2);
    }

    k=0;
    for(c1 =0; c1 <= nPoly; c1+=1)                  // fill matrix eg[{4,3,2}{3,2,1}{2,1,0}]
    {
        for(c2 =0; c2 <= nPoly; c2+=1)
        {
            fitmatrix[c1*(nPoly+1)+c2] = fitcorr[k+c2];
        }
        k += 1;
    }

// slope = (n*sumxy - sumx*sumy)/(n*sumxx-sumx*sumx);
    {
    double  sumxy, sumxx, sumx, sumy, sum0;
    int     lastmat;
    lastmat = (FitOrd+1)*(FitOrd+1)-1;
    sum0    = fitmatrix[lastmat];
    sumx    = fitmatrix[lastmat-(FitOrd+1)];
    sumxx   = fitmatrix[lastmat-(FitOrd+1)-1];
    sumy    = fitrhs[FitOrd];
    sumxy   = fitrhs[FitOrd-1];
    AvgWGain  = (sum0*sumxy - sumx*sumy)/(sum0*sumxx-sumx*sumx)*SampPerDay;
//    AvgWeight = sumy/sum0;  //Not good this has been adjusted for LV
                            // Only valid when LVP is off
#if EASYWINDEBUG & 0
printf("%f %f %f %f %f \n",
    sumxy, sumxx, sumx, sumy, sum0);
#endif

#if EASYWINDEBUG & 0
printf("Solve %d\n",(int)fitcorr[2*nPoly]);
dumpf(fitrhs,nPoly+1,(nPoly+1));
dumpf(fitmatrix,nPoly+1,(nPoly+1)*(nPoly+1));
printf("\n");
#endif
#if USELAPACK
    {
        int forder = 1+nPoly;      // readability temps
        int nsolutions = 1;
        int dimA = forder;
        int dimR = forder;
        char cstore = 'U';

        k = 0;
//        dgesv_( &forder,            // Order
        dgesv_( (integer *)&forder,            // Order
                (integer *)&nsolutions,        // number of rhs (multiple solutions)
                (doublereal*)fitmatrix,          // matrix
                (integer *)&dimA,              // leading dimension of A (order)
                (integer*)pivot,              // pivoting results
                (doublereal*)fitrhs,             // right-hand-sides (1 or more)
                (integer *)&dimR,              // leading dimension of R (order)
                (integer *)&k);                // return value (0 is OK)
    }
    for(n=0;n<(1+FitOrd);n+=1)
    {
        a[n]=fitrhs[n];
    }
#endif

#if ! USELAPACK
    FitOrd = 1;
    a[0] = (sum0*sumxy - sumx*sumy)/(sum0*sumxx-sumx*sumx);
    a[1] = (sumy - a[0]*sumx)/sum0;
#endif
    }

#if EASYWINDEBUG & 0
if(k!=0)
{
printf("result  k %d\n",k);
dumpf(fitrhs,nPoly+1,(nPoly+1));
dumpf(fitmatrix,nPoly+1,(nPoly+1)*(nPoly+1));
printf("\n");
}
#endif

    return 1;

}

/*
**************************************************************************
*  Function: float PolyVal(float x, float* w, int nPoly)
*
*  Purpose: Polynomial evaluation
*
*  Comments:
*
*
**************************************************************************
*/
float PolyVal(float x, float* w, int nPoly)
{
    double  t,tx;
    int     n;

    if(nPoly < 0 )
        return(0);
    tx=x;
    t = *w++;
    while(--nPoly > 0)
    {
#if EASYWINDEBUG & 0
printf(" %f ",t);
#endif
        t=t*tx + (*w++);
    }
#if EASYWINDEBUG & 0
printf("\n");
#endif
    if(t > 1e38)
        t=1e38;
    else if(t < -1e38)
        t=-1e38;
    return(t);
}

/*
**************************************************************************
*
*  Function: int StringToSDate(char *str_in, char *str_out);
*
*  Purpose: Date recognizer
*
*  Comments:
*
*  Simplified: Must be m/d/y format
*
*
*  finall we check the numbers against the range
*
*characters in s2
*size_t strspn(const char *s1, const char *s2);
*strspn returns the length of the initial segment
*of s1 that consists entirely of characters from s2.
*
*characters not in s2
*size_t strcspn(const char *s1, const char *s2);
*strcspn returns the length of the initial segment
*of string s1 that consists entirely of characters
*NOT from string s2.
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
    char    sTmp[64];   // s
    int     iChar,eChar;

    int     dd,mm,yy;                           // build date in these
    int     hh,mn,ampm;                         // time in here
    int numbers[4];     // converted numbers
    int numpos[4];       // positions of numbers
    int mpos;           // txt month position (if any)
    int nnums;          // number of converted numbers [0..3]
    int i,j,k;          // general use
    int flag;
    int d_state;
    char *s1,*s2;
    int retval;

/*
*---
* Copy into a working buffer so strtok can clobber things
*---
*/
    strncpy(sTmp,str_in,63);
    sTmp[63]=0;
    iChar = 0;
    eChar = strlen(sTmp);

#if EASYWINDEBUG & 0
    printf("StringToSDate <%s> <%s>\n",str_in,sTmp);
#endif

    iChar += strspn(&sTmp[iChar],MM_white);       // skip to the first nonwhite
#if EASYWINDEBUG & 0
    printf("skip white to <%s>  %d\n",&sTmp[iChar],iChar);
#endif
    if(iChar >= eChar) return(-12);
    k=sscanf(&sTmp[iChar],"%d",&mm);                // is mm (month)
#if EASYWINDEBUG & 0
    printf("sscanf <%s> k %d mm = %d\n",&sTmp[iChar],k, mm);
#endif
    if(k != 1) return(-13);
    iChar += strspn(&sTmp[iChar],MM_digits);        // skip to the following non-digit
#if EASYWINDEBUG & 0
    printf("skip digits to <%s>  %d\n",&sTmp[iChar],iChar);
#endif
    if(iChar >= eChar) return(-14);

    iChar += strspn(&sTmp[iChar],MM_seperator);     // skip seperators
#if EASYWINDEBUG & 0
    printf("skip seperators to <%s> = %d\n",&sTmp[iChar],mm);
#endif
    if(iChar >= eChar) return(-22);
    k=sscanf(&sTmp[iChar],"%d",&dd);                // is dd (day)
#if EASYWINDEBUG & 0
    printf("sscanf <%s> k %d dd = %d\n",&sTmp[iChar],k,dd);
#endif
    if(k != 1) return(-23);
    iChar += strspn(&sTmp[iChar],MM_digits);        // skip to the following non-digit
#if EASYWINDEBUG & 0
    printf("skip digits to <%s>  %d\n",&sTmp[iChar],iChar);
#endif
    if(iChar >= eChar) return(-24);

    iChar += strspn(&sTmp[iChar],MM_seperator);     // skip seperators
#if EASYWINDEBUG & 0
    printf("skip seperators to <%s> = %d\n",&sTmp[iChar],mm);
#endif
    if(iChar >= eChar) return(-32);
    k=sscanf(&sTmp[iChar],"%d",&yy);                // is yy (year)
#if EASYWINDEBUG & 0
    printf("sscanf <%s> k %d yy = %d\n",&sTmp[iChar],k,yy);
#endif
    if(k != 1) return(-33);
    iChar += strspn(&sTmp[iChar],MM_digits);        // skip to the following non-digit
#if EASYWINDEBUG & 0
    printf("skip digits to <%s>  %d\n",&sTmp[iChar],iChar);
#endif
    if(iChar >= eChar) return(-34);

    iChar += strspn(&sTmp[iChar],MM_seperator);     // skip seperators
#if EASYWINDEBUG & 0
    printf("skip seperators to <%s> = %d\n",&sTmp[iChar],mm);
#endif
    if(iChar >= eChar) return(-42);
    k=sscanf(&sTmp[iChar],"%d",&hh);                // is hh (hours)
#if EASYWINDEBUG & 0
    printf("sscanf <%s> k %d hh = %d\n",&sTmp[iChar],k,hh);
#endif
    if(k != 1) return(-43);
    iChar += strspn(&sTmp[iChar],MM_digits);        // skip to the following non-digit
#if EASYWINDEBUG & 0
    printf("skip digits to <%s>  %d\n",&sTmp[iChar],iChar);
#endif
    if(iChar >= eChar) return(-44);

    iChar += strspn(&sTmp[iChar],MM_seperator);     // skip seperators
#if EASYWINDEBUG & 0
    printf("skip seperators to <%s> = %d\n",&sTmp[iChar],mm);
#endif
    if(iChar >= eChar) return(-52);
    k=sscanf(&sTmp[iChar],"%d",&mn);                // is mn (min)
#if EASYWINDEBUG & 0
    printf("sscanf <%s> k %d mn = %d\n",&sTmp[iChar],k,mn);
#endif
    if(k != 1) return(-53);
    iChar += strspn(&sTmp[iChar],MM_digits);        // skip to the following non-digit
#if EASYWINDEBUG & 0
    printf("skip digits to <%s>  %d\n",&sTmp[iChar],iChar);
#endif
    if(iChar >= eChar) return(-54);

    iChar += strspn(&sTmp[iChar],MM_white);       // skip any white
#if EASYWINDEBUG & 0
    printf("ampm skip white to <%s>  %d\n",&sTmp[iChar],iChar);
#endif
    if((sTmp[iChar] == 'p') || (sTmp[iChar] == 'P'))
        ampm = 12;
    else
        ampm = 0;
    iChar += strspn(&sTmp[iChar],"AMPamps");
#if EASYWINDEBUG & 0
    printf("skipped MPam to <%s>  %d\n",&sTmp[iChar],iChar);
#endif

/*
*---
* Final checks, day amd month have to be in range
*---
*/

    hh = (hh%12) + ampm;
    mm %= 60;
    hh %= 24;

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

    tout->tm_mon = mm;
    tout->tm_mday = dd;
    tout->tm_year = yy;
    tout->tm_min = mn;
    tout->tm_hour = hh ;
    tout->tm_sec = 0;

#if EASYWINDEBUG & 0
    printf("%d/%d/%d %d:%02d%c \n",mm,dd,yy,hh,mn,(ampm?'P':'A'));
#endif

    return(iChar);

}

/*
**************************************************************************
*
*  Function: U32 UtimeToSamp(UTIME* utime64)
*   Convert UTIME to sample offset
*  Function: F64 UtimeToSec(UTIME* utime64)
*   Convert UTIME to seconds
*  Function: int UtimeToTM(UTIME* utime64, struct tm* sTime)
*   Use localtime to convert to (struct tm) and copy
*
*  Purpose: Convert windows time to Sample index
*
*  Comments:
*
*   Compute the sample index from the windows time and the offset
*
**************************************************************************
*/
F64 UtimeToSec(UTIME* utime64)
{
    UTIME   tmp1U64;
    F64     retval;

    tmp1U64 = *((UTIME*)utime64);
    retval = tmp1U64;
    return retval;
}

U32 UtimeToSamp(UTIME* utime64)
{
    UTIME   tmp1U64;
    U32     retval;

    tmp1U64 = *((UTIME*)utime64);
    retval = (tmp1U64 - TSamp1Start) * SampPerTic;
    return retval;
}

int UtimeToTM(UTIME* utime64, struct tm* sTime)
{
    struct tm* ptmtime;
    UTIME       utime;
    int         retval;

#if USEUNIXTIME
    ptmtime = localtime((UTIME*)utime64);
    *sTime =  *ptmtime;
#else
    SYSTEMTIME  sLocalTime;
    memset(sTime,0,sizeof(struct tm));
    FileTimeToSystemTime((FILETIME*)utime64,&sLocalTime);
    sTime->tm_year  =sLocalTime.wYear-1900;
    sTime->tm_mon   =sLocalTime.wMonth-1;
    sTime->tm_mday  =sLocalTime.wDay;
    sTime->tm_wday  =sLocalTime.wDayOfWeek;
    sTime->tm_hour  =sLocalTime.wHour;
    sTime->tm_min   =sLocalTime.wMinute;
    sTime->tm_sec   =sLocalTime.wSecond;
#endif
    return 0;
}

UTIME SecToUtime(F64* sec)
{
    UTIME     retval;

    retval = ((UTIME)sec) * TICSPERSEC;
    return retval;
}

UTIME SampToUtime(S32* samp)
{
    UTIME     tmp1U64;
    UTIME     retval;

    tmp1U64 = *((S32*)samp);
    retval = (tmp1U64 * TicPerSamp) + TSamp1Start;
    return retval;
}

UTIME TMToUtime(UTIME* putime64, struct tm* sTime)
{
    UTIME       utime;
    int         retval;

#if USEUNIXTIME
    utime = mktime(sTime);
#else
    SYSTEMTIME  sLocalTime;

    memset(&sLocalTime,0,sizeof(sLocalTime));
    sLocalTime.wYear    =sTime->tm_year+1900;
    sLocalTime.wMonth   =sTime->tm_mon+1;
    sLocalTime.wDay     =sTime->tm_mday;
    sLocalTime.wDayOfWeek=sTime->tm_wday;
    sLocalTime.wHour    =sTime->tm_hour;
    sLocalTime.wMinute  =sTime->tm_min;
    sLocalTime.wSecond  =sTime->tm_sec;
    if(!SystemTimeToFileTime(&sLocalTime,(FILETIME*)&utime))
    {
MessageLastError();
printf("Err: SystemTimeToFileTime\n%d,%d,%d,%d,%d,%d\n",
    sLocalTime.wYear,
    sLocalTime.wMonth,
    sLocalTime.wDay,
    sLocalTime.wHour,
    sLocalTime.wMinute,
    sLocalTime.wSecond);
    }
#endif
    if(putime64)
        *putime64 =  utime;
    return utime;
}
/*
**************************************************************************
*
*  Function: static int GetPlotPoints(float x, float y, int *ix, int *iy, struct S_PXY*   PltCtl)
*
*  Purpose: Get the integer points for the plot
*
*  Comments:
*
*   Uses PltCtl to compute the integer coordinates for a plot
*   if the destination pointer *ix or *iy is null, the result is
*   not stored
**************************************************************************
*/
static int GetPlotPoints(float x, float y, int *ix, int *iy, struct S_PXY*   PltCtl)
{

    //clamp to box
    if(ix)
    {
        if(x > PltCtl->xmax)x = PltCtl->xmax;
        if(x < PltCtl->xmin)x = PltCtl->xmin;
        x = (x-PltCtl->xmin) * PltCtl->xscl + 0.5;
        *ix = (int)x + PltCtl->ix0;
    }

    if(iy)
    {
        if(y > PltCtl->ymax)y = PltCtl->ymax;
        if(y < PltCtl->ymin)y = PltCtl->ymin;
        y = (y-PltCtl->ymin) * PltCtl->yscl + 0.5;
        *iy = (int)y + PltCtl->iy0;
    }
    return(1);
}


/*
**************************************************************************
*
*  Function: int Plotxx
*
*  Purpose: Plot squally spaced samples
*  OBSOLETE use PlotXY with the index array
*
*  Comments:
*
*
* Markers
*   0   line to every point (normal)
*   1   +
*   2   X
*   3   box
*   4   suppress draw at y limits
*/
static int PlotXX(
    float*  ydata,                  // array
    int     nmPlot,              // number of samples
    float   ymin, float ymax,          // y bounds
    float   xpixels, float ypixels,      // scale factor
    int     ix0, int iy0,             // offsets
    int     colour, int marker
    )
{
    int k,n;
    int ix,iy;
    float x,y;
    float xscl,yscl;
    int     FlagPlot = 1;

    if(nmPlot <= 0) return(-1);
    if(ymax-ymin <= 0) return -1;

    newpen(colour,PS_SOLID+((marker>>8)&7));
    yscl = (ypixels/(ymax-ymin));
    xscl = (xpixels/nmPlot);

    for(k=0; k<nmPlot; k+=1)
    {
        x = (k*xscl);
        y = ydata[k];
        if(y < ymin)
            y = ymin;
        if(y > ymax)
            y = ymax;
        ix = (int)(x+.5);
        iy = (int)((y-ymin)*yscl+.5);
        switch(marker&255)
        {
            default:
            case 0:                 // default connect lines
                if(FlagPlot)
                    moveto(ix,iy);
                else
                    lineto(ix,iy);
                FlagPlot = 0;
            break;

            case 1:                 // cross
                if(FlagPlot)
                    moveto(ix,iy);
                else
                {
                    moveto(ix-2,iy);
                    lineto(ix+3,iy);
                    moveto(ix,iy-2);
                    lineto(ix,iy+3);
                    moveto(ix,iy);
                }
                FlagPlot = 0;
            break;

            case 2:                 // X
                if(FlagPlot)
                    moveto(ix,iy);
                else
                {
                    moveto(ix-2,iy-2);
                    lineto(ix+3,iy+3);
                    moveto(ix-2,iy+3);
                    lineto(ix+3,iy-2);
                    moveto(ix,iy);
                }
                FlagPlot = 0;
            break;

            case 3:                 // box
                if(FlagPlot)
                    moveto(ix,iy);
                else
                {
                    moveto(ix-2,iy-2);
                    lineto(ix+3,iy-2);
                    lineto(ix+3,iy+3);
                    lineto(ix-2,iy+3);
                    lineto(ix-2,iy-2);
                    moveto(ix,iy);
                }
                FlagPlot = 0;
            break;

            case 4:                 //ignore points on the frame
                if(y <= ymin)
                    continue;
                if(y >= ymax)
                    continue;
                if(FlagPlot)
                    moveto(ix,iy);
                else
                    lineto(ix,iy);
                FlagPlot = 0;
            break;
        }

    }

    return(0);
}


/*
**************************************************************************
*
*  Function: int PlotXYCommon
static int PlotXYCommon(
    struct S_PXY*   PltCtl
    )
*
*  Purpose: Preprocessing
*
*/
static int PlotXYCommon(struct S_PXY* PltCtl)
{
    if((PltCtl->xmax-PltCtl->xmin) <= 0)
        return -1;
    if((PltCtl->ymax-PltCtl->ymin) <= 0)
        return -1;
    PltCtl->xscl = (PltCtl->ixdelta)/(PltCtl->xmax-PltCtl->xmin);
    PltCtl->yscl = (PltCtl->iydelta)/(PltCtl->ymax-PltCtl->ymin);
    PltCtl->ixmin = PltCtl->ix0 ;
    PltCtl->iymin = PltCtl->iy0 ;
    PltCtl->ixmax = PltCtl->ix0 + PltCtl->ixdelta ;
    PltCtl->iymax = PltCtl->iy0 + PltCtl->iydelta ;

#if EASYWINDEBUG & 0
printf("%d %d %d %d\n",PltCtl->ixmin,PltCtl->ixmax,PltCtl->iymin,PltCtl->iymax);
printf("xscl %f yscl %f ixdelta %d %d\n",PltCtl->xscl,PltCtl->yscl,PltCtl->ixdelta,PltCtl->iydelta);
moveto(PltCtl->ixmin,PltCtl->iymin);
lineto(PltCtl->ixmax,PltCtl->iymin);
lineto(PltCtl->ixmax,PltCtl->iymax);
lineto(PltCtl->ixmin,PltCtl->iymax);
lineto(PltCtl->ixmin,PltCtl->iymin);

#endif

    return(0);
}
/*
**************************************************************************
*
*  Function: int PlotxY
static int PlotXY(
    float*  xdata,
    float*  ydata,                  // array
    int             colour,
    int             marker,
    struct S_PXY*   PltCtl
    )
*
*  Purpose: Plot squally spaced samples
*
*  Comments:
*
*
* Markers
*   0   line to every point (normal)
*   1   +
*   2   X
*   3   box
*   4   suppress draw at y limits
*
*   upper [8..12] bits of marker pen width
*   0   1
*   1   1
*   2   2
*   3   4
*/
static int PlotXY(
    float*          xdata,
    float*          ydata,                  // array
    int             nmPlot,              // number of samples
    int             colour,
    int             marker,
    struct S_PXY*   PltCtl
    )
{
    int k,n;
    int ix,iy;
    float x,y;
    int     FlagPlot = 1;

    if(nmPlot <= 0) return(-1);
    if(PltCtl->ymax-PltCtl->ymin <= 0) return -1;
    if(PltCtl->xmax-PltCtl->xmin <= 0) return -1;

    newpen(colour,PS_SOLID+((marker>>4)&(7<<4)));
    PlotXYCommon(PltCtl);
    moveto(0,0);
    for(k=0; k<nmPlot; k+=1)
    {

        GetPlotPoints(xdata[k],ydata[k],&ix,&iy,PltCtl);
#if EASYWINDEBUG & 0
printf("%5.1f %5.1f \n",xdata[k],ydata[k]);
#endif

        switch(marker&255)
        {
            default:
            case 0:                 // default connect lines
                if(FlagPlot)
                    moveto(ix,iy);
                else
                    lineto(ix,iy);
                FlagPlot = 0;
            break;

            case 1:                 // cross
                if(FlagPlot)
                    moveto(ix,iy);
                else
                {
                    moveto(ix-2,iy);
                    lineto(ix+3,iy);
                    moveto(ix,iy-2);
                    lineto(ix,iy+3);
                    moveto(ix,iy);
                }
                FlagPlot = 0;
            break;

            case 2:                 // X
                if(FlagPlot)
                    moveto(ix,iy);
                else
                {
                    moveto(ix-2,iy-2);
                    lineto(ix+3,iy+3);
                    moveto(ix-2,iy+3);
                    lineto(ix+3,iy-2);
                    moveto(ix,iy);
                }
                FlagPlot = 0;
            break;

            case 3:                 // box
                if(FlagPlot)
                    moveto(ix,iy);
                else
                {
                    moveto(ix-2,iy-2);
                    lineto(ix+3,iy-2);
                    lineto(ix+3,iy+3);
                    lineto(ix-2,iy+3);
                    lineto(ix-2,iy-2);
                    moveto(ix,iy);
                }
                FlagPlot = 0;
            break;

            #if 1
            case 4:                 //ignore points on the frame
            // *** iffy
                if(ix >= PltCtl->ixmax)continue;
                if(iy >= PltCtl->iymax)continue;
                if(ix <= PltCtl->ixmin)continue;
                if(iy <= PltCtl->iymin)continue;
#if EASYWINDEBUG & 0
printf("%d %5.1f %5.1f \n",k,xdata[k],ydata[k]);
#endif
                if(FlagPlot)
                    moveto(ix,iy);
                else
                    lineto(ix,iy);
                FlagPlot = 0;
            break;
            #endif
        }

    }

    return(0);
}

/*
**************************************************************************
*
*  Function: static int GridXY(
*               float   xstep,
*               float   ystep,                  // array
*               int     colour,
*               struct S_PXY*   PltCtl
*               )
*
*  Purpose: Plot grid
*
*  Comments:
*   If step <= 0 the gridline is not plotted
*
*
* Markers
*   0   line to every point (normal)
*   1   +
*   2   X
*   3   box
*   4   suppress draw at y limits
*/
static int GridXY(
    float   xstep,
    float   ystep,                  // array
    int     colour,
    struct S_PXY*   PltCtl
    )
{

    float   x;
    int     ix,iy;
    int     drawstep,drawcnt;
    UTIME     indexl;
    char    tmptxt[32];
    struct tm tmptm;
    UTIME   tmputime;

    PlotXYCommon(PltCtl);

    newpen(colour,PS_DOT);

    if(xstep >0)
    {
#if 0
        x = PltCtl->xmin;
        indexl=x-PltCtl->ixmin;
        indexl=SampToUtime((S32*)&indexl);
        UtimeToTM(&indexl,&tmptm);
        tmptm.tm_hour = 0;
        tmptm.tm_min = 0;
        TMToUtime(&tmputime, &tmptm);
        do
        {
            tmptm.tm_mday +=1;
            indexl=UtimeToSamp(&tmputime);
            GetPlotPoints((int)indexl,0,&(int)indexl,NULL,PltCtl);
            moveto(indexl,PltCtl->iymin);
            lineto(indexl,PltCtl->iymax);
            //x += xstep;
        }while((indexl < PltCtl->ixmax));
#endif

        /* Big hack to step x by 1, 2, 3, 5 */
            x = (PltCtl->xmax - PltCtl->xmin)/(SampPerDay);
            if(x < 50)       {drawstep =1;}
            else if(x < 100) {drawstep =2;}
            else             {drawstep =5;}
            drawcnt = 0;

        x = PltCtl->xmin;                       // xmin
        indexl = x;                             // type convert for pointer
        indexl=SampToUtime((S32*)&indexl);      // index1 is a UTIME conversion
        UtimeToTM(&indexl,&tmptm);              // parse into struct tm
        tmptm.tm_hour = 0;                      // set to midnight
        tmptm.tm_min = 0;
        TMToUtime(&indexl,&tmptm);          // get back
        do
        {
            indexl += (TicPerSamp*SampPerDay);   // inc by one day
            UtimeToTM(&indexl,&tmptm);              // parse
            x=UtimeToSamp(&indexl);
            GetPlotPoints(x,0,&ix,NULL,PltCtl);
            if((drawcnt++ % drawstep) == 0)
            {
            newpen(colour,PS_DOT);
            moveto(ix,PltCtl->iymin);
            lineto(ix,PltCtl->iymax);
            sprintf(tmptxt,"%-3.0f",x/SampPerDay);
            drawtext(ix+2,PltCtl->iymin, tmptxt, JTEXT_LEFT+JTEXT_BELOW);
            }
            if((tmptm.tm_mday%7)==1)
            {
                newpen(colour,PS_SOLID);
                moveto(ix,PltCtl->iymin+6);
                lineto(ix,PltCtl->iymin-16);
                sprintf(tmptxt,"%2d/%2d",tmptm.tm_mon+1,tmptm.tm_mday);
                drawtext(ix+2,PltCtl->iymin-9, tmptxt, JTEXT_LEFT+JTEXT_BELOW);
            }
#if EASYWINDEBUG & 0
printf("%d %5.1f %d %ld\n",ix,x,tmptm.tm_mday,indexl);
#endif
            //x += xstep;
        }while((ix < PltCtl->ixmax));
    }

    if(ystep >0)
    {
        newpen(colour,PS_DOT);
        x = PltCtl->ymin;
        while(x < PltCtl->ymax)
        {
            GetPlotPoints(0,x,NULL,&ix,PltCtl);
            moveto(PltCtl->ixmin,ix);
            lineto(PltCtl->ixmax,ix);
            x += ystep;
        }
        x = PltCtl->ymin;
        while(x < PltCtl->ymax)
        {
            GetPlotPoints(0,x,NULL,&ix,PltCtl);
            sprintf(tmptxt,"%-5.0f",x);
            drawtext(PltCtl->ixmin-2, ix, tmptxt, JTEXT_RIGHT+JTEXT_MID);
            x += ystep;
        }
    }

    return(0);
}


/*
**************************************************************************
*
*  Function:
*  Purpose: Filter coefficient tables
*
*  Comments:
*   Read from the last table back upwards. We enter the source this
*   way so we can skip prototyping
*
**************************************************************************
*/

t_selection SelectWindow=
{
   &ConstWeight,                  // selection variable
    {"None",
"E48 (.9905)",
"EXP .95",
"EXP .8",
"BUT 8 48",
"BUT 8 24",
"BUT 8 12",
"7 bpf1 .9@48",
"8 EXP .95",
"Test",
    0}
};

t_selection SelectUrine=
{
   &ConstUrine,                  // selection variable
    {"None",
"E48 (.9905)",
"EXP 0.95",
"EXP 0.8",
"BUT 8 48",
"BUT 8 24",
"BUT 8 12",
"7 EXP .95",
"8 EXP .95",
"Test",
    0}
};

// these look wrong, generally too high

const int SmoothDelays[]={
0,  //"None",
48,  //"E48 .9905",
16,  //"EXP 0.95",
3,  //"EXP 0.8",
43, //"BUT 8 24",
22, //"BUT 8 12",
87,  //"BUT 8 48",
4,  //"EXP .95",
0,  //"EXP .95",
0   //"Test"
};

const double IIRTaps[] = {
//0 None: 0 Full Band filter
   1.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
// B
   1.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,

//1 EXP : 2 One Pole 0.9905
// A
   1.00000000000000,
  -0.9905,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
// B
   0.0095,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,

//2 EXP .95 : 1 One Pole 0.95
// A
   1.0,
  -0.95,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
// B
   0.05,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,

//3 EXP : 2 One Pole 0.8
// A
   1.00000000000000,
  -0.8,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
// B
   0.2,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,


//4 BUT 8 12 : 4 8 pole 1/48 BW -17 ISI dly 22
//  87.00000000000000  -6.34730672468488
   1.00000000000000,
  -7.66452154825225,
  25.70767150133034,
 -49.28503600563748,
  59.06881977745168,
 -45.32006992257542,
  21.73753210798133,
  -5.95933167657650,
   0.71493576656382,
   0.01115330050538e-010,
   0.08919975869048e-010,
   0.31239011377693e-010,
   0.62428284763882e-010,
   0.78102857514750e-010,
   0.62428284763882e-010,
   0.31246116805050e-010,
   0.08916423155370e-010,
   0.01116218228958e-010,

//5 BUT 8 24 : 3 8 pole 1/24 BW -11 ISI dly 45
//   8.00000000000000  24.00000000000000
//  45.00000000000000 -10.63511683517843
// A
   1.00000000000000,
  -7.32908131692269,
  23.52661947453176,
 -43.20135634302372,
  49.63245808844130,
 -36.53005173755608,
  16.82044226055678,
  -4.42992488959204,
   0.51089452588660,
   0.00243444930881e-007,
   0.01947558558868e-007,
   0.06816456732395e-007,
   0.13632927675644e-007,
   0.17041131172846e-007,
   0.13632927675644e-007,
   0.06816446074254e-007,
   0.01947560335225e-007,
   0.00243444708836e-007,

//6 BUT 8 12 : 4 8 pole 1/12 BW -17 ISI dly 22
// A
//  22.00000000000000 -16.92560514058966
   1.00000000000000,
  -6.65846380940733,
  19.49304977920082,
 -32.75991964645707,
  34.55772213555994,
 -23.42437168408122,
   9.96108958427228,
  -2.42912996327182,
   0.26003538537036,
   0.00460202576047e-005,
   0.03681620635021e-005,
   0.12885672049379e-005,
   0.25771344454029e-005,
   0.32214180123447e-005,
   0.25771344454029e-005,
   0.12885672049379e-005,
   0.03681620635021e-005,
   0.00460202573827e-005,


//7 EXP : 7 One Pole 0.95 angle 24 See tm.m
// A
   1.00000000000000,
  -1.94323,
   0.96040,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
// B
   0.017168,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,

//8 EXP : 8 One Pole 0.95
// A
   1.00000000000000,
  -0.95,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
// B
   0.05,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,

//9 Test
// A
   1.00000000000000,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
// B
   1.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,
   0.0,

   0    // end
};
