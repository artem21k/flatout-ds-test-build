/* ============================================================================
   maps.h  -  everything BESIDE THE ROAD for the two "roadside" race tracks:
       RALLY FOREST (mapId 2, trackDefs[0])   and   HARVEST (mapId 4, trackDefs[2]).
   The lap layouts themselves (control points) are in trackDefs[] in main.c, and so are CANYON and the
   two derby arenas; this file adds walls, fences, props, trees and wall collisions to Forest and Harvest.
   fmTrk selects the track: 0 = FOREST, 1 = HARVEST.

   Included by main.c right after drawArena().  Everything is built ONCE in
   fmBuild() as small textured quads with integer vertices, grouped by texture and
   by road segment, so drawing only touches the ~20 road segments around the
   player (short draw distance on purpose).  No floating point at draw time.

   Where things are is controlled by tables / functions you can edit (one set per track):
     fmZones0[] / fmZones1[]            what stands left / right of the road (by lap position 0..1)
     fmBuildProps() / fmBuildProps1()   signs, cones, towers, stands, bridge, silos, barns ...
     fmTrees()                          tree density and tint
   Lap position s: 0.0 = start line, goes round clockwise on the minimap.
   Left/right are seen by the driver.
   ============================================================================ */
#include "forestatlas.h"

#define FM_QMAX   4600
#define FM_FTMAX  2600
#define FM_WBACK  5            /* draw this many road segments behind the camera ... */
#define FM_WFWD   12           /* ... and this many in front (1 segment is ~4.6 m)   */

enum { FG_ATLAS, FG_GRASS, FG_FLAT, FG_TIRES, FG_N };
enum { FT_NONE, FT_CONC, FT_ROCKW, FT_ROCKL, FT_FWOOD, FT_FMET, FT_LOGS, FT_HILL, FT_ROCKB };
enum { FL_ROCK, FL_CONC, FL_MET, FL_WOOD, FL_LED, FL_CHEV, FL_CONE, FL_LOG };

static const short fmTile[8][4] = {        /* x,y,w,h in the 64x64 atlas (see gen_forest_atlas.py) */
    {0,0,32,32}, {32,0,32,16}, {32,16,32,16}, {0,32,32,16},
    {32,32,32,16}, {0,48,16,16}, {16,48,16,16}, {32,48,32,16}
};

typedef struct { int tx,ty,tz; short v[12]; short u0,u1,vb,vt; unsigned char r,g,b; } FQ;
typedef struct { int tx,ty,tz,sc; unsigned char var; } FTree;

static FQ fq[FM_QMAX];
static unsigned char fqG[FM_QMAX];
static short fqS[FM_QMAX], fmOrd[FM_QMAX], fmGS[FG_N][MAXPTS+1], fmPos[FG_N][MAXPTS+1];
static int nfq;
static FTree ft[FM_FTMAX];
static short ftS[FM_FTMAX], ftOrd[FM_FTMAX], ftGS[MAXPTS+2], ftPos[MAXPTS+2];
static int nft;
static int fmTexId=-1;
static float fmAcc[MAXPTS+1];
static unsigned char fmBar[2][MAXPTS], fmBack[2][MAXPTS], fmHard[2][MAXPTS];   /* [0]=left [1]=right */
static float fmSolid[2][MAXPTS];
static unsigned int fmSeed;

/* ---------- HARVEST ground classes (painted on the map by the author: forest / field / river), 16 m cells ---------- */
#define FMG_COLS 50
#define FMG_ROWS 42
#define FMG_CELL 16.0f
#define FMG_X0   (-400.0f)
#define FMG_Z0   (-340.0f)
static const unsigned char fmGrid[FMG_ROWS*FMG_COLS] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,3,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
};
static unsigned char fmGridRT[FMG_ROWS*FMG_COLS];          /* the grid + the yard, filled by fmBuild */
/* 1 = forest floor (trees), 2 = field (yellow, slows you down), 3 = river (blocks, slows a lot), 4 = open yard (like road) */
static int fmGround(float x,float z)
{
    int c=(int)floorf((x-FMG_X0)/FMG_CELL), r=(int)floorf((z-FMG_Z0)/FMG_CELL);
    if(c<0) c=0; else if(c>=FMG_COLS) c=FMG_COLS-1;
    if(r<0) r=0; else if(r>=FMG_ROWS) r=FMG_ROWS-1;
    return fmGridRT[r*FMG_COLS+c];
}

/* where the fences stand: 9.2 m from the road centre, further out around the open yard */
static float fmFO(int i)
{
    float w=fmTrk?yardHW((float)i/(float)npts):HALFW;
    return (w>HALFW+0.01f)?(w+2.0f>9.2f?w+2.0f:9.2f):9.2f;
}

/* small jump beside the covered bridge (HARVEST): rises along the road, drops at the end */
static int fmRampOn=0;
static float fmRampX, fmRampZ, fmRampFx, fmRampFz, fmRampNx, fmRampNz, fmRampL;
static float fmRampH(float x,float z)
{
    float dx,dz,u,v;
    if(!fmRampOn) return 0.0f;
    dx=x-fmRampX; dz=z-fmRampZ;
    u=dx*fmRampFx+dz*fmRampFz; v=dx*fmRampNx+dz*fmRampNz;
    if(u<0.0f || u>fmRampL || v<-2.0f || v>2.0f) return 0.0f;
    return 1.15f*u/fmRampL;
}

/* ---------- breakable roadside: fence sections, cones and signs vanish when hit and give nitro ---------- */
#define FM_BRKMAX (2*MAXPTS+520)       /* ids 1..2*MAXPTS = fence section (side,segment); then one id per sign/cone */
#define FM_NSIGN  512
static unsigned short fqB[FM_QMAX];
static unsigned char fmBroken[FM_BRKMAX];
static int fmBk=0;                     /* id given to the quads added right now (0 = not breakable) */
static int fmNSign;
static float fsX[FM_NSIGN], fsZ[FM_NSIGN];
static short fsS[FM_NSIGN];
static unsigned char fsNit[FM_NSIGN];

static unsigned int fmRnd(void){ fmSeed=fmSeed*1664525u+1013904223u; return fmSeed>>16; }
static float fmRf(void){ return (float)(fmRnd()&1023)/1024.0f; }
static int fmH(int i,int k){ unsigned int h=(unsigned int)(i*2654435761u+k*40503u); h^=h>>13; h*=2246822519u; return (int)((h>>16)&255); }

/* ---------- lap-position table: [a,b) -> barrier + backdrop on each side ------------ */
typedef struct { float a,b; unsigned char bl,kl,br,kr; } FZone;
static int fmTrk=0;            /* 0 = RALLY FOREST, 1 = HARVEST (set by buildScenery before fmBuild) */
static const FZone fmZones0[] = {
    /*  from   to    L bar      L back     R bar      R back  */
    { 0.000f,0.150f, FT_NONE ,  FT_HILL ,  FT_FMET ,  FT_NONE  },   /* start straight, metal fence, rocky hill left */
    { 0.150f,0.240f, FT_NONE ,  FT_NONE ,  FT_NONE ,  FT_NONE  },   /* open pines down the right edge             */
    { 0.240f,0.322f, FT_ROCKW,  FT_NONE ,  FT_ROCKW,  FT_NONE  },   /* GORGE with the bridge                      */
    { 0.322f,0.340f, FT_ROCKL,  FT_NONE ,  FT_ROCKL,  FT_NONE  },
    { 0.340f,0.445f, FT_CONC ,  FT_NONE ,  FT_ROCKL,  FT_ROCKB },   /* concrete wall left, cliff right            */
    { 0.445f,0.500f, FT_CONC ,  FT_NONE ,  FT_FWOOD,  FT_HILL  },
    { 0.500f,0.535f, FT_CONC ,  FT_NONE ,  FT_NONE ,  FT_ROCKB },
    { 0.535f,0.590f, FT_ROCKW,  FT_NONE ,  FT_ROCKW,  FT_NONE  },   /* second gorge with the ROCK ARCH            */
    { 0.590f,0.640f, FT_CONC ,  FT_NONE ,  FT_FWOOD,  FT_HILL  },
    { 0.640f,0.700f, FT_ROCKL,  FT_ROCKB,  FT_FWOOD,  FT_NONE  },
    { 0.700f,0.750f, FT_LOGS ,  FT_NONE ,  FT_FWOOD,  FT_HILL  },   /* sharp bend: log barrier, big grass hill    */
    { 0.750f,0.800f, FT_FWOOD,  FT_ROCKB,  FT_NONE ,  FT_HILL  },
    { 0.800f,0.880f, FT_FWOOD,  FT_NONE ,  FT_NONE ,  FT_NONE  },
    { 0.880f,0.960f, FT_FWOOD,  FT_NONE ,  FT_FWOOD,  FT_NONE  },
    { 0.960f,1.001f, FT_NONE ,  FT_NONE ,  FT_NONE ,  FT_NONE  }
};
#define FM_NZONES0 (int)(sizeof(fmZones0)/sizeof(fmZones0[0]))

/* HARVEST: autumn farm road.  Lap position from the video of a lap: open yard with a pipe bridge at the start, fences along the fields,
   a long right-hand sweeper (0.21-0.38), a left bend (0.50-0.58), a right hairpin (0.62-0.70), then the grain yard on the left edge of the map. */
static const FZone fmZones1[] = {
    /*  from   to    L bar      L back     R bar      R back  */
    { 0.000f,0.060f, FT_NONE ,  FT_NONE ,  FT_NONE ,  FT_NONE  },   /* start yard                                  */
    { 0.060f,0.200f, FT_NONE ,  FT_NONE ,  FT_FWOOD,  FT_NONE  },   /* straight, post fence on the right           */
    { 0.200f,0.300f, FT_FWOOD,  FT_NONE ,  FT_NONE ,  FT_NONE  },   /* right-hand sweeper, wire fence on the outside */
    { 0.300f,0.340f, FT_NONE ,  FT_NONE ,  FT_NONE ,  FT_NONE  },
    { 0.340f,0.420f, FT_FWOOD,  FT_NONE ,  FT_NONE ,  FT_NONE  },   /* fields                                      */
    { 0.420f,0.470f, FT_FWOOD,  FT_NONE ,  FT_FWOOD,  FT_NONE  },   /* barn                                        */
    { 0.470f,0.530f, FT_NONE ,  FT_NONE ,  FT_NONE ,  FT_NONE  },   /* left bend                                   */
    { 0.530f,0.640f, FT_FWOOD,  FT_NONE ,  FT_NONE ,  FT_HILL  },   /* fence left, earth bank right                */
    { 0.640f,0.740f, FT_FWOOD,  FT_NONE ,  FT_FWOOD,  FT_NONE  },   /* right hairpin                               */
    { 0.740f,0.780f, FT_NONE ,  FT_NONE ,  FT_NONE ,  FT_NONE  },
    { 0.780f,0.985f, FT_FMET ,  FT_NONE ,  FT_FMET ,  FT_NONE  },   /* grain yard: wire-mesh fences both sides     */
    { 0.985f,1.001f, FT_NONE ,  FT_NONE ,  FT_NONE ,  FT_NONE  }
};
#define FM_NZONES1 (int)(sizeof(fmZones1)/sizeof(fmZones1[0]))
static const FZone *fmZ=fmZones0;
static int fmNZ=FM_NZONES0;

/* ---------------------------------- geometry helpers --------------------------- */
static float fmVk(float off){ return off<=VERGE0?1.0f:(off>=VERGE1?0.0f:(VERGE1-off)/(VERGE1-VERGE0)); }
static float fmGH(int i,float off){ return roadH[i]*fmVk(off); }               /* ground height beside road point i */
static void fmP(float *o,int i,int sd,float off,float y){ o[0]=roadX[i]+nrmX[i]*(float)sd*off; o[1]=y+fmGH(i,off); o[2]=roadZ[i]+nrmZ[i]*(float)sd*off; }

static void fmUV(int tile,int flip,int *u0,int *u1,int *vb,int *vt)
{
    const short *t=fmTile[tile];
    int a=t[0]*16+8, b=(t[0]+t[2])*16-8;
    if(flip){ int x=a; a=b; b=x; }
    *u0=a; *u1=b; *vt=t[1]*16+8; *vb=(t[1]+t[3])*16-8;
}

static void fmAddOne(int g,int si,float c[4][3],int u0,int u1,int vb,int vt,int r,int gg,int b)
{
    FQ *q; float cx,cy,cz; int k;
    if(nfq>=FM_QMAX) return;
    cx=0.25f*(c[0][0]+c[1][0]+c[2][0]+c[3][0]); cy=0.25f*(c[0][1]+c[1][1]+c[2][1]+c[3][1]); cz=0.25f*(c[0][2]+c[1][2]+c[2][2]+c[3][2]);
    if(fmTrk && g==FG_GRASS){                                    /* HARVEST is autumn: grass quads take the colour of the ground they lie on */
        if(fmGround(cx,cz)==2){ gg=gg*252/255; b=b*120/255; } else { r=r*255/255; gg=gg*222/255; b=b*92/255; }
    }
    q=&fq[nfq]; fqG[nfq]=(unsigned char)g; fqS[nfq]=(short)si; fqB[nfq]=(unsigned short)fmBk; nfq++;
    q->tx=floattof32(cx); q->ty=floattof32(cy); q->tz=floattof32(cz);
    for(k=0;k<4;k++){ q->v[k*3]=(short)cl16(c[k][0]-cx); q->v[k*3+1]=(short)cl16(c[k][1]-cy); q->v[k*3+2]=(short)cl16(c[k][2]-cz); }
    q->u0=(short)u0; q->u1=(short)u1; q->vb=(short)vb; q->vt=(short)vt;
    q->r=(unsigned char)r; q->g=(unsigned char)gg; q->b=(unsigned char)b;
}

/* >0 while rock walls / hills / the grass skirt are built: every piece is made this fraction larger on all four sides, so neighbouring
   pieces overlap a little (same texture, same plane).  Without it the hairline gaps at the seams (the pieces are cut differently on
   each side of a seam) show the ground behind as sparkling slits in the rock. */
static float fmPad=0.0f;

/* c = bottom-left, bottom-right, top-right, top-left.  Big quads are cut into pieces
   of at most ~11 m so the 16-bit vertex offsets never overflow. */
static void fmAdd(int g,int si,float c[4][3],int u0,int u1,int vb,int vt,int r,int gg,int b)
{
    float d01=sqrtf((c[1][0]-c[0][0])*(c[1][0]-c[0][0])+(c[1][1]-c[0][1])*(c[1][1]-c[0][1])+(c[1][2]-c[0][2])*(c[1][2]-c[0][2]));
    float d12=sqrtf((c[2][0]-c[1][0])*(c[2][0]-c[1][0])+(c[2][1]-c[1][1])*(c[2][1]-c[1][1])+(c[2][2]-c[1][2])*(c[2][2]-c[1][2]));
    int nu=(int)(d01/10.0f)+1, nv=(int)(d12/10.0f)+1, iu, iv, a;
    if(nu==1 && nv==1 && fmPad<=0.0f){ fmAddOne(g,si,c,u0,u1,vb,vt,r,gg,b); return; }
    for(iv=0;iv<nv;iv++) for(iu=0;iu<nu;iu++){
        float s0=(float)iu/(float)nu, s1=(float)(iu+1)/(float)nu, t0=(float)iv/(float)nv, t1=(float)(iv+1)/(float)nv;
        float d[4][3], s[4], t[4]; int k;
        s[0]=s0-fmPad; t[0]=t0-fmPad; s[1]=s1+fmPad; t[1]=t0-fmPad; s[2]=s1+fmPad; t[2]=t1+fmPad; s[3]=s0-fmPad; t[3]=t1+fmPad;   /* texture coordinates below stay unpadded */
        for(k=0;k<4;k++) for(a=0;a<3;a++){
            float lo=c[0][a]+(c[1][a]-c[0][a])*s[k], hi=c[3][a]+(c[2][a]-c[3][a])*s[k];
            d[k][a]=lo+(hi-lo)*t[k];
        }
        fmAddOne(g,si,d,u0+(int)((float)(u1-u0)*s0),u0+(int)((float)(u1-u0)*s1),
                 vb+(int)((float)(vt-vb)*t0),vb+(int)((float)(vt-vb)*t1),r,gg,b);
    }
}

/* flat-colour quad (uses the 8x8 white texture) */
static void fmFlat(int si,float c[4][3],int r,int g,int b){ fmAdd(FG_FLAT,si,c,0,64,64,0,r,g,b); }

/* ---------------------------------- barrier / backdrop per segment ------------- */
static float fmHeightR(int i,float base,float amp){ return base+amp*(float)fmH(i,3)/255.0f; }

static void fmVerge(int i,int sx)       /* grass skirt that joins a raised / lowered road to the flat ground */
{
    int j=(i+1)%npts, sd=sx?1:-1, g0=(int)(fmAcc[i]*128.0f)%1024, g1=g0+(int)((fmAcc[i+1]-fmAcc[i])*128.0f);
    float c[4][3];
    if(fabsf(roadH[i])<0.02f && fabsf(roadH[j])<0.02f) return;
    fmP(c[0],i,sd,VERGE0,0.0f); fmP(c[1],j,sd,VERGE0,0.0f); fmP(c[2],j,sd,VERGE1,0.0f); fmP(c[3],i,sd,VERGE1,0.0f);
    fmPad=0.012f; fmAdd(FG_GRASS,i,c,g0,g1,0,1100,255,255,255); fmPad=0.0f;
}

static void fmSegQuads(int i,int sx,int type)
{
    int j=(i+1)%npts, sd=sx?1:-1, fl=fmH(i,1)&1, u0,u1,vb,vt, sh=225+(fmH(i,2)&30);
    float c[4][3];
    fmPad=(type==FT_ROCKW||type==FT_ROCKL||type==FT_ROCKB||type==FT_HILL)?0.012f:0.0f;
    switch(type){
    case FT_CONC:
        fmP(c[0],i,sd,8.0f,0.0f); fmP(c[1],j,sd,8.0f,0.0f); fmP(c[2],j,sd,8.0f,1.5f); fmP(c[3],i,sd,8.0f,1.5f);
        fmUV(FL_CONC,fl,&u0,&u1,&vb,&vt); fmAdd(FG_ATLAS,i,c,u0,u1,vb,vt,sh,sh,sh);
        fmP(c[0],i,sd,8.0f,1.5f); fmP(c[1],j,sd,8.0f,1.5f); fmP(c[2],j,sd,8.7f,1.5f); fmP(c[3],i,sd,8.7f,1.5f);
        fmAdd(FG_ATLAS,i,c,u0,u1,vt+24,vt+24,200,200,200);
        break;
    case FT_ROCKW: {
        float hi=fmHeightR(i,6.0f,4.0f), hj=fmHeightR(j,6.0f,4.0f);
        fmP(c[0],i,sd,8.2f,0.0f); fmP(c[1],j,sd,8.2f,0.0f); fmP(c[2],j,sd,9.8f,hj); fmP(c[3],i,sd,9.8f,hi);
        fmUV(FL_ROCK,fl,&u0,&u1,&vb,&vt); fmAdd(FG_ATLAS,i,c,u0,u1,vb,vt,sh,sh,sh);
        break; }
    case FT_ROCKL: {
        float hi=fmHeightR(i,2.4f,2.0f), hj=fmHeightR(j,2.4f,2.0f);
        fmP(c[0],i,sd,8.4f,0.0f); fmP(c[1],j,sd,8.4f,0.0f); fmP(c[2],j,sd,9.6f,hj); fmP(c[3],i,sd,9.6f,hi);
        fmUV(FL_ROCK,fl,&u0,&u1,&vb,&vt); fmAdd(FG_ATLAS,i,c,u0,u1,vb,vt,sh,sh,sh);
        break; }
    case FT_FWOOD:
        fmP(c[0],i,sd,fmFO(i),0.0f); fmP(c[1],j,sd,fmFO(j),0.0f); fmP(c[2],j,sd,fmFO(j),1.3f); fmP(c[3],i,sd,fmFO(i),1.3f);
        fmUV(FL_WOOD,0,&u0,&u1,&vb,&vt); fmBk=1+(sx?MAXPTS:0)+i; fmAdd(FG_ATLAS,i,c,u0,u1,vb,vt,255,255,255); fmBk=0;
        break;
    case FT_FMET:
        fmP(c[0],i,sd,fmFO(i),0.0f); fmP(c[1],j,sd,fmFO(j),0.0f); fmP(c[2],j,sd,fmFO(j),2.4f); fmP(c[3],i,sd,fmFO(i),2.4f);
        fmUV(FL_MET,0,&u0,&u1,&vb,&vt); fmBk=1+(sx?MAXPTS:0)+i; fmAdd(FG_ATLAS,i,c,u0,u1,vb,vt,255,255,255); fmBk=0;
        break;
    case FT_LOGS:
        fmP(c[0],i,sd,9.0f,0.1f); fmP(c[1],j,sd,9.0f,0.1f); fmP(c[2],j,sd,9.0f,1.7f); fmP(c[3],i,sd,9.0f,1.7f);
        fmUV(FL_LOG,fl,&u0,&u1,&vb,&vt); fmAdd(FG_ATLAS,i,c,u0,u1,vb,vt,255,255,255);
        break;
    case FT_HILL: {
        int g0=(int)(fmAcc[i]*128.0f)%1024, g1=g0+(int)((fmAcc[i+1]-fmAcc[i])*128.0f);
        fmP(c[0],i,sd,11.5f,0.0f); fmP(c[1],j,sd,11.5f,0.0f); fmP(c[2],j,sd,24.0f,9.0f); fmP(c[3],i,sd,24.0f,9.0f);
        fmAdd(FG_GRASS,i,c,g0,g1,0,1600,206,226,176);
        fmP(c[0],i,sd,24.0f,9.0f); fmP(c[1],j,sd,24.0f,9.0f); fmP(c[2],j,sd,37.0f,9.0f); fmP(c[3],i,sd,37.0f,9.0f);
        fmAdd(FG_GRASS,i,c,g0,g1,1600,3200,190,212,160);
        break; }
    case FT_ROCKB: {
        int g0=(int)(fmAcc[i]*128.0f)%1024, g1=g0+(int)((fmAcc[i+1]-fmAcc[i])*128.0f);
        fmP(c[0],i,sd,11.0f,0.0f); fmP(c[1],j,sd,11.0f,0.0f); fmP(c[2],j,sd,15.0f,8.0f); fmP(c[3],i,sd,15.0f,8.0f);
        fmUV(FL_ROCK,fl,&u0,&u1,&vb,&vt); fmAdd(FG_ATLAS,i,c,u0,u1,vb,vt,sh-25,sh-25,sh-25);
        fmP(c[0],i,sd,15.0f,8.0f); fmP(c[1],j,sd,15.0f,8.0f); fmP(c[2],j,sd,27.0f,8.0f); fmP(c[3],i,sd,27.0f,8.0f);
        fmAdd(FG_GRASS,i,c,g0,g1,0,1536,190,212,160);
        break; }
    default: break;
    }
    fmPad=0.0f;
}

static float fmSolidOff(int type,int *hard)
{
    *hard=1;
    switch(type){
    case FT_CONC: return 8.0f;
    case FT_ROCKW: return 8.2f;
    case FT_ROCKL: return 8.4f;
    case FT_HILL: *hard=0; return 11.5f;
    case FT_ROCKB: *hard=0; return 11.0f;
    default: return 0.0f;
    }
}
static float fmBackExtent(int type){ return type==FT_HILL?37.0f:(type==FT_ROCKB?27.0f:0.0f); }

static float fmBackY(int type,float off)
{
    if(type==FT_HILL){ if(off<11.5f) return 0.0f; if(off<24.0f) return (off-11.5f)/12.5f*9.0f; return 9.0f; }
    if(type==FT_ROCKB){ if(off<11.0f) return 0.0f; if(off<15.0f) return (off-11.0f)/4.0f*8.0f; return 8.0f; }
    return 0.0f;
}

/* ---------------------------------- props ---------------------------------------- */
static int fmIdx(float s){ int i=(int)(s*(float)npts+0.5f); return ((i%npts)+npts)%npts; }

/* a quad standing across the road direction (facing the driver); w = half width */
static void fmFace(int g,int i,int sd,float off,float fo,float w,float y0,float y1,int tile,int flip,int r,int gg,int b)
{
    float c[4][3], p[3], fx=nrmZ[i], fz=-nrmX[i], nx=nrmX[i], nz=nrmZ[i]; int u0=0,u1=64,vb=64,vt=0;
    fmP(p,i,sd,off,0.0f); p[0]+=fx*fo; p[2]+=fz*fo;
    c[0][0]=p[0]-nx*w; c[0][1]=y0+p[1]; c[0][2]=p[2]-nz*w;
    c[1][0]=p[0]+nx*w; c[1][1]=y0+p[1]; c[1][2]=p[2]+nz*w;
    c[2][0]=p[0]+nx*w; c[2][1]=y1+p[1]; c[2][2]=p[2]+nz*w;
    c[3][0]=p[0]-nx*w; c[3][1]=y1+p[1]; c[3][2]=p[2]-nz*w;
    if(g==FG_ATLAS) fmUV(tile,flip,&u0,&u1,&vb,&vt);
    else if(g==FG_TIRES){ u0=0; u1=512; vb=512; vt=0; }
    fmAdd(g,i,c,u0,u1,vb,vt,r,gg,b);
}

static void fmSign(int i,int sd,float off,int tile,int flip,float w,float y0,float y1)
{
    int keep=fmBk;
    if(fmNSign<FM_NSIGN){                                        /* every sign and cone can be knocked down for a little nitro */
        fsX[fmNSign]=roadX[i]+nrmX[i]*(float)sd*off; fsZ[fmNSign]=roadZ[i]+nrmZ[i]*(float)sd*off;
        fsS[fmNSign]=(short)i; fsNit[fmNSign]=(unsigned char)(tile==FL_CONE?2:3);
        fmBk=2*MAXPTS+1+fmNSign; fmNSign++;
    }
    fmFace(FG_ATLAS,i,sd,off,0.0f,w,y0,y1,tile,flip,255,255,255);
    if(y0>0.2f) fmFace(FG_FLAT,i,sd,off,0.02f,0.07f,0.0f,y0,0,0,92,92,98);
    fmBk=keep;
}

/* oriented box: centre fo metres along the road and no metres to the right of road point i */
#define FM_F  1
#define FM_B  2
#define FM_L  4
#define FM_R  8
#define FM_T  16
#define FM_BOT 32
static void fmBox(int g,int i,float fo,float no,float a,float b,float y0,float y1,int mask,int tile,int r,int gg,int bb)
{
    float fx=nrmZ[i], fz=-nrmX[i], nx=nrmX[i], nz=nrmZ[i];
    float cx=roadX[i]+fx*fo+nx*no, cz=roadZ[i]+fz*fo+nz*no, c[4][3], gb=fmGH(i,fabsf(no));
    static const signed char F[6][4][3]={            /* sf,sn,level(0=y0,1=y1) in u/v order BL,BR,TR,TL */
        {{ 1,-1,0},{ 1, 1,0},{ 1, 1,1},{ 1,-1,1}},    /* front  */
        {{-1, 1,0},{-1,-1,0},{-1,-1,1},{-1, 1,1}},    /* back   */
        {{-1,-1,0},{ 1,-1,0},{ 1,-1,1},{-1,-1,1}},    /* left   */
        {{ 1, 1,0},{-1, 1,0},{-1, 1,1},{ 1, 1,1}},    /* right  */
        {{-1,-1,1},{-1, 1,1},{ 1, 1,1},{ 1,-1,1}},    /* top    */
        {{-1,-1,0},{-1, 1,0},{ 1, 1,0},{ 1,-1,0}}     /* bottom */
    };
    static const unsigned char shade[6]={235,200,215,175,255,120};
    int f,k,u0=0,u1=64,vb=64,vt=0;
    if(g==FG_ATLAS) fmUV(tile,0,&u0,&u1,&vb,&vt);
    else if(g==FG_TIRES){ u0=0; u1=512; vb=512; vt=0; }
    for(f=0;f<6;f++){
        if(!(mask&(1<<f))) continue;
        for(k=0;k<4;k++){
            c[k][0]=cx+fx*a*(float)F[f][k][0]+nx*b*(float)F[f][k][1];
            c[k][2]=cz+fz*a*(float)F[f][k][0]+nz*b*(float)F[f][k][1];
            c[k][1]=(F[f][k][2]?y1:y0)+gb;
        }
        fmAdd(g,i,c,u0,u1,vb,vt,r*shade[f]/255,gg*shade[f]/255,bb*shade[f]/255);
    }
}

static void fmCones(float s0,float s1,int sd,float off,float step)
{
    float s; for(s=s0;s<s1;s+=step) fmSign(fmIdx(s),sd,off,FL_CONE,0,0.38f,0.0f,0.85f);
}

/* advertising tower: two posts and two panels (generic art - no real brands) */
static void fmTower(int i,int sd,float off,int var)
{
    static const unsigned char pc[3][3]={{30,30,34},{176,38,30},{34,72,150}};
    int k=var%3;
    fmFace(FG_FLAT,i,sd,off,0.0f,0.16f,0.0f,7.0f,0,0,86,88,96);          /* post (centre) */
    fmFace(FG_FLAT,i,sd,off,-0.02f,3.1f,4.5f,7.1f,0,0,46,48,54);          /* top panel frame */
    fmFace(FG_FLAT,i,sd,off,-0.06f,2.9f,4.7f,6.9f,0,0,238,228,198);       /* cream poster */
    fmFace(FG_FLAT,i,sd,off,-0.10f,1.7f,5.0f,6.5f,0,0,188,126,72);        /* picture */
    fmFace(FG_FLAT,i,sd,off,-0.02f,3.1f,2.9f,4.4f,0,0,46,48,54);          /* lower panel frame */
    fmFace(FG_FLAT,i,sd,off,-0.06f,2.9f,3.05f,4.25f,0,0,pc[k][0],pc[k][1],pc[k][2]);
    fmFace(FG_FLAT,i,sd,off,-0.10f,2.4f,3.5f,3.8f,0,0,248,206,26);        /* stripe */
}

static void fmPlank(int i,int sd,float off,float ang,float len)
{
    float p[3], c[4][3], fx=nrmZ[i], fz=-nrmX[i], nx=nrmX[i], nz=nrmZ[i], ca=cosf(ang), sa=sinf(ang);
    float dx=fx*ca+nx*sa, dz=fz*ca+nz*sa, ex=-fx*sa+nx*ca, ez=-fz*sa+nz*ca; int u0,u1,vb,vt;
    fmP(p,i,sd,off,0.05f);
    c[0][0]=p[0]-dx*len-ex*0.15f; c[0][1]=p[1]; c[0][2]=p[2]-dz*len-ez*0.15f;
    c[1][0]=p[0]+dx*len-ex*0.15f; c[1][1]=p[1]; c[1][2]=p[2]+dz*len-ez*0.15f;
    c[2][0]=p[0]+dx*len+ex*0.15f; c[2][1]=p[1]; c[2][2]=p[2]+dz*len+ez*0.15f;
    c[3][0]=p[0]-dx*len+ex*0.15f; c[3][1]=p[1]; c[3][2]=p[2]-dz*len+ez*0.15f;
    fmUV(FL_LOG,0,&u0,&u1,&vb,&vt); fmAdd(FG_ATLAS,i,c,u0,u1,vb,vt,255,255,255);
}

/* grain silo: riveted concrete block with a darker band and cap */
static void fmSilo(float s,int sd,float off,float r,float h)
{
    int i=fmIdx(s); float no=(float)sd*off;
    fmBox(FG_ATLAS,i,0.0f,no,r,r,0.0f,h,FM_F|FM_B|FM_L|FM_R|FM_T,FL_CONC,205,200,188);
    fmBox(FG_FLAT,i,0.0f,no,r*1.04f,r*1.04f,h*0.60f,h*0.65f,FM_F|FM_B|FM_L|FM_R,-1,126,128,122);
    fmBox(FG_FLAT,i,0.0f,no,r*1.07f,r*1.07f,h,h+0.5f,FM_F|FM_B|FM_L|FM_R|FM_T,-1,118,120,114);
}

/* barn / shed: textured walls (planks or sheet metal), gable roof along the road, dark door on the end wall facing the driver.
   tile = FL_LOG (wooden planks) or FL_CONC (sheet metal); wr,wg,wb tint the tile */
static void fmBarnB(float s,int sd,float off,float hl,float hw,int tile,int wr,int wg,int wb,int rr,int rg,int rb)
{
    int i=fmIdx(s); float c[4][3], fx=nrmZ[i], fz=-nrmX[i], nx=nrmX[i]*(float)sd, nz=nrmZ[i]*(float)sd;
    float cx=roadX[i]+nx*off, cz=roadZ[i]+nz*off, wh=4.6f, rh=7.0f, gb=fmGH(i,off);
    int ar=(tile==FL_LOG)?130:200, ag=(tile==FL_LOG)?90:196, ab=(tile==FL_LOG)?55:185;           /* average colour of the tile */
    fmBox(FG_ATLAS,i,0.0f,(float)sd*off,hl,hw,0.0f,wh,FM_F|FM_B|FM_L|FM_R,tile,wr,wg,wb);
    c[0][0]=cx-nx*hw-fx*hl; c[0][1]=wh+gb; c[0][2]=cz-nz*hw-fz*hl;      /* roof, first slope */
    c[1][0]=cx-nx*hw+fx*hl; c[1][1]=wh+gb; c[1][2]=cz-nz*hw+fz*hl;
    c[2][0]=cx+fx*hl;       c[2][1]=rh+gb; c[2][2]=cz+fz*hl;
    c[3][0]=cx-fx*hl;       c[3][1]=rh+gb; c[3][2]=cz-fz*hl;
    fmFlat(i,c,rr,rg,rb);
    c[0][0]=cx+nx*hw-fx*hl; c[0][2]=cz+nz*hw-fz*hl;                     /* second slope */
    c[1][0]=cx+nx*hw+fx*hl; c[1][2]=cz+nz*hw+fz*hl;
    fmFlat(i,c,rr*9/10,rg*9/10,rb*9/10);
    c[0][0]=cx-nx*hw+fx*hl; c[0][1]=wh+gb; c[0][2]=cz-nz*hw+fz*hl;      /* gable end facing the driver */
    c[1][0]=cx+nx*hw+fx*hl; c[1][1]=wh+gb; c[1][2]=cz+nz*hw+fz*hl;
    c[2][0]=cx+fx*hl;       c[2][1]=rh+gb; c[2][2]=cz+fz*hl;
    c[3][0]=c[2][0];        c[3][1]=c[2][1]; c[3][2]=c[2][2];
    fmFlat(i,c,wr*ar/255*9/10,wg*ag/255*9/10,wb*ab/255*9/10);
    fmFace(FG_FLAT,i,sd,off,hl+0.06f,hw*0.55f,0.0f,3.4f,0,0,34,30,28);  /* door */
}

/* flat quad on the road-side wall of a barn (windows, doors): off = distance of the wall plane from the road centre, f0..f1 along the road */
static void fmWallQ(int i,int sd,float off,float f0,float f1,float y0,float y1,int r,int g,int b)
{
    float c[4][3], fx=nrmZ[i], fz=-nrmX[i], nx=nrmX[i]*(float)sd, nz=nrmZ[i]*(float)sd, gb=fmGH(i,off);
    float px=roadX[i]+nx*off, pz=roadZ[i]+nz*off;
    c[0][0]=px+fx*f0; c[0][1]=y0+gb; c[0][2]=pz+fz*f0;
    c[1][0]=px+fx*f1; c[1][1]=y0+gb; c[1][2]=pz+fz*f1;
    c[2][0]=px+fx*f1; c[2][1]=y1+gb; c[2][2]=pz+fz*f1;
    c[3][0]=px+fx*f0; c[3][1]=y1+gb; c[3][2]=pz+fz*f0;
    fmFlat(i,c,r,g,b);
}

/* the long grey plank barn of the screenshots: stone base, windows with white frames, a red door */
static void fmBarnDress(float s,int sd,float off,float hl,float hw)
{
    int i=fmIdx(s), k; float wall=off-hw-0.16f, f;
    fmBox(FG_ATLAS,i,0.0f,(float)sd*off,hl+0.12f,hw+0.12f,0.0f,1.25f,FM_F|FM_B|FM_L|FM_R|FM_T,FL_ROCK,176,170,150);
    for(k=0;k<4;k++){
        f=-hl*0.70f+hl*0.46f*(float)k;
        fmWallQ(i,sd,wall,f-1.15f,f+1.15f,1.85f,3.55f,214,210,200);          /* frame */
        fmWallQ(i,sd,wall-0.04f,f-0.95f,f+0.95f,2.05f,3.35f,34,42,52);       /* glass */
    }
    fmWallQ(i,sd,wall,hl*0.82f-1.2f,hl*0.82f+1.2f,1.25f,3.7f,150,44,30);     /* red door */
}

/* water tower: four legs, tank, roof */
static void fmWater(float s,int sd,float off)
{
    int i=fmIdx(s); float no=(float)sd*off, ax, lz;
    for(ax=-2.0f;ax<=2.0f;ax+=4.0f) for(lz=-2.0f;lz<=2.0f;lz+=4.0f) fmBox(FG_FLAT,i,ax,no+lz,0.3f,0.3f,0.0f,11.0f,63,-1,66,62,56);
    fmBox(FG_FLAT,i,0.0f,no,2.7f,2.7f,11.0f,14.6f,63,-1,128,126,124);
    fmBox(FG_FLAT,i,0.0f,no,1.5f,1.5f,14.6f,16.0f,63,-1,92,92,96);
}

/* flatbed truck loaded with hay bales, parked beside the road */
static void fmHayTruck(float s,int sd,float off)
{
    int i=fmIdx(s); float no=(float)sd*off;
    fmBox(FG_FLAT,i,0.0f,no,3.6f,1.3f,0.9f,1.25f,63,-1,46,56,80);           /* bed */
    fmBox(FG_FLAT,i,4.5f,no,1.1f,1.3f,0.9f,2.7f,63,-1,56,92,152);           /* cab */
    fmBox(FG_FLAT,i,-0.2f,no,3.1f,1.15f,1.25f,3.5f,63,-1,198,166,78);       /* hay */
    fmBox(FG_FLAT,i,-2.3f,no,0.6f,1.4f,0.0f,0.9f,63,-1,22,22,24);           /* wheels */
    fmBox(FG_FLAT,i,1.6f,no,0.6f,1.4f,0.0f,0.9f,63,-1,22,22,24);
    fmBox(FG_FLAT,i,4.6f,no,0.6f,1.4f,0.0f,0.9f,63,-1,22,22,24);
}

static void fmBuildProps1(void)
{
    int i; float s;
    /* ---- start yard: pipe bridge across the road, silos on both sides ---- */
    i=fmIdx(0.016f);
    fmBox(FG_FLAT,i,0.0f,0.0f,0.9f,9.6f,5.6f,6.3f,63,-1,66,64,60);       /* truss beam */
    fmBox(FG_FLAT,i,0.0f,0.0f,0.5f,9.0f,4.7f,5.3f,63,-1,112,104,86);     /* pipe */
    fmBox(FG_FLAT,i,0.0f,-9.4f,0.6f,0.6f,0.0f,5.7f,63,-1,66,64,60);      /* pylons */
    fmBox(FG_FLAT,i,0.0f, 9.4f,0.6f,0.6f,0.0f,5.7f,63,-1,66,64,60);
    fmSilo(0.014f,-1,15.5f,3.6f,12.0f); fmSilo(0.014f,1,15.0f,3.4f,11.0f);
    fmSilo(0.030f,-1,19.0f,4.0f,14.0f); fmSilo(0.032f,1,21.0f,3.4f,10.0f);
    fmTower(fmIdx(0.075f),1,13.0f,2);
    fmCones(0.060f,0.095f,-1,7.0f,0.0030f);
    /* ---- right-hand sweeper: chevrons on the outside (left), cones inside, power pylon, billboard ---- */
    for(s=0.215f;s<0.372f;s+=0.012f) fmSign(fmIdx(s),-1,8.8f,FL_CHEV,0,0.7f,0.9f,2.4f);
    fmCones(0.205f,0.300f,1,7.0f,0.0035f);
    i=fmIdx(0.255f);
    fmBox(FG_FLAT,i,0.0f,-15.0f,0.45f,0.45f,0.0f,15.0f,63,-1,96,98,102);
    fmBox(FG_FLAT,i,0.0f,-15.0f,0.35f,3.6f,12.2f,12.7f,63,-1,96,98,102);
    fmFace(FG_FLAT,fmIdx(0.327f),-1,12.5f,0.0f,5.2f,2.8f,7.2f,0,0,34,36,40);       /* billboard */
    fmFace(FG_FLAT,fmIdx(0.327f),-1,12.5f,-0.05f,4.7f,3.2f,6.8f,0,0,196,188,150);
    fmFace(FG_FLAT,fmIdx(0.327f),-1,12.5f,-0.09f,3.0f,4.2f,5.8f,0,0,176,70,40);
    fmFace(FG_FLAT,fmIdx(0.327f),-1,12.5f,0.02f,0.18f,0.0f,2.8f,0,0,70,70,76);
    fmCones(0.305f,0.335f,-1,7.0f,0.0035f);
    /* ---- fields, red barn, closed-bridge sign, hay ---- */
    fmBarnB(0.432f,1,27.0f,8.0f,6.0f,FL_LOG,255,100,84,112,114,120);
    for(s=0.528f;s<0.580f;s+=0.012f) fmSign(fmIdx(s),1,8.8f,FL_CHEV,1,0.7f,0.9f,2.4f);   /* left bend after the bridge: chevrons point left, on the right */
    fmBox(FG_FLAT,fmIdx(0.452f),0.0f,11.0f,1.2f,1.0f,0.0f,1.3f,FM_F|FM_B|FM_L|FM_R|FM_T,-1,196,164,78);
    fmBox(FG_FLAT,fmIdx(0.456f),0.0f,12.5f,1.2f,1.0f,0.0f,1.3f,FM_F|FM_B|FM_L|FM_R|FM_T,-1,190,158,72);
    fmBox(FG_FLAT,fmIdx(0.454f),0.0f,11.6f,1.2f,1.0f,1.3f,2.6f,FM_F|FM_B|FM_L|FM_R|FM_T,-1,198,166,80);
    /* ---- covered wooden bridge over the river (the river is in fmGrid), with a small jump on its left, as in the lap video ---- */
    {   int bi, b0=-1, b1=-1, mid;
        for(bi=0;bi<npts;bi++) if(fmGround(roadX[bi],roadZ[bi])==3){ if(b0<0) b0=bi; b1=bi; }
        if(b0>=0){
            int e, j;
            mid=(b0+b1)/2;
            for(bi=b0;bi<=b1;bi++){
                if(bi>=mid-3 && bi<=mid+3){                                          /* the covered part: plank walls, roof, plank floor */
                    fmBox(FG_ATLAS,bi,2.67f,-6.9f,2.7f,0.35f,0.0f,4.4f,FM_L|FM_R|FM_F|FM_B,FL_LOG,150,120,90);
                    fmBox(FG_ATLAS,bi,2.67f, 6.9f,2.7f,0.35f,0.0f,4.4f,FM_L|FM_R|FM_F|FM_B,FL_LOG,150,120,90);
                    fmBox(FG_FLAT,bi,2.67f,0.0f,2.7f,7.5f,4.4f,5.0f,63,-1,86,64,44);
                    fmBox(FG_ATLAS,bi,2.67f,0.0f,2.7f,6.3f,0.0f,0.14f,FM_T,FL_LOG,205,160,110);
                } else {                                                             /* open stretch: concrete parapets */
                    fmBox(FG_ATLAS,bi,2.67f,-6.7f,2.7f,0.3f,0.0f,1.05f,FM_F|FM_B|FM_L|FM_R|FM_T,FL_CONC,196,192,182);
                    fmBox(FG_ATLAS,bi,2.67f, 6.7f,2.7f,0.3f,0.0f,1.05f,FM_F|FM_B|FM_L|FM_R|FM_T,FL_CONC,196,192,182);
                }
            }
            i=(b0+npts-8)%npts;                                                      /* ROAD CLOSED board before the bridge */
            fmFace(FG_FLAT,i,-1,9.0f,0.0f,1.4f,1.2f,2.9f,0,0,226,226,220);
            fmFace(FG_FLAT,i,-1,9.0f,-0.04f,1.2f,2.1f,2.5f,0,0,200,50,40);
            fmFace(FG_FLAT,i,-1,9.0f,0.02f,0.10f,0.0f,1.2f,0,0,90,90,96);
            fmCones(((float)((b0+npts-18)%npts))/(float)npts,((float)((b0+npts-2)%npts))/(float)npts,-1,7.0f,0.0032f);
            /* jump: a wooden ramp left of the road just after the bridge, rising along the road, dropping at the end */
            e=(b1+3)%npts; j=(e+1)%npts;
            {   float len=sqrtf(seg2[e]), fx=(roadX[j]-roadX[e])/len, fz=(roadZ[j]-roadZ[e])/len, lx=-nrmX[e], lz=-nrmZ[e];
                float ox=roadX[e]+lx*10.5f, oz=roadZ[e]+lz*10.5f, c[4][3]; int u0,u1,vb,vt;
                fmRampX=ox; fmRampZ=oz; fmRampFx=fx; fmRampFz=fz; fmRampNx=lx; fmRampNz=lz; fmRampL=len; fmRampOn=1;
                c[0][0]=ox-lx*2.0f;          c[0][1]=0.03f; c[0][2]=oz-lz*2.0f;                         /* top (planks) */
                c[1][0]=ox+lx*2.0f;          c[1][1]=0.03f; c[1][2]=oz+lz*2.0f;
                c[2][0]=ox+lx*2.0f+fx*len;   c[2][1]=1.18f; c[2][2]=oz+lz*2.0f+fz*len;
                c[3][0]=ox-lx*2.0f+fx*len;   c[3][1]=1.18f; c[3][2]=oz-lz*2.0f+fz*len;
                fmUV(FL_LOG,0,&u0,&u1,&vb,&vt); fmAdd(FG_ATLAS,e,c,u0,u1,vb,vt,225,185,150);
                c[0][1]=-0.25f; c[1][1]=-0.25f; c[1][0]=ox-lx*2.0f+fx*len; c[1][2]=oz-lz*2.0f+fz*len; c[2][0]=c[1][0]; c[2][1]=1.18f; c[2][2]=c[1][2];
                c[3][0]=c[2][0]; c[3][1]=c[2][1]; c[3][2]=c[2][2];
                fmFlat(e,c,120,86,50);                                                              /* side wall */
                c[0][0]=ox+lx*2.0f; c[0][1]=-0.25f; c[0][2]=oz+lz*2.0f; c[1][0]=ox+lx*2.0f+fx*len; c[1][1]=-0.25f; c[1][2]=oz+lz*2.0f+fz*len;
                c[2][0]=c[1][0]; c[2][1]=1.18f; c[2][2]=c[1][2]; c[3][0]=c[2][0]; c[3][1]=c[2][1]; c[3][2]=c[2][2];
                fmFlat(e,c,120,86,50);                                                              /* other side wall */
                c[0][0]=ox-lx*2.0f+fx*len; c[0][1]=-0.25f; c[0][2]=oz-lz*2.0f+fz*len; c[1][0]=ox+lx*2.0f+fx*len; c[1][1]=-0.25f; c[1][2]=oz+lz*2.0f+fz*len;
                c[2][0]=c[1][0]; c[2][1]=1.18f; c[2][2]=c[1][2]; c[3][0]=c[0][0]; c[3][1]=1.18f; c[3][2]=c[0][2];
                fmFlat(e,c,96,66,38);                                                               /* the drop at the end */
            }
        }
    }
    /* ---- right hairpin ---- */
    for(s=0.622f;s<0.702f;s+=0.010f) fmSign(fmIdx(s),-1,8.8f,FL_CHEV,0,0.7f,0.9f,2.4f);
    fmCones(0.600f,0.700f,1,7.0f,0.0035f);
    fmPlank(fmIdx(0.662f),-1,3.0f,0.3f,1.7f); fmPlank(fmIdx(0.665f),-1,1.0f,-0.5f,1.9f);
    /* ---- grain yard (lap video + screenshots): the barns and the steel pipe racks stand on the LEFT, silos on the right ---- */
    fmBarnB(0.800f,-1,33.0f,13.0f,6.0f,FL_LOG,232,226,218,98,86,72); fmBarnDress(0.800f,-1,33.0f,13.0f,6.0f);   /* long grey plank barn */
    fmBarnB(0.848f,-1,36.0f,9.0f,7.5f,FL_CONC,150,170,196,126,130,136);       /* big metal barn, open door */
    fmBarnB(0.888f,-1,34.0f,9.0f,6.0f,FL_LOG,255,100,84,226,228,232);         /* red barn, white roof */
    fmBarnB(0.926f,-1,30.0f,6.0f,5.0f,FL_LOG,255,100,84,92,60,46);            /* small red barn */
    fmBarnB(0.962f,-1,28.0f,8.0f,6.0f,FL_CONC,172,182,198,150,152,158);
    fmSilo(0.772f,-1,17.0f,3.2f,12.0f); fmSilo(0.782f,-1,19.0f,3.4f,13.0f);
    fmWater(0.822f,-1,48.0f); fmWater(0.866f,-1,54.0f);
    fmHayTruck(0.866f,-1,26.0f);
    fmSilo(0.790f,1,29.0f,4.2f,15.0f); fmSilo(0.801f,1,30.0f,4.2f,15.0f); fmSilo(0.812f,1,28.0f,4.0f,14.0f);
    fmSilo(0.868f,1,30.0f,4.4f,15.0f); fmSilo(0.880f,1,28.5f,4.0f,13.0f); fmSilo(0.906f,1,27.0f,3.8f,12.0f);
    {   int rk;
        for(rk=0;rk<4;rk++){                                              /* pipe racks: lattice columns on the left, truss beam and yellow pipes over the road */
            int ri=fmIdx(0.838f+0.0105f*(float)rk);
            fmBox(FG_FLAT,ri,-1.3f,-19.5f,0.45f,0.45f,0.0f,7.2f,63,-1,58,54,48);
            fmBox(FG_FLAT,ri, 1.3f,-19.5f,0.45f,0.45f,0.0f,7.2f,63,-1,58,54,48);
            fmBox(FG_FLAT,ri, 0.0f,-19.5f,0.25f,0.25f,3.2f,3.5f,63,-1,66,62,56);
            fmBox(FG_FLAT,ri, 0.0f,-6.0f,0.35f,13.5f,6.5f,7.1f,63,-1,66,62,56);   /* beam from the columns over the road */
            fmBox(FG_FLAT,ri, 0.0f,-14.0f,5.2f,0.35f,5.0f,5.6f,63,-1,182,152,70);
            fmBox(FG_FLAT,ri, 0.0f,-9.0f,5.2f,0.30f,5.8f,6.3f,63,-1,176,146,66);
            fmBox(FG_FLAT,ri, 0.0f,-3.5f,5.2f,0.30f,5.8f,6.3f,63,-1,176,146,66);
        }
    }
    fmCones(0.805f,0.870f,1,7.0f,0.0035f);
    fmBox(FG_ATLAS,fmIdx(0.948f),0.0f,-13.0f,9.0f,0.9f,0.0f,3.2f,FM_F|FM_B|FM_L|FM_R|FM_T,FL_ROCK,190,184,172);   /* stone wall */
    fmCones(0.905f,0.975f,-1,7.0f,0.0035f);
}

static void fmBuildProps(void)
{
    int i,k; float s;
    if(fmTrk){ fmBuildProps1(); return; }
    /* ---- start / finish: grandstand on the left, tyre arch on the right ---- */
    i=fmIdx(0.986f);
    for(k=0;k<4;k++){
        fmBox(FG_FLAT,i,6.0f,-(14.0f+2.4f*(float)k),12.0f,1.2f,0.0f,1.0f+1.0f*(float)k,FM_R|FM_T|FM_F|FM_B,-1,120,126,138);
        fmBox(FG_FLAT,i,6.0f,-(14.0f+2.4f*(float)k),12.0f,1.1f,1.0f+1.0f*(float)k,1.08f+1.0f*(float)k,FM_T,-1,196,170,120);
    }
    i=fmIdx(0.010f);
    for(k=0;k<7;k++){
        float an=3.14159f*(float)k/6.0f, no=21.0f+5.5f*cosf(an), yc=0.8f+5.2f*sinf(an);
        fmBox(FG_TIRES,i,0.0f,no,0.8f,0.8f,yc-0.8f,yc+0.8f,FM_F|FM_B|FM_L|FM_R,-1,255,255,255);
    }
    fmTower(fmIdx(0.045f),1,14.0f,2);                    /* garage poster on the right */
    fmTower(fmIdx(0.100f),-1,13.0f,1);
    /* ---- first corner (right-hand): chevron signs on the OUTSIDE (left) ---- */
    for(s=0.112f;s<0.150f;s+=0.010f) fmSign(fmIdx(s),-1,9.2f,FL_CHEV,0,0.75f,1.0f,2.5f);
    fmCones(0.150f,0.190f,-1,7.0f,0.0023f);
    fmTower(fmIdx(0.170f),-1,14.0f,0);
    /* ---- gorge with the bridge ---- */
    fmBox(FG_ATLAS,fmIdx(0.305f),0.0f,0.0f,2.4f,15.0f,6.8f,7.7f,63,FL_CONC,190,184,172);
    /* ---- bottom-right corner: row of chevrons on the left wall ---- */
    for(s=0.396f;s<0.436f;s+=0.010f) fmSign(fmIdx(s),-1,8.6f,FL_CHEV,0,0.7f,0.9f,2.4f);
    fmPlank(fmIdx(0.452f),-1,3.0f,0.3f,1.7f); fmPlank(fmIdx(0.456f),-1,1.0f,-0.5f,1.9f); fmPlank(fmIdx(0.459f),-1,4.2f,1.2f,1.4f);
    /* ---- the S-bump: big LED chevron boards + cones ---- */
    fmCones(0.470f,0.520f,-1,7.0f,0.0023f);
    for(s=0.485f;s<0.515f;s+=0.010f) fmFace(FG_ATLAS,fmIdx(s),-1,9.8f,0.0f,1.5f,0.7f,2.5f,FL_LED,0,255,255,255);
    fmSign(fmIdx(0.535f),-1,8.8f,FL_CHEV,0,0.75f,1.0f,2.5f);
    /* ---- rock arch ---- */
    fmBox(FG_ATLAS,fmIdx(0.562f),0.0f,0.0f,4.2f,16.0f,7.4f,12.5f,63,FL_ROCK,214,212,206);
    /* ---- boards + cones after the arch ---- */
    fmCones(0.600f,0.640f,-1,7.0f,0.0023f);
    for(s=0.605f;s<0.635f;s+=0.010f) fmFace(FG_ATLAS,fmIdx(s),-1,9.8f,0.0f,1.5f,0.7f,2.5f,FL_LED,0,255,255,255);
    for(s=0.655f;s<0.695f;s+=0.010f) fmSign(fmIdx(s),-1,9.0f,FL_CHEV,0,0.7f,1.0f,2.4f);
    /* ---- left-hand straight up the left edge ---- */
    fmPlank(fmIdx(0.842f),-1,7.8f,1.4f,1.8f); fmPlank(fmIdx(0.845f),-1,6.5f,0.2f,1.6f); fmPlank(fmIdx(0.848f),-1,8.4f,-1.0f,1.5f);
    /* ---- last corner + finishing straight ---- */
    fmTower(fmIdx(0.905f),-1,14.0f,0);
    fmCones(0.905f,0.962f,-1,7.0f,0.0023f);
    for(s=0.925f;s<0.955f;s+=0.010f) fmFace(FG_ATLAS,fmIdx(s),-1,9.8f,0.0f,1.5f,0.7f,2.5f,FL_LED,0,255,255,255);
    fmCones(0.965f,0.996f,-1,7.0f,0.0023f);
    /* barn (grey siding, dark door facing the road) */
    i=fmIdx(0.972f);
    fmBox(FG_FLAT,i,0.0f,28.0f,8.0f,6.0f,0.0f,4.8f,FM_F|FM_B|FM_L|FM_R,-1,118,128,142);
    fmFace(FG_FLAT,i,1,21.9f,0.0f,3.0f,0.0f,3.4f,0,0,38,42,50);           /* door side is the one facing the road: drawn as a plain panel */
    {
        float c[4][3], fx=nrmZ[i], fz=-nrmX[i], nx=nrmX[i], nz=nrmZ[i], cx=roadX[i]+nx*28.0f, cz=roadZ[i]+nz*28.0f;
        c[0][0]=cx-nx*6.4f-fx*8.4f; c[0][1]=4.8f; c[0][2]=cz-nz*6.4f-fz*8.4f;
        c[1][0]=cx-nx*6.4f+fx*8.4f; c[1][1]=4.8f; c[1][2]=cz-nz*6.4f+fz*8.4f;
        c[2][0]=cx+fx*8.4f;         c[2][1]=7.2f; c[2][2]=cz+fz*8.4f;
        c[3][0]=cx-fx*8.4f;         c[3][1]=7.2f; c[3][2]=cz-fz*8.4f;
        fmFlat(i,c,150,152,158);
        c[0][0]=cx+nx*6.4f-fx*8.4f; c[0][2]=cz+nz*6.4f-fz*8.4f;
        c[1][0]=cx+nx*6.4f+fx*8.4f; c[1][2]=cz+nz*6.4f+fz*8.4f;
        fmFlat(i,c,128,130,136);
    }
}

/* ---------------------------------- forest ---------------------------------------- */
static void fmTrees(void)
{
    int i,sx,near=0,k,j;
    nft=0;
    for(i=0;i<npts;i++){
        float s=(float)i/(float)npts;
        for(sx=0;sx<2;sx++){
            int sd=sx?1:-1, bt=fmBack[sx][i], bar=fmBar[sx][i], cnt;
            float dens=fmTrk?3.2f:3.3f, minOff=12.5f, maxOff=36.0f;
            if(s>0.95f||s<0.03f) dens=0.80f;
            else if(fmTrk && s>0.78f) dens=1.2f;                               /* HARVEST: the grain yard is mostly buildings */
            else if(bar==FT_ROCKW) dens=4.0f;
            if(bar==FT_ROCKW){ minOff=11.5f; maxOff=24.0f; }
            if(bt==FT_HILL){ minOff=14.0f; maxOff=35.0f; }
            if(bt==FT_ROCKB){ minOff=16.5f; maxOff=26.0f; }
            cnt=(int)dens; if(fmRf()<dens-(float)cnt) cnt++;
            for(k=0;k<cnt && nft<FM_FTMAX;k++){
                float off=minOff+fmRf()*(maxOff-minOff), jitter=(fmRf()-0.5f)*4.4f;
                float x=roadX[i]+nrmX[i]*(float)sd*off+nrmZ[i]*jitter, z=roadZ[i]+nrmZ[i]*(float)sd*off-nrmX[i]*jitter;
                float y=fmBackY(bt,off)+fmGH(i,off), lim=off*0.9f; int xi=(int)x, zi=(int)z, ok=1;
                if(fmTrk && fmGround(x,z)!=1) continue;                       /* HARVEST: trees only in the forest, none in the fields or the river */
                for(j=0;j<npts;j++){
                    int d=(j-i+npts)%npts; int dx,dz; float l2;
                    if(d<14||d>npts-14) continue;
                    dx=xi-segCx[j]; dz=zi-segCz[j]; l2=(float)(dx*dx+dz*dz);
                    if(l2<lim*lim){ ok=0; break; }
                }
                if(!ok) continue;
                ft[nft].tx=floattof32(x); ft[nft].ty=floattof32(y); ft[nft].tz=floattof32(z);
                ft[nft].sc=floattof32(1.25f+0.75f*fmRf()); ft[nft].var=(unsigned char)(fmRnd()%3);
                ftS[nft]=(short)i; nft++;
            }
            /* a few collidable trees just off the road where nothing solid stands */
            if(near<170 && fmSolid[sx][i]==0.0f && fabsf(roadH[i])<0.4f && s>0.04f && s<0.95f && (fmRnd()&255)<120){
                float off=9.6f+fmRf()*3.6f, jitter=(fmRf()-0.5f)*3.0f;
                float x=roadX[i]+nrmX[i]*(float)sd*off+nrmZ[i]*jitter, z=roadZ[i]+nrmZ[i]*(float)sd*off-nrmX[i]*jitter;
                int ok=1, xi=(int)x, zi=(int)z; float lim=off*0.85f;
                if(fmTrk && fmGround(x,z)!=1) ok=0;
                for(j=0;j<npts;j++){
                    int d=(j-i+npts)%npts; int dx,dz;
                    if(d<14||d>npts-14) continue;
                    dx=xi-segCx[j]; dz=zi-segCz[j];
                    if((float)(dx*dx+dz*dz)<lim*lim){ ok=0; break; }
                }
                if(ok && nobs<MAXOBS){
                    addObs(x,z,0.7f,OB_TREE,(int)(fmRnd()%3),0.0f);
                    obs[nobs-1].sc=floattof32(1.2f+0.6f*fmRf());
                    near++;
                }
            }
        }
    }
}

/* ---------------------------------- build ------------------------------------------ */
static void fmIndex(void)
{
    int g,i,k,run=0;
    static short cnt[FG_N][MAXPTS+1];
    for(g=0;g<FG_N;g++) for(i=0;i<=npts;i++) cnt[g][i]=0;
    for(k=0;k<nfq;k++) cnt[fqG[k]][fqS[k]]++;
    for(g=0;g<FG_N;g++){
        for(i=0;i<npts;i++){ fmGS[g][i]=(short)run; fmPos[g][i]=(short)run; run+=cnt[g][i]; }
        fmGS[g][npts]=(short)run;
    }
    for(k=0;k<nfq;k++) fmOrd[fmPos[fqG[k]][fqS[k]]++]=(short)k;
    run=0;
    for(i=0;i<=npts;i++) ftGS[i]=0;
    for(k=0;k<nft;k++) ftGS[ftS[k]+1]++;
    for(i=0;i<npts;i++) ftGS[i+1]+=ftGS[i];
    for(i=0;i<=npts;i++) ftPos[i]=ftGS[i];
    for(k=0;k<nft;k++) ftOrd[ftPos[ftS[k]]++]=(short)k;
}

static void fmBuild(void)
{
    int i,z,sx;
    fmSeed=fmTrk?0x51C3B7Du:0x2F6E1A9u; nfq=0; nft=0; nobs=0;
    fmZ=fmTrk?fmZones1:fmZones0; fmNZ=fmTrk?FM_NZONES1:FM_NZONES0;
    fmBk=0; fmNSign=0; fmRampOn=0; for(i=0;i<FM_BRKMAX;i++) fmBroken[i]=0;
    for(i=0;i<FMG_ROWS*FMG_COLS;i++) fmGridRT[i]=fmGrid[i];
    if(fmTrk){                                                      /* the open yard: every cell near the road in the yard stretch */
        int pi,gr,gc;
        for(pi=0;pi<npts;pi++){
            float hw=yardHW((float)pi/(float)npts);
            if(hw<HALFW+1.0f) continue;
            for(gr=0;gr<FMG_ROWS;gr++) for(gc=0;gc<FMG_COLS;gc++){
                float cx0=FMG_X0+((float)gc+0.5f)*FMG_CELL-roadX[pi], cz0=FMG_Z0+((float)gr+0.5f)*FMG_CELL-roadZ[pi];
                if(cx0*cx0+cz0*cz0<hw*hw) fmGridRT[gr*FMG_COLS+gc]=4;
            }
        }
    }                  /* a new race: every fence and cone is back */
    fmAcc[0]=0.0f;
    for(i=0;i<npts;i++) fmAcc[i+1]=fmAcc[i]+sqrtf(seg2[i]);
    /* 1. what stands beside each road segment */
    for(i=0;i<npts;i++){
        float s=(float)i/(float)npts;
        for(z=0;z<fmNZ;z++) if(s>=fmZ[z].a && s<fmZ[z].b) break;
        if(z>=fmNZ) z=fmNZ-1;
        fmBar[0][i]=fmZ[z].bl; fmBack[0][i]=fmZ[z].kl; fmBar[1][i]=fmZ[z].br; fmBack[1][i]=fmZ[z].kr;
        for(sx=0;sx<2;sx++){
            /* a hill or cliff is dropped where another part of the track runs close beside it */
            int bt=fmBack[sx][i];
            if(bt!=FT_NONE){
                float need=fmBackExtent(bt)+8.0f; int j, bad=0;
                for(j=0;j<npts && !bad;j++){
                    int d=(j-i+npts)%npts, dx,dz;
                    if(d<30||d>npts-30) continue;
                    dx=segCx[i]-segCx[j]; dz=segCz[i]-segCz[j];
                    if((float)(dx*dx+dz*dz)<need*need) bad=1;
                }
                if(bad) fmBack[sx][i]=FT_NONE;
            }
        }
        for(sx=0;sx<2;sx++){
            int h1=1,h2=1; float a=fmSolidOff(fmBar[sx][i],&h1), b=fmSolidOff(fmBack[sx][i],&h2);
            fmSolid[sx][i]=0.0f; fmHard[sx][i]=0;
            if(a>0.0f){ fmSolid[sx][i]=a; fmHard[sx][i]=(unsigned char)h1; }
            else if(b>0.0f){ fmSolid[sx][i]=b; fmHard[sx][i]=(unsigned char)h2; }
            fmVerge(i,sx);
            fmSegQuads(i,sx,fmBar[sx][i]);
            fmSegQuads(i,sx,fmBack[sx][i]);
        }
    }
    fmBuildProps();
    fmTrees();
    fmIndex();
}

/* ---------------------------------- draw -------------------------------------------- */
static void fmInitTex(void)
{
    glGenTextures(1,&fmTexId);
    glBindTexture(0,fmTexId);
    glTexImage2D(0,0,GL_RGBA,szlog(64),szlog(64),0,TEXGEN_TEXCOORD|GL_TEXTURE_WRAP_S|GL_TEXTURE_WRAP_T,tex_forest);
    curTex=-1;
}

static void fmDrawTrees(int base)
{
    int m,k,tw=texDefs[TX_PINE].w*16, th=texDefs[TX_PINE].h*16, hw=K16(1.75f), h=K16(7.0f);
    if(fmTrk){                                                    /* HARVEST: oaks in yellow / lime leaves instead of pines */
        useTex(TX_WHITE);
        for(m=-FM_WBACK;m<=FM_WFWD;m++){
            int i=(base+m+npts)%npts;
            for(k=ftGS[i];k<ftGS[i+1];k++){
                const FTree *t=&ft[ftOrd[k]];
                int dx=(t->tx>>12)-camXi, dz=(t->tz>>12)-camZi;
                if(dx*dx+dz*dz>62*62) continue;
                if(dx*camFxi+dz*camFzi< -384) continue;
                oakDraw(t->tx,t->ty,t->tz,t->sc,t->var,k);
            }
        }
        return;
    }
    useTex(TX_PINE);
    for(m=-FM_WBACK;m<=FM_WFWD;m++){
        int i=(base+m+npts)%npts;
        for(k=ftGS[i];k<ftGS[i+1];k++){
            const FTree *t=&ft[ftOrd[k]];
            int dx=(t->tx>>12)-camXi, dz=(t->tz>>12)-camZi, x0,z0,ua,ub,tr,tg,tb;
            if(dx*dx+dz*dz>62*62) continue;
            if(dx*camFxi+dz*camFzi< -384) continue;
            if(fmTrk){ if(t->var==0){ tr=255;tg=222;tb=146; } else if(t->var==1){ tr=255;tg=200;tb=112; } else { tr=236;tg=228;tb=150; } }   /* autumn tints */
            else if(t->var==0){ tr=255;tg=255;tb=255; } else if(t->var==1){ tr=228;tg=238;tb=214; } else { tr=255;tg=226;tb=176; }
            x0=(hw*camRxi)>>12; z0=(hw*camRzi)>>12;
            if(k&1){ ua=tw-6; ub=6; } else { ua=6; ub=tw-6; }
            glPushMatrix();
            mtxT(t->tx,t->ty,t->tz);
            mtxS(t->sc,t->sc,t->sc);
            glBegin(QUADS_MODE);
            glColor3b(tr,tg,tb);
            tv(ua,th-6,-x0,0,-z0); tv(ub,th-6,x0,0,z0); tv(ub,6,x0,h,z0); tv(ua,6,-x0,h,-z0);
            glEnd();
            glPopMatrix(1);
        }
    }
}

static void fmDraw(void)
{
    int g,m,k, base=cars[playerIdx].idx;
    for(g=0;g<FG_N;g++){
        if(g==FG_ATLAS){ glBindTexture(0,fmTexId); curTex=-2; }
        else useTex(g==FG_GRASS?(fmTrk?TX_SAND:TX_GRASS):(g==FG_FLAT?TX_WHITE:TX_TIRES));
        for(m=-FM_WBACK;m<=FM_WFWD;m++){
            int i=(base+m+npts)%npts;
            for(k=fmGS[g][i];k<fmGS[g][i+1];k++){
                const FQ *q=&fq[fmOrd[k]];
                if(fqB[fmOrd[k]] && fmBroken[fqB[fmOrd[k]]]) continue;            /* knocked down */
                glPushMatrix();
                mtxT(q->tx,q->ty,q->tz);
                glBegin(QUADS_MODE);
                glColor3b(q->r,q->g,q->b);
                tv(q->u0,q->vb,q->v[0],q->v[1],q->v[2]);  tv(q->u1,q->vb,q->v[3],q->v[4],q->v[5]);
                tv(q->u1,q->vt,q->v[6],q->v[7],q->v[8]);  tv(q->u0,q->vt,q->v[9],q->v[10],q->v[11]);
                glEnd();
                glPopMatrix(1);
            }
        }
    }
    fmDrawTrees(base);
}

/* ---------------------------------- collision with the roadside ----------------------- */
/* a knocked-down piece flies off with the physics of the car parts, and is gone after 5 seconds (keeps the DS memory free) */
static void fmFly(float x,float y,float z,const Car *c,int sgn,int nx0,int nz0,int ha,int hy,int hb,int r,int g,int b)
{
    float out=3.0f+rndf()*4.0f;
    spawnDebris(0,x,y,z,c->vx*0.7f+nrmX[nx0]*(float)sgn*out*0.5f+(rndf()-0.5f)*3.0f,3.0f+rndf()*3.5f,c->vz*0.7f+nrmZ[nz0]*(float)sgn*out*0.5f+(rndf()-0.5f)*3.0f,rndf()*6.28f,ha,hy,hb,r,g,b);
    dbr[(dbrNext+MAXDB-1)%MAXDB].life=300;
}

static void fmBreakCheck(Car *c)
{
    int i=c->idx, sx, k, got=0, pieces=0;
    float d=(c->x-roadX[i])*nrmX[i]+(c->z-roadZ[i])*nrmZ[i];
    if(!c->alive) return;
    for(sx=0;sx<2;sx++){                                          /* fence: the three sections around the car go down */
        float dd=sx?d:-d; int t=fmBar[sx][i];
        if((t==FT_FWOOD||t==FT_FMET) && dd>fmFO(i)-c->cr-0.2f){
            for(k=-1;k<=1;k++){
                int j=(i+k+npts)%npts, id=1+(sx?MAXPTS:0)+j, tj=fmBar[sx][j];
                if((tj==FT_FWOOD||tj==FT_FMET) && !fmBroken[id]){
                    float fo=fmFO(j), sg=sx?1.0f:-1.0f;
                    fmBroken[id]=1; got+=3;
                    if(pieces<4){                                 /* two or three planks / mesh panels per section, at most 4 pieces per hit */
                        int w=(tj==FT_FWOOD), q;
                        for(q=0;q<2 && pieces<4;q++,pieces++)
                            fmFly(roadX[j]+nrmX[j]*sg*fo,0.7f+rndf()*0.4f,roadZ[j]+nrmZ[j]*sg*fo,c,sx?1:-1,j,j,w?2800:2200,w?110:60,w?330:600,w?140:118,w?98:124,w?52:130);
                    }
                }
            }
        }
    }
    for(k=0;k<fmNSign;k++){                                       /* cones and signs */
        int ds=fsS[k]-i; float dx,dz,rr;
        if(ds<0) ds=-ds;
        if(ds>2 && ds<npts-2) continue;
        if(fmBroken[2*MAXPTS+1+k]) continue;
        dx=c->x-fsX[k]; dz=c->z-fsZ[k]; rr=c->cr+0.6f;
        if(dx*dx+dz*dz<rr*rr){
            fmBroken[2*MAXPTS+1+k]=1; got+=fsNit[k];
            if(fsNit[k]==2) fmFly(fsX[k],0.5f,fsZ[k],c,1,i,i,480,740,480,255,120,24);               /* cone */
            else            fmFly(fsX[k],1.0f,fsZ[k],c,1,i,i,1100,700,90,240,200,10);              /* sign board */
        }
    }
    if(got>0){
        int q;
        c->nitro=clampf(c->nitro+(float)got,0.0f,100.0f);
        c->vx*=0.975f; c->vz*=0.975f;                              /* a small bump */
        for(q=0;q<4;q++) spawnPart(PT_DUST,c->x+(rndf()-0.5f)*2.0f,0.5f,c->z+(rndf()-0.5f)*2.0f,(rndf()-0.5f)*5.0f,1.5f+rndf()*2.0f,(rndf()-0.5f)*5.0f,0.5f);
        sndHit(3.0f,c->x,c->z);                                    /* a crack, not the nitro jingle */
    }
}

static void fmCollide(Car *c)
{
    fmBreakCheck(c);
    int i=c->idx, sx;
    float d=(c->x-roadX[i])*nrmX[i]+(c->z-roadZ[i])*nrmZ[i];
    for(sx=0;sx<2;sx++){
        float so=fmSolid[sx][i], sd=sx?1.0f:-1.0f, dd=d*sd, lim, nx, nz, vn, dmg;
        if(so<=0.0f) continue;
        lim=so-c->cr;
        if(dd<=lim) continue;
        nx=nrmX[i]*sd; nz=nrmZ[i]*sd;
        c->x-=nx*(dd-lim); c->z-=nz*(dd-lim);
        vn=c->vx*nx+c->vz*nz;
        if(vn>0.0f){
            c->vx-=1.3f*vn*nx; c->vz-=1.3f*vn*nz;
            if(fmHard[sx][i]) tumbleHit(c,nx,nz,vn*1.3f,0.15f,5.0f);                 /* only solid walls / rock flip a car, a hill just stops it */
            dmg=(vn-4.0f)*(fmHard[sx][i]?0.30f:0.12f);
            if(dmg>0.0f){ hitSrc=SRC_WALL; hitFx(c,dmg,nx,nz); }
        }
    }
}
