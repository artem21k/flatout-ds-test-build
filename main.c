/* ============================================================================
   RUMBLE FOREST DS  -  a Flatout-2-inspired demolition derby + rally game
   Target: ORIGINAL Nintendo DS (ARM9 only, libnds / devkitARM). No DSi features.

   Modes / maps
     DERBY  : STADIUM, JUNKYARD   - 8 cars, last one running wins
     RALLY  : FOREST, CANYON      - 8 cars, 3 laps, ramming allowed

   Top screen    : hardware 3D engine, textured low-poly world
   Bottom screen : 256x192 bitmap HUD (minimap, gauges) + touch menu
   Textures      : procedurally generated (tools/gen_textures.py) and embedded
                   as source/textures.h, so no data files are needed.
   ============================================================================ */
/* ---- compile switches (uncomment if your libnds version complains) ----------
   #define ENABLE_SOUND 0          - drop all sound code (PSG/noise API differences)
   #define USE_RAW_MATRIX_REGS 1   - use MATRIX_TRANSLATE/MATRIX_SCALE registers directly
   ------------------------------------------------------------------------- */
#include <nds.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "textures.h"

#ifndef ENABLE_SOUND
#define ENABLE_SOUND 1          /* set to 0 if your libnds has no sound API */
#endif

/* ------------------------------ tunables ---------------------------------- */
#define PI_F     3.14159265f
#define TWOPI_F  6.28318531f
#define DT       (1.0f/60.0f)

#define MAXC     8
#define MAXCTRL  20
#define SUBD     7
#define MAXPTS   (MAXCTRL*SUBD)
#define HALFW    6.0f
#define LAPS     3          /* default lap count (lapsN is the live value) */
#define MAXOBS   200
#define MAXPART  40
#define CAR_R    1.5f          /* 1.1 * CAR_SCALE */

#define MAXSPD   28.0f
#define ACCEL    18.0f
#define BRAKE    40.0f
#define REVACC   10.0f
#define MAXREV   7.0f
#define DRAG     0.45f

#define TRIS_MODE  0            /* glBegin hardware values */
#define QUADS_MODE 1

#define K16(x)   ((int)((x)*4096.0f))     /* compile-time float -> v16 */
#define UV(t)    ((t)*16)                 /* texels -> 12.4 texcoord   */

#ifndef TEXTURE_PACK
#define TEXTURE_PACK(u,v) (((u)&0xFFFF)|((v)<<16))
#endif

#ifdef USE_RAW_MATRIX_REGS
static inline void mtxT(int x,int y,int z){ MATRIX_TRANSLATE=x; MATRIX_TRANSLATE=y; MATRIX_TRANSLATE=z; }
static inline void mtxS(int x,int y,int z){ MATRIX_SCALE=x; MATRIX_SCALE=y; MATRIX_SCALE=z; }
#else
#define mtxT(x,y,z) glTranslatef32((x),(y),(z))
#define mtxS(x,y,z) glScalef32((x),(y),(z))
#endif

#ifdef HOST_TEST
static int autopilot = 0;
static int dbgCamOn = 0;
static float dbgCam[6];
#define AUTOPILOT autopilot
#else
#define AUTOPILOT 0
#endif

enum { ST_MENU, ST_COUNT, ST_PLAY, ST_END };
enum { GM_DERBY, GM_RACE };
enum { PT_SMOKE, PT_SPARK, PT_DUST, PT_FLAME, PT_BLACK };
enum { OB_TREE=0, OB_PILLAR, OB_BARREL, OB_BLOCK, OB_ROCK, OB_TIRES };

typedef struct {
    float x, z, vx, vz, h;      /* position, velocity, heading            */
    float sn, cs;               /* cached sin/cos of heading               */
    float hp, nitro, steer, spd;
    float skill, lane;          /* AI personality                          */
    float prog, nearD;          /* race progress / dist^2 to centre line   */
    int   def[4];               /* roof corner crumple (v16 units)         */
    int   alive, isPlayer, stuck, reverseT, nitroOn;
    int   idx, lap, finished, finishRank;
    int   r, g, b;
    int   model;                /* index into carDefs */
    float kTop, kAcc, kHan, kStr, mass, kNit;
} Car;

typedef struct {
    float x, z, r;
    int   ix, iz, ri;           /* 1/16 m ints for fast rejection          */
    int   kind, var;
    int   tx, tz, hx, hy, sc;   /* f32 values for matrix ops               */
    int   ux, uy, uz;           /* box texture spans (12.4)                */
} Obs;

typedef struct { float x,y,z,vx,vy,vz,life,max; int type; } Part;

typedef struct { int n; float p[MAXCTRL][2]; } TrackDef;

static const TrackDef trackDefs[2] = {
    { 14, {{0,-100},{60,-105},{105,-70},{110,-10},{80,30},{95,75},{50,110},
           {-10,95},{-30,50},{-75,60},{-110,20},{-95,-40},{-55,-60},{-40,-95}} },
    { 18, {{0,-110},{70,-110},{125,-85},{130,-30},{100,10},{60,25},{40,60},{70,100},
           {40,135},{-20,125},{-60,95},{-45,55},{-85,30},{-125,50},{-135,0},{-110,-50},
           {-60,-70},{-40,-105}} }
};

static const char *mapName[4] = { "STADIUM", "JUNKYARD", "FOREST", "CANYON" };
static const unsigned char themeGround[4] = { TX_DIRT, TX_DIRT, TX_GRASS, TX_SAND };
static const unsigned char themeSky[4]    = { TX_SKY_FOREST, TX_SKY_DESERT, TX_SKY_FOREST, TX_SKY_DESERT };
static const unsigned char themeTint[4][3]= { {255,255,255},{214,196,176},{255,255,255},{255,238,222} };
static const char *diffName[3] = { "EASY", "NORMAL", "HARD" };
static const float diffSkill[3] = { 0.86f, 1.0f, 1.08f };

static const unsigned char carCol[MAXC][3] = {
    {255,120,10},{200,30,30},{40,90,230},{235,200,40},
    {40,170,70},{160,60,200},{225,225,225},{40,180,180}
};

/* extras bolted onto a body */
#define X_PIPES 1      /* exhaust stacks through the bonnet */
#define X_SCOOP 2      /* bonnet scoop */
#define X_WING  4      /* rear wing */
#define X_FINS  8      /* tail fins */
#define X_BAR   16     /* bull bar */
#define X_SPLIT 32     /* front splitter */
#define X_BED   64     /* pick-up cargo bed walls */
#define X_CRATE 128    /* wooden crate in the bed */
#define CAR_SCALE 1.35f   /* cars are drawn (and collide) at this scale: bigger = less toy-like */

typedef struct {
    const char *name; int cls; const char *drive;     /* cls: 0 DERBY, 1 RACE, 2 STREET, 3 BONUS */
    float top, acc, han, str, wgt, nit;                /* stats 0..10 */
    unsigned char r, g, b;
    /* shape, metres, car-local, +b = forward */
    float W, L, y0, yF, yR;                            /* body half-width/length, underside, top at front/rear */
    float cb0, cb1, ct0, ct1, cw0, cw1, cy;            /* cabin: base back/front, roof back/front, half-width base/roof, roof height */
    float wb, wh, wy;                                  /* wheels: axle offset, half-length, height */
    int   xt;                                          /* X_* extras */
} CarDef;
#define NCARDEF 8
static const char *className[4] = { "DERBY", "RACE", "STREET", "BONUS" };
static const CarDef carDefs[NCARDEF] = {
 /* name        cls drv    top  acc  han  str  wgt  nit    r   g   b     W     L    y0    yF    yR    cb0    cb1    ct0    ct1   cw0   cw1    cy    wb    wh    wy   extras */
 { "ROAMER",     0,"RWD", 3.0f,5.2f,5.0f,8.0f,7.8f,2.0f, 225,225,225, 0.70f,1.30f,0.26f,0.80f,0.84f,-0.88f, 0.22f,-0.72f, 0.02f,0.58f,0.50f,1.30f, 0.88f,0.31f,0.64f, X_PIPES|X_BAR },
 { "MALICE",     0,"RWD", 3.6f,6.2f,5.6f,7.0f,6.6f,2.2f, 150, 40, 40, 0.66f,1.28f,0.24f,0.66f,0.80f,-0.98f,-0.08f,-0.80f,-0.30f,0.54f,0.44f,1.10f, 0.86f,0.30f,0.62f, X_PIPES|X_SCOOP },
 { "NEVADA",     1,"RWD", 6.4f,6.6f,6.4f,5.0f,5.0f,3.2f, 235,200, 40, 0.72f,1.26f,0.20f,0.56f,0.62f,-0.82f, 0.18f,-0.62f,-0.02f,0.56f,0.46f,0.98f, 0.84f,0.30f,0.62f, X_WING|X_SPLIT },
 { "INSETTA",    1,"4WD", 6.8f,7.0f,8.0f,3.8f,3.4f,3.6f,  40, 90,230, 0.66f,1.04f,0.26f,0.62f,0.78f,-0.84f, 0.36f,-0.76f, 0.14f,0.54f,0.48f,1.20f, 0.70f,0.30f,0.62f, X_WING|X_SCOOP },
 { "BULLET GT",  2,"RWD", 8.2f,7.2f,6.4f,3.4f,4.4f,4.8f, 255,130, 20, 0.72f,1.34f,0.22f,0.60f,0.70f,-0.72f, 0.16f,-0.58f,-0.12f,0.58f,0.50f,1.02f, 0.90f,0.30f,0.62f, X_SCOOP|X_SPLIT },
 { "CANYON",     2,"RWD", 7.4f,6.8f,5.6f,4.6f,6.8f,4.2f, 200, 30, 30, 0.72f,1.36f,0.34f,0.90f,0.84f, 0.02f, 0.80f, 0.16f, 0.58f,0.64f,0.58f,1.50f, 0.92f,0.38f,0.76f, X_BED|X_BAR },
 { "TRUCK",      3,"4WD", 4.0f,4.4f,3.8f,9.0f,9.0f,1.5f,  70,140, 70, 0.80f,1.50f,0.42f,1.02f,0.94f,-0.10f, 0.86f, 0.04f, 0.60f,0.72f,0.66f,1.78f, 1.00f,0.46f,0.92f, X_BED|X_BAR|X_CRATE },
 { "FLATMOBILE", 3,"RWD", 9.4f,8.8f,7.4f,3.0f,3.8f,7.0f,  60, 60, 84, 0.66f,1.50f,0.20f,0.46f,0.52f,-0.34f, 0.56f,-0.14f, 0.22f,0.38f,0.22f,0.92f, 0.98f,0.28f,0.56f, X_FINS|X_SPLIT }
};

/* -------------------------------- globals --------------------------------- */
static int   gmode, mapId, diff=1, state, playerIdx, ncars, nobs, npts;
static int   frameNo, countT, endT, finishCount, wrecks, hudTimer;
static float camH, shake, menuSpin, arenaH=45.0f;
static char  endMsg1[32], endMsg2[32];
static Car   cars[MAXC];
static Obs   obs[MAXOBS];
static Part  parts[MAXPART];
static int   partNext;
static int   menuScr, mainSel, modeSel, mapSel, carSel=4, optSel, lastMap=2;
static int   lapsN=3, sndOn=1;
static float trackTotal;
enum { SC_MAIN, SC_MAPS, SC_CAR, SC_OPT };

static float roadX[MAXPTS], roadZ[MAXPTS], nrmX[MAXPTS], nrmZ[MAXPTS];
static float trkH[MAXPTS], limitSp[MAXPTS], seg2[MAXPTS];
static int   segV[MAXPTS][12], segTx[MAXPTS], segTz[MAXPTS], segCx[MAXPTS], segCz[MAXPTS];
static int   segV0[MAXPTS], segV1[MAXPTS];
static float mapOx, mapOy, mapS;           /* minimap transform */

static int camXi, camZi, camFxi, camFzi;   /* culling info (set in render) */
static int camRxi, camRzi;                 /* billboard right vector (4096) */

/* ------------------------------- helpers ---------------------------------- */
static inline int   cl16(float v){ int i=(int)(v*4096.0f); if(i>32500)i=32500; if(i<-32500)i=-32500; return i; }
static inline float clampf(float v,float lo,float hi){ return v<lo?lo:(v>hi?hi:v); }
static inline float wrapa(float a){ while(a>PI_F)a-=TWOPI_F; while(a<-PI_F)a+=TWOPI_F; return a; }
static inline int   iabs(int v){ return v<0?-v:v; }
static inline int   imin(int a,int b){ return a<b?a:b; }

static u32 rngState = 2463534242u;
static u32 rnd(void){ rngState = rngState*1664525u + 1013904223u; return rngState>>8; }
static float rndf(void){ return (float)(rnd()&0xFFFF)/65536.0f; }

/* ================================ SOUND ==================================== */
#if ENABLE_SOUND
static int engineCh=-1, crashCh[2]={-1,-1}, crashT[2], beepCh=-1, beepT;
static void sndInit(void){ soundEnable(); }
static void sndEngineStop(void){ if(engineCh>=0){ soundKill(engineCh); engineCh=-1; } }
static void sndEngine(float spd,int nit,int on)
{
    if(!on||!sndOn){ sndEngineStop(); return; }
    if(engineCh<0) engineCh=soundPlayPSG(DutyCycle_25,90,0,64);
    if(engineCh>=0){
        soundSetFreq(engineCh,(int)(62.0f+fabsf(spd)*7.0f+(nit?45.0f:0.0f)));
        soundSetVolume(engineCh,(int)(26.0f+fabsf(spd)*1.1f));
    }
}
static void sndCrash(float dmg)
{
    int s=(crashT[0]<=crashT[1])?0:1;
    if(!sndOn) return;
    if(crashCh[s]>=0) soundKill(crashCh[s]);
    crashCh[s]=soundPlayNoise(2500+(int)(dmg*60.0f),(int)clampf(40.0f+dmg*5.0f,40.0f,127.0f),64);
    crashT[s]=6+(int)clampf(dmg*0.4f,0.0f,10.0f);
}
static void sndBeep(int hi)
{
    if(!sndOn) return;
    if(beepCh>=0) soundKill(beepCh);
    beepCh=soundPlayPSG(DutyCycle_50,hi?880:440,100,64);
    beepT=hi?20:10;
}
static void sndTick(void)
{
    int s;
    for(s=0;s<2;s++) if(crashCh[s]>=0 && --crashT[s]<=0){ soundKill(crashCh[s]); crashCh[s]=-1; crashT[s]=0; }
    if(beepCh>=0 && --beepT<=0){ soundKill(beepCh); beepCh=-1; }
}
#else
static void sndInit(void){}
static void sndEngineStop(void){}
static void sndEngine(float spd,int nit,int on){ (void)spd;(void)nit;(void)on; }
static void sndCrash(float dmg){ (void)dmg; }
static void sndBeep(int hi){ (void)hi; }
static void sndTick(void){}
#endif

/* ============================== PARTICLES ================================== */
static void spawnPart(int type,float x,float y,float z,float vx,float vy,float vz,float life)
{
    Part *p=&parts[partNext];
    partNext=(partNext+1)%MAXPART;
    p->type=type; p->x=x; p->y=y; p->z=z; p->vx=vx; p->vy=vy; p->vz=vz; p->life=life; p->max=life;
}

static void updateParts(void)
{
    int i;
    for(i=0;i<MAXPART;i++){
        Part *p=&parts[i];
        if(p->life<=0.0f) continue;
        p->life-=DT;
        p->x+=p->vx*DT; p->y+=p->vy*DT; p->z+=p->vz*DT;
        if(p->type==PT_SPARK){ p->vy-=14.0f*DT; if(p->y<0.05f){ p->y=0.05f; p->vy*=-0.3f; p->vx*=0.6f; p->vz*=0.6f; } }
        else if(p->type==PT_FLAME){ /* straight */ }
        else { p->vx*=0.97f; p->vz*=0.97f; p->vy*=0.98f; }
    }
}

static void emitSparks(float x,float z,int n)
{
    int i;
    for(i=0;i<n;i++)
        spawnPart(PT_SPARK,x,0.5f+rndf()*0.4f,z,(rndf()-0.5f)*9.0f,2.0f+rndf()*4.0f,(rndf()-0.5f)*9.0f,0.35f+rndf()*0.3f);
}

/* ================================ TRACK ==================================== */
static void buildTrack(int which)
{
    const TrackDef *td=&trackDefs[which];
    int n=td->n, k,s,i,a;
    float total=0.0f, scale, acc=0.0f, mnx=1e9f,mxx=-1e9f,mnz=1e9f,mxz=-1e9f;
    npts=n*SUBD;
    i=0;
    for(k=0;k<n;k++){
        const float *p0=td->p[(k+n-1)%n], *p1=td->p[k];
        const float *p2=td->p[(k+1)%n],   *p3=td->p[(k+2)%n];
        for(s=0;s<SUBD;s++,i++){
            float t=(float)s/(float)SUBD, t2=t*t, t3=t2*t, q[2];
            for(a=0;a<2;a++)
                q[a]=0.5f*((2.0f*p1[a])+(-p0[a]+p2[a])*t
                    +(2.0f*p0[a]-5.0f*p1[a]+4.0f*p2[a]-p3[a])*t2
                    +(-p0[a]+3.0f*p1[a]-3.0f*p2[a]+p3[a])*t3);
            roadX[i]=q[0]; roadZ[i]=q[1];
        }
    }
    for(i=0;i<npts;i++){
        int j=(i+1)%npts, h=(i+npts-1)%npts;
        float tx=roadX[j]-roadX[h], tz=roadZ[j]-roadZ[h];
        float l=sqrtf(tx*tx+tz*tz), dx=roadX[j]-roadX[i], dz=roadZ[j]-roadZ[i];
        tx/=l; tz/=l;
        nrmX[i]=-tz; nrmZ[i]=tx;
        trkH[i]=atan2f(tx,-tz);          /* heading whose forward=(sin h,-cos h) */
        seg2[i]=dx*dx+dz*dz;
        total+=sqrtf(seg2[i]);
        if(roadX[i]<mnx)mnx=roadX[i];
        if(roadX[i]>mxx)mxx=roadX[i];
        if(roadZ[i]<mnz)mnz=roadZ[i];
        if(roadZ[i]>mxz)mxz=roadZ[i];
    }
    trackTotal=total;
    /* texture scale so the road texture loops seamlessly around the lap */
    scale=5.33f; scale=64.0f*floorf(total*scale/64.0f+0.5f)/total;
    for(i=0;i<npts;i++){
        int j=(i+1)%npts, m=(i+6)%npts, nn=(i+1)%npts;
        float ang=fabsf(wrapa(trkH[m]-trkH[nn]));
        float lim=36.0f-24.0f*ang;
        float lx0=roadX[i]+nrmX[i]*HALFW, lz0=roadZ[i]+nrmZ[i]*HALFW;
        float rx0=roadX[i]-nrmX[i]*HALFW, rz0=roadZ[i]-nrmZ[i]*HALFW;
        float lx1=roadX[j]+nrmX[j]*HALFW, lz1=roadZ[j]+nrmZ[j]*HALFW;
        float rx1=roadX[j]-nrmX[j]*HALFW, rz1=roadZ[j]-nrmZ[j]*HALFW;
        float cx=0.5f*(roadX[i]+roadX[j]), cz=0.5f*(roadZ[i]+roadZ[j]);
        float len=sqrtf(seg2[i]);
        int *v=segV[i];
        limitSp[i]=clampf(lim,13.0f,34.0f);
        v[0]=cl16(lx0-cx); v[1]=0; v[2]=cl16(lz0-cz);
        v[3]=cl16(rx0-cx); v[4]=0; v[5]=cl16(rz0-cz);
        v[6]=cl16(rx1-cx); v[7]=0; v[8]=cl16(rz1-cz);
        v[9]=cl16(lx1-cx); v[10]=0;v[11]=cl16(lz1-cz);
        segTx[i]=floattof32(cx); segTz[i]=floattof32(cz);
        segCx[i]=(int)cx; segCz[i]=(int)cz;
        segV0[i]=((int)(acc*scale*16.0f))%1024;
        segV1[i]=segV0[i]+(int)(len*scale*16.0f);
        acc+=len;
    }
    segV0[0]=0; segV1[0]=UV(64);        /* start line segment uses its own texture */
    /* minimap transform: 120x120 box at (8,20) */
    {
        float w=mxx-mnx, h=mxz-mnz, s1=104.0f/w, s2=104.0f/h;
        mapS=(s1<s2)?s1:s2;
        mapOx=8.0f+(116.0f-w*mapS)*0.5f-mnx*mapS;
        mapOy=28.0f+(112.0f-h*mapS)*0.5f-mnz*mapS;
    }
}

static void addObs(float x,float z,float r,int kind,int var,float hy)
{
    Obs *o;
    float sc=1.0f;
    if(nobs>=MAXOBS) return;
    o=&obs[nobs++];
    o->x=x; o->z=z; o->r=r; o->kind=kind; o->var=var;
    o->ix=(int)(x*16.0f); o->iz=(int)(z*16.0f);
    o->ri=(int)((CAR_R+r)*16.0f)+2;
    o->tx=floattof32(x); o->tz=floattof32(z);
    o->hx=floattof32(r*0.9f); o->hy=floattof32(hy);
    if(kind==OB_TREE)      sc=(mapId==3)?(0.9f+rndf()*0.4f):(0.9f+rndf()*0.5f);
    else if(kind==OB_ROCK) sc=0.8f+rndf()*0.7f;
    o->sc=floattof32(sc);
    o->ux=UV(32); o->uy=UV(32); o->uz=UV(32);
    if(kind==OB_BLOCK){ o->ux=o->uz=UV(64); o->uy=UV(64); }
    if(kind==OB_TIRES){ o->uy=UV(48); }
}

static void buildScenery(void)
{
    int tries=0, ok, j, k;
    nobs=0;
    while(nobs<150 && tries<3000){
        float side=(rnd()&1)?1.0f:-1.0f;
        float off=HALFW+4.5f+rndf()*26.0f;
        float x,z;
        tries++;
        k=rnd()%npts;
        x=roadX[k]+nrmX[k]*off*side+(rndf()-0.5f)*6.0f;
        z=roadZ[k]+nrmZ[k]*off*side+(rndf()-0.5f)*6.0f;
        ok=1;
        for(j=0;j<npts;j++){
            float dx=roadX[j]-x, dz=roadZ[j]-z;
            if(dx*dx+dz*dz < (HALFW+4.0f)*(HALFW+4.0f)){ ok=0; break; }
        }
        if(!ok) continue;
        if(mapId==3 && (rnd()%10)>=6) addObs(x,z,1.0f,OB_ROCK,(int)(rnd()%2),0.0f);
        else addObs(x,z,0.7f,OB_TREE,(int)(rnd()%3),0.0f);
    }
}

/* ================================== CARS =================================== */
static void initCar(Car *c,int k,float x,float z,float h)
{
    memset(c,0,sizeof(Car));
    c->x=x; c->z=z; c->h=h; c->sn=sinf(h); c->cs=cosf(h);
    c->hp=100.0f; c->nitro=25.0f; c->alive=1;
    c->isPlayer=(k==playerIdx);
    c->skill=c->isPlayer?1.0f:(0.80f+0.04f*(float)(k%5))*diffSkill[diff];
    c->lane=((float)((k*37)%9)-4.0f)*0.8f;
    {
        const CarDef *d;
        static const unsigned char aiDerby[7]={0,1,6,0,1,5,6}, aiRace[7]={2,3,4,5,2,3,4};
        c->model=c->isPlayer?carSel:((gmode==GM_RACE)?aiRace[k%7]:aiDerby[k%7]);
        d=&carDefs[c->model];
        c->kTop=0.75f+0.04f*d->top; c->kAcc=0.70f+0.06f*d->acc; c->kHan=0.80f+0.04f*d->han;
        c->kStr=1.35f-0.07f*d->str; c->mass=0.60f+0.12f*d->wgt; c->kNit=0.60f+0.08f*d->nit;
        if(c->isPlayer){ c->r=d->r; c->g=d->g; c->b=d->b; }
        else { c->r=carCol[k][0]; c->g=carCol[k][1]; c->b=carCol[k][2]; }
    }
}

static void updateNearest(Car *c,int global)
{
    int k,i,best=c->idx,lo,hi,prev=c->idx,j;
    float bd=1e9f, dx, dz, d;
    lo=global?0:-3; hi=global?npts-1:6;
    for(k=lo;k<=hi;k++){
        i=global?k:((c->idx+k+npts)%npts);
        dx=roadX[i]-c->x; dz=roadZ[i]-c->z; d=dx*dx+dz*dz;
        if(d<bd){ bd=d; best=i; }
    }
    c->nearD=bd;
    if(!global){
        if(prev>npts*3/4 && best<npts/4) c->lap++;
        else if(prev<npts/4 && best>npts*3/4) c->lap--;
    }
    c->idx=best;
    j=(best+1)%npts;
    dx=c->x-roadX[best]; dz=c->z-roadZ[best];
    d=(dx*(roadX[j]-roadX[best])+dz*(roadZ[j]-roadZ[best]))/seg2[best];
    c->prog=(float)(c->lap*npts+best)+clampf(d,0.0f,1.0f);
}

static void placeOnTrack(Car *c,int i,float lane)
{
    c->x=roadX[i]+nrmX[i]*lane; c->z=roadZ[i]+nrmZ[i]*lane;
    c->h=trkH[i]; c->sn=sinf(c->h); c->cs=cosf(c->h);
    c->vx=c->vz=0.0f; c->spd=0.0f; c->idx=i; c->stuck=0; c->reverseT=0;
}

static void hitFx(Car *c,float dmg,float nx,float nz)
{
    float a,b,px,pz;
    int corner;
    if(!c->alive||dmg<=0.0f) return;
    dmg*=c->kStr;
    c->hp-=dmg;
    px=c->x+nx*CAR_R; pz=c->z+nz*CAR_R;
    if(dmg>1.5f) emitSparks(px,pz,1+(int)(dmg*0.25f));
    if(dmg>3.0f){
        Car *p=&cars[playerIdx];
        float dx=p->x-c->x, dz=p->z-c->z;
        if(dx*dx+dz*dz<1600.0f) sndCrash(dmg);
    }
    if(c->hp<=0.0f){
        int i;
        c->hp=0.0f; c->alive=0; if(!c->isPlayer) wrecks++;
        for(i=0;i<5;i++) spawnPart(PT_BLACK,c->x,0.8f,c->z,(rndf()-0.5f)*4.0f,2.0f+rndf()*2.0f,(rndf()-0.5f)*4.0f,1.2f);
        emitSparks(c->x,c->z,6);
    }
    c->nitro=clampf(c->nitro+dmg*c->kNit,0.0f,100.0f);
    /* nx,nz = world direction from this car towards what hit it */
    a=nx*c->cs+nz*c->sn;       /* along car-right   */
    b=nx*c->sn-nz*c->cs;       /* along car-forward */
    corner=(a>0.0f?1:0)|(b>0.0f?2:0);
    c->def[corner]+=(int)(dmg*90.0f);
    if(c->def[corner]>1500) c->def[corner]=1500;
    c->h+=-a*(b>0.0f?1.0f:-1.0f)*dmg*0.012f;      /* spin from off-centre hits */
    if(c->isPlayer) shake=clampf(shake+dmg*0.02f,0.0f,0.6f);
}

/* arcade car physics: forward speed + lateral slip */
static void driveCar(Car *c,float thr,float strTgt,int nit,int hb,int onRoad)
{
    float fx=c->sn, fz=-c->cs, rx=c->cs, rz=c->sn;
    float vf=c->vx*fx+c->vz*fz, vl=c->vx*rx+c->vz*rz;
    float power=0.55f+0.45f*(c->hp*0.01f);
    float maxsp=MAXSPD*power*c->skill*c->kTop, acc=ACCEL*c->kAcc, grip, spdf, yaw;

    c->nitroOn=0;
    if(nit && c->nitro>0.0f){ acc*=2.0f; maxsp*=1.4f; c->nitro-=0.55f; if(c->nitro<0.0f)c->nitro=0.0f; c->nitroOn=1; }
    if(!onRoad) maxsp*=0.6f;
    c->steer+=(strTgt-c->steer)*0.25f;

    if(thr>0.0f){ if(vf<maxsp) vf+=acc*thr*DT; else vf-=(vf-maxsp)*2.0f*DT; }
    else if(thr<0.0f){ if(vf>0.5f) vf+=BRAKE*thr*DT; else if(vf>-MAXREV) vf+=REVACC*thr*DT; }
    vf-=vf*DRAG*DT;
    if(!onRoad) vf-=vf*1.2f*DT;

    grip=hb?1.5f:(onRoad?9.0f:5.5f);
    vl-=vl*clampf(grip*DT,0.0f,1.0f);

    spdf=fabsf(vf);
    yaw=c->steer*2.5f*c->kHan*clampf(spdf/5.0f,0.0f,1.0f)/(1.0f+spdf*0.025f);
    if(hb) yaw*=1.5f;
    if(vf<0.0f) yaw=-yaw;
    c->h=wrapa(c->h+yaw*DT);

    c->sn=sinf(c->h); c->cs=cosf(c->h);
    fx=c->sn; fz=-c->cs; rx=c->cs; rz=c->sn;
    c->vx=fx*vf+rx*vl; c->vz=fz*vf+rz*vl;
    c->x+=c->vx*DT; c->z+=c->vz*DT;
    c->spd=vf;
}

/* ================================= AI ====================================== */
static void aiThink(Car *c,int k,float *thr,float *str,int *nit)
{
    float tx,tz,desired,diff2,sp;
    int j,t=-1;
    *thr=1.0f; *str=0.0f; *nit=0;
    sp=sqrtf(c->vx*c->vx+c->vz*c->vz);

    if(gmode==GM_DERBY){
        float bd=1e9f,dx,dz,d;
        for(j=0;j<ncars;j++){
            if(j==k||!cars[j].alive) continue;
            dx=cars[j].x-c->x; dz=cars[j].z-c->z; d=dx*dx+dz*dz;
            if(j==playerIdx && (k%3)==0) d*=0.5f;  /* a few rivals hold a grudge against you */
            if(d<bd){ bd=d; t=j; }
        }
        if(t<0){ tx=0.0f; tz=0.0f; }
        else { tx=cars[t].x+cars[t].vx*0.3f; tz=cars[t].z+cars[t].vz*0.3f; }
        if(fabsf(c->x)>arenaH-7.0f||fabsf(c->z)>arenaH-7.0f){ tx=0.0f; tz=0.0f; }
    } else {
        int ti=(c->idx+3)%npts;
        float lim=limitSp[c->idx]*c->skill;
        tx=roadX[ti]+nrmX[ti]*c->lane; tz=roadZ[ti]+nrmZ[ti]*c->lane;
        if(sp>lim+2.0f) *thr=-0.7f;
    }
    desired=atan2f(tx-c->x,-(tz-c->z));
    diff2=wrapa(desired-c->h);
    *str=clampf(diff2*2.5f,-1.0f,1.0f);
    if(fabsf(diff2)<0.15f && c->nitro>50.0f && (gmode==GM_DERBY || limitSp[c->idx]>28.0f)) *nit=1;

    if(c->reverseT>0){ *thr=-1.0f; *str=-*str; c->reverseT--; }
    if(sp<1.5f) c->stuck++; else c->stuck=0;
    if(c->stuck>45 && c->reverseT<=0){
        if(gmode==GM_RACE && c->stuck>130){ placeOnTrack(c,c->idx,0.0f); c->stuck=0; }
        else c->reverseT=45;
    }
}

/* ============================== COLLISIONS ================================= */
static void carVsCar(Car *a,Car *b)
{
    float dx=b->x-a->x, dz=b->z-a->z, d2=dx*dx+dz*dz;
    float lim=2.0f*CAR_R, d,nx,nz,pen,rvn,dmg;
    if(!a->alive && !b->alive) return;
    if(d2>=lim*lim || d2<0.0001f) return;
    d=sqrtf(d2); nx=dx/d; nz=dz/d;
    pen=(lim-d)*0.5f;
    a->x-=nx*pen; a->z-=nz*pen; b->x+=nx*pen; b->z+=nz*pen;
    rvn=(b->vx-a->vx)*nx+(b->vz-a->vz)*nz;
    if(rvn<0.0f){
        float ma=a->mass, mb=b->mass, J=-(1.4f)*rvn/(1.0f/ma+1.0f/mb);
        a->vx-=nx*J/ma; a->vz-=nz*J/ma; b->vx+=nx*J/mb; b->vz+=nz*J/mb;
        dmg=(-rvn-2.0f)*0.5f;
        if(dmg>0.0f){ hitFx(a,dmg*sqrtf(mb/ma),nx,nz); hitFx(b,dmg*sqrtf(ma/mb),-nx,-nz); }
    }
}

static void carVsObstacles(Car *c)
{
    int i, xi=(int)(c->x*16.0f), zi=(int)(c->z*16.0f), dxi, dzi;
    for(i=0;i<nobs;i++){
        const Obs *o=&obs[i];
        float dx,dz,d2,rr,d,nx,nz,vn,dmg;
        dxi=xi-o->ix; if(dxi>o->ri||dxi<-o->ri) continue;
        dzi=zi-o->iz; if(dzi>o->ri||dzi<-o->ri) continue;
        dx=c->x-o->x; dz=c->z-o->z; d2=dx*dx+dz*dz; rr=CAR_R+o->r;
        if(d2>=rr*rr||d2<0.0001f) continue;
        d=sqrtf(d2); nx=dx/d; nz=dz/d;
        c->x=o->x+nx*rr; c->z=o->z+nz*rr;
        vn=c->vx*nx+c->vz*nz;
        if(vn<0.0f){
            c->vx-=1.25f*vn*nx; c->vz-=1.25f*vn*nz;
            dmg=(-vn-4.0f)*0.35f;
            if(dmg>0.0f) hitFx(c,dmg,-nx,-nz);
        }
    }
}

static void carVsWalls(Car *c)
{
    float lim=arenaH-CAR_R, dmg;
    if(c->x>lim){  c->x=lim;  if(c->vx>0.0f){ dmg=(c->vx-4.0f)*0.25f;  c->vx*=-0.3f; hitFx(c,dmg, 1.0f,0.0f);} }
    if(c->x<-lim){ c->x=-lim; if(c->vx<0.0f){ dmg=(-c->vx-4.0f)*0.25f; c->vx*=-0.3f; hitFx(c,dmg,-1.0f,0.0f);} }
    if(c->z>lim){  c->z=lim;  if(c->vz>0.0f){ dmg=(c->vz-4.0f)*0.25f;  c->vz*=-0.3f; hitFx(c,dmg,0.0f, 1.0f);} }
    if(c->z<-lim){ c->z=-lim; if(c->vz<0.0f){ dmg=(-c->vz-4.0f)*0.25f; c->vz*=-0.3f; hitFx(c,dmg,0.0f,-1.0f);} }
}

/* =============================== GAME FLOW ================================= */
static int countAlive(void){ int i,n=0; for(i=0;i<ncars;i++) if(cars[i].alive) n++; return n; }

static int racePlace(int k)
{
    Car *me=&cars[k];
    int j,p;
    if(me->finished) return me->finishRank;
    p=1+finishCount;
    for(j=0;j<ncars;j++){
        if(j==k||cars[j].finished||!cars[j].alive) continue;
        if(cars[j].prog>me->prog) p++;
    }
    return p;
}

static void buildArena(int map)
{
    int k;
    nobs=0;
    if(map==0){
        arenaH=45.0f;
        addObs(0.0f,0.0f,4.0f,OB_BLOCK,0,3.0f);
        for(k=0;k<8;k++){ float a=(float)k*TWOPI_F/8.0f+PI_F/8.0f; addObs(sinf(a)*20.0f,-cosf(a)*20.0f,1.2f,OB_PILLAR,0,1.6f); }
        for(k=0;k<8;k++){ float a=(float)k*TWOPI_F/8.0f;          addObs(sinf(a)*36.0f,-cosf(a)*36.0f,0.8f,OB_BARREL,0,0.7f); }
    } else {
        arenaH=52.0f;
        for(k=0;k<10;k++){ float a=((float)k+0.5f)*TWOPI_F/10.0f; addObs(sinf(a)*14.0f,-cosf(a)*14.0f,1.2f,OB_TIRES,0,1.2f); }
        addObs( 28.0f, 28.0f,3.2f,OB_BLOCK,0,2.6f); addObs(-28.0f, 28.0f,3.2f,OB_BLOCK,0,2.6f);
        addObs( 28.0f,-28.0f,3.2f,OB_BLOCK,0,2.6f); addObs(-28.0f,-28.0f,3.2f,OB_BLOCK,0,2.6f);
        for(k=0;k<4;k++){ float a=(float)k*TWOPI_F/4.0f+PI_F/4.0f; addObs(sinf(a)*44.0f,-cosf(a)*44.0f,0.8f,OB_BARREL,0,0.7f); }
        addObs(0.0f,0.0f,3.0f,OB_BLOCK,0,2.4f);
        for(k=0;k<4;k++){ float a=(float)k*TWOPI_F/4.0f+PI_F/4.0f; addObs(sinf(a)*22.0f,-cosf(a)*22.0f,1.2f,OB_PILLAR,0,1.6f); }
    }
}

static void setupDerby(void)
{
    int k;
    float spawnR;
    ncars=MAXC; playerIdx=0;
    buildArena(mapId);
    spawnR=arenaH*0.62f;
    for(k=0;k<ncars;k++){
        float a=(float)k*TWOPI_F/(float)ncars;
        initCar(&cars[k],k,sinf(a)*spawnR,-cosf(a)*spawnR,a+PI_F);
    }
    mapS=104.0f/(2.0f*arenaH); mapOx=8.0f+58.0f; mapOy=28.0f+56.0f;
}

static void setupRace(void)
{
    int k;
    ncars=MAXC; playerIdx=5;
    buildTrack(mapId-2);
    buildScenery();
    for(k=0;k<ncars;k++){
        int row=k/2, col=k%2, i=npts-1-row*2;           /* rows 2 road points (~14 m) apart so nobody sits in the camera */
        initCar(&cars[k],k,0.0f,0.0f,0.0f);
        placeOnTrack(&cars[k],i,col?2.6f:-2.6f);
        updateNearest(&cars[k],1);
    }
}

static void startGame(int map)
{
    mapId=map; gmode=(map<2)?GM_DERBY:GM_RACE;
    frameNo=0; finishCount=0; wrecks=0; shake=0.0f; hudTimer=0;
    memset(parts,0,sizeof(parts));
    if(gmode==GM_DERBY) setupDerby(); else setupRace();
    camH=cars[playerIdx].h;
    state=ST_COUNT; countT=180;
    sndEngineStop();
}

static void goMenu(void)
{
    state=ST_MENU; ncars=1; playerIdx=0; nobs=0; menuSpin=0.0f; menuScr=SC_MAIN;
    mapId=lastMap; gmode=(mapId<2)?GM_DERBY:GM_RACE;
    memset(parts,0,sizeof(parts));
    initCar(&cars[0],0,0.0f,0.0f,0.0f);
    hudTimer=0;
    sndEngineStop();
}

static void endGame(const char *l1,const char *l2)
{
    state=ST_END; endT=0; hudTimer=0;
    strncpy(endMsg1,l1,31); endMsg1[31]=0;
    strncpy(endMsg2,l2,31); endMsg2[31]=0;
}

static void emitCarEffects(Car *c)
{
    float spd=fabsf(c->spd);
    float bx=c->x-c->sn*1.9f, bz=c->z+c->cs*1.9f;          /* behind the car */
    if(!c->alive){
        if((frameNo&3)==0) spawnPart(PT_BLACK,c->x,0.7f,c->z,(rndf()-0.5f)*0.8f,1.4f,(rndf()-0.5f)*0.8f,1.3f);
        return;
    }
    if(spd>9.0f && (frameNo&3)==(int)(c->r&3))
        spawnPart(PT_DUST,bx,0.2f,bz,(rndf()-0.5f)*1.5f,0.8f,(rndf()-0.5f)*1.5f,0.7f);
    if(c->hp<45.0f && (frameNo&3)==0)
        spawnPart(c->hp<20.0f?PT_BLACK:PT_SMOKE,c->x+c->sn*1.3f,1.1f,c->z-c->cs*1.3f,(rndf()-0.5f)*0.5f,1.5f,(rndf()-0.5f)*0.5f,1.0f);
    if(c->nitroOn)
        spawnPart(PT_FLAME,bx-c->sn*0.2f,0.45f,bz+c->cs*0.2f,-c->sn*3.0f,0.0f,c->cs*3.0f,0.18f);
}

static void updatePlay(u32 held,u32 down)
{
    int i,j,k;
    for(k=0;k<ncars;k++){
        Car *c=&cars[k];
        float thr=0.0f,str=0.0f; int nit=0,hb=0,onRoad=1;
        if(gmode==GM_RACE){
            updateNearest(c,0);
            onRoad=(c->nearD<(HALFW+1.0f)*(HALFW+1.0f));
            if(c->lap>lapsN && !c->finished){ c->finished=1; c->finishRank=++finishCount; }
        }
        if(!c->alive){
            c->vx*=0.95f; c->vz*=0.95f; c->x+=c->vx*DT; c->z+=c->vz*DT; c->spd=0.0f;
            emitCarEffects(c);
            continue;
        }
        if(c->isPlayer && !AUTOPILOT){
            if(state==ST_PLAY){
                thr=(held&(KEY_A|KEY_UP))?1.0f:0.0f;
                if(held&(KEY_B|KEY_DOWN)) thr=-1.0f;
                str=(held&KEY_RIGHT)?1.0f:((held&KEY_LEFT)?-1.0f:0.0f);
                nit=(held&(KEY_Y|KEY_R))!=0;
                hb=(held&KEY_L)!=0;
                if((down&KEY_X)&&gmode==GM_RACE) placeOnTrack(c,c->idx,0.0f);
            }
        } else {
            aiThink(c,k,&thr,&str,&nit);
        }
        driveCar(c,thr,str,nit,hb,onRoad);
        emitCarEffects(c);
    }
    for(i=0;i<ncars;i++) for(j=i+1;j<ncars;j++) carVsCar(&cars[i],&cars[j]);
    for(i=0;i<ncars;i++){
        carVsObstacles(&cars[i]);
        if(gmode==GM_DERBY) carVsWalls(&cars[i]);
    }
    updateParts();

    if(state==ST_PLAY){
        Car *p=&cars[playerIdx];
        if(gmode==GM_DERBY){
            if(!p->alive) endGame("YOU ARE WRECKED!","Better luck next time");
            else if(countAlive()==1) endGame("YOU WIN!","Last car standing");
        } else {
            if(p->finished){
                char b[32]; siprintf(b,"FINISHED  P%d / %d",p->finishRank,ncars);
                endGame(p->finishRank==1?"YOU WIN!":"RACE FINISHED",b);
            } else if(!p->alive) endGame("ENGINE DEAD - DNF","You got wrecked");
        }
    }
}

/* ===== PART 2 (rendering, HUD, main loop) is appended below ===== */

/* ============================== TEXTURES =================================== */
static int texId[TX_COUNT], curTex=-1;
static int skyC[12], skyS[12];

static int szlog(int n){ int k=0; while((8<<k)<n) k++; return k; }   /* 8->0 ... 1024->7 */

static void loadTextures(void)
{
    int i;
    vramSetBankA(VRAM_A_TEXTURE);
    vramSetBankB(VRAM_B_TEXTURE);
    for(i=0;i<TX_COUNT;i++){
        glGenTextures(1,&texId[i]);
        glBindTexture(0,texId[i]);
        glTexImage2D(0,0,GL_RGBA,szlog(texDefs[i].w),szlog(texDefs[i].h),0,
                     TEXGEN_TEXCOORD|GL_TEXTURE_WRAP_S|GL_TEXTURE_WRAP_T,texDefs[i].data);
    }
    for(i=0;i<12;i++){
        float a=(float)i*TWOPI_F/12.0f;
        skyC[i]=(int)(cosf(a)*4096.0f); skyS[i]=(int)(sinf(a)*4096.0f);
    }
}

static inline void useTex(int t){ if(t!=curTex){ glBindTexture(0,texId[t]); curTex=t; } }
static inline void tv(int u,int v,int x,int y,int z){ GFX_TEX_COORD=TEXTURE_PACK(u,v); glVertex3v16(x,y,z); }

/* ================================ RENDER =================================== */
static const unsigned char faceIdx[5][4]={{4,5,7,6},{2,3,7,6},{0,1,5,4},{0,2,6,4},{1,3,7,5}};

/* car atlas UVs (12.4, 1/4 texel inset): top, front, back, left, right */
static const short uvBody[5][4][2]={
    {{3,1021},{509,1021},{509,515},{3,515}},
    {{515,637},{1021,637},{1021,515},{515,515}},
    {{515,765},{1021,765},{1021,643},{515,643}},
    {{3,253},{1021,253},{1021,3},{3,3}},
    {{3,253},{1021,253},{1021,3},{3,3}}
};
static const short uvCabin[5][4][2]={
    {{256,776},{256,776},{256,776},{256,776}},
    {{515,893},{1021,893},{1021,771},{515,771}},
    {{515,1021},{1021,1021},{1021,899},{515,899}},
    {{3,509},{1021,509},{1021,259},{3,259}},
    {{3,509},{1021,509},{1021,259},{3,259}}
};

static void drawSky(float cx,float cz)
{
    int k;
    useTex(themeSky[mapId]);
    glPushMatrix();
    mtxT(floattof32(cx),inttof32(22),floattof32(cz));
    mtxS(inttof32(100),inttof32(30),inttof32(100));
    glBegin(QUADS_MODE);
    glColor3b(255,255,255);
    for(k=0;k<12;k++){
        int k1=(k+1)%12, u0=k*4096/12, u1=(k+1)*4096/12;
        tv(u0,1010,skyC[k], -4096,skyS[k]);
        tv(u1,1010,skyC[k1],-4096,skyS[k1]);
        tv(u1,6,   skyC[k1], 4096,skyS[k1]);
        tv(u0,6,   skyC[k],  4096,skyS[k]);
    }
    glEnd();
    glPopMatrix(1);
}

static void drawGround(float cx,float cz)
{
    int ix0=(int)floorf(cx/24.0f), iz0=(int)floorf(cz/24.0f), i, j;
    const unsigned char *t=themeTint[mapId];
    useTex(themeGround[mapId]);
    for(j=iz0-4;j<=iz0+4;j++) for(i=ix0-4;i<=ix0+4;i++){
        int sh=(((i*7+j*13)&3)==0)?232:255;
        glPushMatrix();
        mtxT(inttof32(i*24+12),floattof32(-0.25f),inttof32(j*24+12));
        mtxS(inttof32(12),inttof32(1),inttof32(12));
        glBegin(QUADS_MODE);
        glColor3b(t[0]*sh/255,t[1]*sh/255,t[2]*sh/255);
        tv(0,0,-4096,0,-4096); tv(4096,0,4096,0,-4096);
        tv(4096,4096,4096,0,4096); tv(0,4096,-4096,0,4096);
        glEnd();
        glPopMatrix(1);
    }
}

static void drawRoadSeg(int i)
{
    const int *v=segV[i];
    glPushMatrix();
    mtxT(segTx[i],0,segTz[i]);
    glBegin(QUADS_MODE);
    glColor3b(255,255,255);
    tv(2,segV0[i],v[0],v[1],v[2]);   tv(1022,segV0[i],v[3],v[4],v[5]);
    tv(1022,segV1[i],v[6],v[7],v[8]); tv(2,segV1[i],v[9],v[10],v[11]);
    glEnd();
    glPopMatrix(1);
}

static void drawRoad(void)
{
    int i;
    useTex(mapId==2?TX_ROAD_DIRT:TX_ROAD_SAND);
    for(i=1;i<npts;i++){
        if(iabs(segCx[i]-camXi)>100||iabs(segCz[i]-camZi)>100) continue;
        drawRoadSeg(i);
    }
    useTex(TX_START);
    drawRoadSeg(0);
}

/* unit cube scaled by the current matrix, textured (5 faces, no bottom) */
static void unitBoxTex(int r,int g,int b,int ux,int uy,int uz)
{
    glBegin(QUADS_MODE);
    glColor3b(r,g,b);
    tv(0,0,-4096,4096,-4096);  tv(ux,0,4096,4096,-4096);
    tv(ux,uz,4096,4096,4096);  tv(0,uz,-4096,4096,4096);
    glColor3b(r*8/10,g*8/10,b*8/10);
    tv(0,uy,-4096,-4096,4096); tv(ux,uy,4096,-4096,4096);
    tv(ux,0,4096,4096,4096);   tv(0,0,-4096,4096,4096);
    tv(0,uy,-4096,-4096,-4096);tv(ux,uy,4096,-4096,-4096);
    tv(ux,0,4096,4096,-4096);  tv(0,0,-4096,4096,-4096);
    glColor3b(r*6/10,g*6/10,b*6/10);
    tv(0,uy,-4096,-4096,-4096);tv(uz,uy,-4096,-4096,4096);
    tv(uz,0,-4096,4096,4096);  tv(0,0,-4096,4096,-4096);
    tv(0,uy,4096,-4096,-4096); tv(uz,uy,4096,-4096,4096);
    tv(uz,0,4096,4096,4096);   tv(0,0,4096,4096,-4096);
    glEnd();
}

static void drawGantry(void)
{
    int gx=(int)(nrmX[0]*27034.0f), gz=(int)(nrmZ[0]*27034.0f);
    int fx=(int)(nrmZ[0]*164.0f), fz=(int)(-nrmX[0]*164.0f);      /* 4 cm along the track */
    int y0=K16(4.6f), y1=K16(6.0f), s;
    useTex(TX_BANNER);
    glPushMatrix();
    mtxT(floattof32(roadX[0]),0,floattof32(roadZ[0]));
    glBegin(QUADS_MODE);
    glColor3b(255,255,255);
    /* approach side (behind the line): text reads left-to-right */
    tv(4,250,-gx-fx,y0,-gz-fz); tv(1020,250,gx-fx,y0,gz-fz); tv(1020,4,gx-fx,y1,gz-fz); tv(4,4,-gx-fx,y1,-gz-fz);
    /* far side: mirrored texture so it also reads correctly */
    tv(1020,250,-gx+fx,y0,-gz+fz); tv(4,250,gx+fx,y0,gz+fz); tv(4,4,gx+fx,y1,gz+fz); tv(1020,4,-gx+fx,y1,-gz+fz);
    glEnd();
    glPopMatrix(1);
    useTex(TX_WALL);
    for(s=-1;s<=1;s+=2){
        glPushMatrix();
        mtxT(floattof32(roadX[0]+nrmX[0]*6.6f*(float)s),inttof32(3),floattof32(roadZ[0]+nrmZ[0]*6.6f*(float)s));
        mtxS(floattof32(0.3f),inttof32(3),floattof32(0.3f));
        unitBoxTex(255,255,255,UV(8),UV(32),UV(8));
        glPopMatrix(1);
    }
}

static void drawBillboards(int kind,int tex,int hw,int h)
{
    int i, tw=texDefs[tex].w*16, th=texDefs[tex].h*16;
    useTex(tex);
    for(i=0;i<nobs;i++){
        const Obs *o=&obs[i];
        int dx,dz,tr,tg,tb,x0,z0,ua,ub;
        if(o->kind!=kind) continue;
        dx=o->ix-camXi*16; dz=o->iz-camZi*16;
        if(dx*dx+dz*dz>(85*16)*(85*16)) continue;
        if(dx*camFxi+dz*camFzi< -6144) continue;
        if(o->var==0){ tr=255;tg=255;tb=255; }
        else if(o->var==1){ tr=228;tg=238;tb=214; if(kind==OB_ROCK){ tr=255;tg=196;tb=160; } }
        else { tr=255;tg=226;tb=176; }
        x0=(hw*camRxi)>>12; z0=(hw*camRzi)>>12;
        if(o->ix&1){ ua=tw-6; ub=6; } else { ua=6; ub=tw-6; }
        glPushMatrix();
        mtxT(o->tx,0,o->tz);
        mtxS(o->sc,o->sc,o->sc);
        glBegin(QUADS_MODE);
        glColor3b(tr,tg,tb);
        tv(ua,th-6,-x0,0,-z0); tv(ub,th-6,x0,0,z0); tv(ub,6,x0,h,z0); tv(ua,6,-x0,h,-z0);
        glEnd();
        glPopMatrix(1);
    }
}

static void drawArena(void)
{
    int i,kind;
    int e=floattof32(arenaH+0.5f), L=floattof32(arenaH+1.0f), hy=floattof32(1.25f);
    int T=floattof32(0.5f), H=floattof32(1.25f);
    int span=(int)((arenaH+1.0f)*2.0f*12.8f*16.0f);
    useTex(TX_WALL);
    glPushMatrix(); mtxT(0,hy,-e); mtxS(L,H,T); unitBoxTex(255,255,255,span,UV(32),UV(6)); glPopMatrix(1);
    glPushMatrix(); mtxT(0,hy, e); mtxS(L,H,T); unitBoxTex(255,255,255,span,UV(32),UV(6)); glPopMatrix(1);
    glPushMatrix(); mtxT(-e,hy,0); mtxS(T,H,L); unitBoxTex(230,230,230,UV(6),UV(32),span); glPopMatrix(1);
    glPushMatrix(); mtxT( e,hy,0); mtxS(T,H,L); unitBoxTex(230,230,230,UV(6),UV(32),span); glPopMatrix(1);
    for(kind=OB_PILLAR;kind<=OB_TIRES;kind++){
        int tex;
        if(kind==OB_ROCK) continue;
        tex=(kind==OB_PILLAR)?TX_PILLAR:(kind==OB_BARREL)?TX_BARREL:(kind==OB_BLOCK)?TX_BLOCK:TX_TIRES;
        useTex(tex);
        for(i=0;i<nobs;i++){
            const Obs *o=&obs[i];
            if(o->kind!=kind) continue;
            glPushMatrix();
            mtxT(o->tx,o->hy,o->tz);
            mtxS(o->hx,o->hy,o->hx);
            unitBoxTex(255,255,255,o->ux,o->uy,o->uz);
            glPopMatrix(1);
        }
    }
}

static void carShade(const Car *c,int *tr,int *tg,int *tb)
{
    if(c->alive){ int f=45+(int)(c->hp*0.55f); *tr=c->r*f/100; *tg=c->g*f/100; *tb=c->b*f/100; }
    else { *tr=*tg=*tb=44; }
}

static inline int m16(float m){ return (int)(m*4096.0f); }

/* flat-coloured box (used for extras); samples the plain-paint spot of the atlas */
static void partBox(int C,int S,int a0,int a1,int y0,int y1,int b0,int b1,int r,int g,int b)
{
    static const int shade[5]={100,85,70,60,60};
    int p[8][3],i,f,k;
    for(i=0;i<8;i++){ p[i][0]=(i&1)?a1:a0; p[i][1]=(i&4)?y1:y0; p[i][2]=(i&2)?b1:b0; }
    for(f=0;f<5;f++){
        glColor3b(r*shade[f]/100,g*shade[f]/100,b*shade[f]/100);
        for(k=0;k<4;k++){
            i=faceIdx[f][k];
            tv(256,776,(p[i][0]*C+p[i][2]*S)>>12,p[i][1],(p[i][0]*S-p[i][2]*C)>>12);
        }
    }
}

static void drawCarBody(const Car *c)
{
    const CarDef *d=&carDefs[c->model];
    int C=(int)(c->cs*4096.0f), S=(int)(c->sn*4096.0f);
    int W=m16(d->W), L=m16(d->L), Y0=m16(d->y0), YF=m16(d->yF), YR=m16(d->yR);
    int tr,tg,tb,i,f,k,p[8][3],q[8][3],dd,ybase,cyv;
    int cb0=m16(d->cb0), cb1=m16(d->cb1), ct0=m16(d->ct0), ct1=m16(d->ct1), cw0=m16(d->cw0), cw1=m16(d->cw1);
    float t;
    static const int shade[5]={100,85,70,60,60};
    carShade(c,&tr,&tg,&tb);

    for(i=0;i<8;i++){                                  /* hull: front and rear tops differ (wedge) */
        int sa=(i&1)?1:-1, sb=(i&2)?1:-1, a=sa*W, b=sb*L, y=Y0;
        if(i&4){
            int de=c->def[i&3], top=(i&2)?YF:YR;
            a=sa*(W-(de>>2)); b=sb*(L-(de>>2)); y=top-de;
            if(y<Y0+500) y=Y0+500;
        }
        p[i][0]=a; p[i][1]=y; p[i][2]=b;
    }
    t=((d->cb0+d->cb1)*0.5f+d->L)/(2.0f*d->L);        /* where the cabin sits on the hull */
    ybase=YR+(int)((float)(YF-YR)*t);
    dd=(c->def[0]+c->def[1]+c->def[2]+c->def[3])>>3;
    cyv=m16(d->cy)-dd;
    for(i=0;i<8;i++){
        int sa=(i&1)?1:-1, top=(i&4)!=0, front=(i&2)!=0;
        q[i][0]=sa*(top?cw1:cw0);
        q[i][1]=top?cyv:ybase;
        q[i][2]=front?(top?ct1:cb1):(top?ct0:cb0);
    }

    glPushMatrix();
    mtxT(floattof32(c->x),0,floattof32(c->z));
    mtxS(floattof32(CAR_SCALE),floattof32(CAR_SCALE),floattof32(CAR_SCALE));
    glBegin(QUADS_MODE);
    for(f=0;f<5;f++){
        int cr,cg,cb;
        if(f==1||f==2){ cr=(tr+255)/2*shade[f]/100; cg=(tg+255)/2*shade[f]/100; cb=(tb+255)/2*shade[f]/100; }
        else          { cr=tr*shade[f]/100; cg=tg*shade[f]/100; cb=tb*shade[f]/100; }
        glColor3b(cr,cg,cb);
        for(k=0;k<4;k++){
            i=faceIdx[f][k];
            tv(uvBody[f][k][0],uvBody[f][k][1],(p[i][0]*C+p[i][2]*S)>>12,p[i][1],(p[i][0]*S-p[i][2]*C)>>12);
        }
    }
    for(f=0;f<5;f++){
        int cr,cg,cb;
        if(f==0){ cr=tr*9/10; cg=tg*9/10; cb=tb*9/10; }
        else    { cr=(150+tr*100/255)*shade[f]/100; cg=(150+tg*100/255)*shade[f]/100; cb=(150+tb*100/255)*shade[f]/100; }
        glColor3b(cr,cg,cb);
        for(k=0;k<4;k++){
            i=faceIdx[f][k];
            tv(uvCabin[f][k][0],uvCabin[f][k][1],(q[i][0]*C+q[i][2]*S)>>12,q[i][1],(q[i][0]*S-q[i][2]*C)>>12);
        }
    }
    /* ---- extras ---- */
    if(c->alive){
        int xt=d->xt;
        if(xt&X_PIPES){                                /* two exhaust stacks through the bonnet */
            int sb=m16(d->cb1+0.30f), sh=m16(0.34f);
            partBox(C,S,m16(0.24f),m16(0.34f),YF,YF+sh,sb,sb+m16(0.10f),178,178,184);
            partBox(C,S,-m16(0.34f),-m16(0.24f),YF,YF+sh,sb,sb+m16(0.10f),178,178,184);
        }
        if(xt&X_SCOOP){
            int sb=m16(d->cb1+0.10f);
            partBox(C,S,-m16(0.17f),m16(0.17f),YF,YF+m16(0.13f),sb,sb+m16(0.45f),36,36,40);
        }
        if(xt&X_WING){
            int b0=-L+m16(0.02f), hh2=m16(0.36f);
            partBox(C,S,-m16(0.42f),-m16(0.34f),YR,YR+hh2,b0,b0+m16(0.08f),tr,tg,tb);
            partBox(C,S, m16(0.34f), m16(0.42f),YR,YR+hh2,b0,b0+m16(0.08f),tr,tg,tb);
            partBox(C,S,-W,W,YR+hh2,YR+hh2+m16(0.06f),b0-m16(0.02f),b0+m16(0.30f),30,30,34);
        }
        if(xt&X_FINS){
            int b0=-L;
            partBox(C,S,W-m16(0.22f),W-m16(0.12f),YR,YR+m16(0.62f),b0,b0+m16(0.62f),tr*8/10,tg*8/10,tb*8/10);
            partBox(C,S,-W+m16(0.12f),-W+m16(0.22f),YR,YR+m16(0.62f),b0,b0+m16(0.62f),tr*8/10,tg*8/10,tb*8/10);
        }
        if(xt&X_BAR) partBox(C,S,-W+m16(0.04f),W-m16(0.04f),m16(0.30f),m16(0.66f),L,L+m16(0.10f),44,44,48);
        if(xt&X_SPLIT) partBox(C,S,-W,W,m16(0.10f),m16(0.20f),L-m16(0.05f),L+m16(0.20f),28,28,32);
        if(xt&X_BED){                                  /* walls round the cargo bed */
            int bb0=-L, bb1=cb0-m16(0.04f), wh2=m16(0.26f), th=m16(0.08f);
            partBox(C,S,-W,-W+th,YR,YR+wh2,bb0,bb1,tr*9/10,tg*9/10,tb*9/10);
            partBox(C,S, W-th,W,YR,YR+wh2,bb0,bb1,tr*9/10,tg*9/10,tb*9/10);
            partBox(C,S,-W,W,YR,YR+wh2,bb0,bb0+th,tr*9/10,tg*9/10,tb*9/10);
            partBox(C,S,-W+th,W-th,YR,YR+m16(0.02f),bb0+th,bb1,52,46,40);
        }
        if(xt&X_CRATE) partBox(C,S,-m16(0.50f),m16(0.50f),YR,YR+m16(0.55f),-L+m16(0.35f),cb0-m16(0.25f),150,108,58);
    }
    glEnd();
    glPopMatrix(1);
}

/* pass 0: outer wheel faces (rim texture)   pass 1: tread faces (black texture) */
static void drawWheels(const Car *c,int pass)
{
    const CarDef *d=&carDefs[c->model];
    int C=(int)(c->cs*4096.0f), S=(int)(c->sn*4096.0f);
    int sA=(int)(c->steer*0.4f*4096.0f), cA=4096-((sA*sA)>>13);
    int s,t,i, hw=m16(0.12f), hl=m16(d->wh), hy=m16(d->wy);
    glPushMatrix();
    mtxT(floattof32(c->x),0,floattof32(c->z));
    mtxS(floattof32(CAR_SCALE),floattof32(CAR_SCALE),floattof32(CAR_SCALE));
    glBegin(QUADS_MODE);
    glColor3b(255,255,255);
    for(s=-1;s<=1;s+=2) for(t=-1;t<=1;t+=2){
        int w[8][3], ca=s*(m16(d->W)+m16(0.10f)), cb=t*m16(d->wb);
        int sa2=(t>0)?sA:0, ca2=(t>0)?cA:4096;
        for(i=0;i<8;i++){
            int da=(i&1)?hw:-hw, db=(i&2)?hl:-hl;
            int ra=(da*ca2+db*sa2)>>12, rb=(-da*sa2+db*ca2)>>12;
            w[i][0]=ca+ra; w[i][1]=(i&4)?hy:0; w[i][2]=cb+rb;
        }
        if(pass==0){
            const unsigned char *fi=faceIdx[(s>0)?4:3];
            static const short uvw[4][2]={{3,253},{253,253},{253,3},{3,3}};
            int k;
            for(k=0;k<4;k++) tv(uvw[k][0],uvw[k][1],(w[fi[k]][0]*C+w[fi[k]][2]*S)>>12,w[fi[k]][1],(w[fi[k]][0]*S-w[fi[k]][2]*C)>>12);
        } else {
            int f,k;
            for(f=0;f<3;f++){
                const unsigned char *fi=faceIdx[f];
                for(k=0;k<4;k++) tv(6,6,(w[fi[k]][0]*C+w[fi[k]][2]*S)>>12,w[fi[k]][1],(w[fi[k]][0]*S-w[fi[k]][2]*C)>>12);
            }
        }
    }
    glEnd();
    glPopMatrix(1);
}

static void drawShadow(const Car *c,int k)
{
    const CarDef *d=&carDefs[c->model];
    int C=(int)(c->cs*4096.0f), S=(int)(c->sn*4096.0f);
    int a=m16(d->W+0.22f), b=m16(d->L+0.18f), y=K16(0.08f);
    glPolyFmt(POLY_ALPHA(13)|POLY_CULL_NONE|POLY_ID(1+k));
    glPushMatrix();
    mtxT(floattof32(c->x),0,floattof32(c->z));
    mtxS(floattof32(CAR_SCALE),floattof32(CAR_SCALE),floattof32(CAR_SCALE));
    glBegin(QUADS_MODE);
    glColor3b(0,0,0);
    tv(6,6,(-a*C-b*S)>>12,y,(-a*S+b*C)>>12);
    tv(6,6,( a*C-b*S)>>12,y,( a*S+b*C)>>12);
    tv(6,6,( a*C+b*S)>>12,y,( a*S-b*C)>>12);
    tv(6,6,(-a*C+b*S)>>12,y,(-a*S-b*C)>>12);
    glEnd();
    glPopMatrix(1);
}

static void drawParticles(void)
{
    int pass,i;
    for(pass=0;pass<2;pass++){
        useTex(pass==0?TX_SMOKE:TX_SPARK);
        for(i=0;i<MAXPART;i++){
            const Part *p=&parts[i];
            float t,sz;
            int a,tr,tg,tb,szv,x0,z0,tw;
            if(p->life<=0.0f) continue;
            if(((p->type==PT_SMOKE)||(p->type==PT_DUST)||(p->type==PT_BLACK))!=(pass==0)) continue;
            t=p->life/p->max;
            switch(p->type){
                case PT_SMOKE: sz=0.45f+(1.0f-t)*1.1f; a=2+(int)(t*12.0f); tr=tg=tb=190; break;
                case PT_BLACK: sz=0.55f+(1.0f-t)*1.2f; a=3+(int)(t*13.0f); tr=tg=50; tb=52; break;
                case PT_DUST:  sz=0.35f+(1.0f-t)*0.9f; a=2+(int)(t*8.0f);  tr=230; tg=200; tb=150; break;
                case PT_FLAME: sz=0.12f+0.38f*t;       a=31;               tr=255; tg=150; tb=50; break;
                default:       sz=0.16f;               a=31;               tr=tg=tb=255; break;
            }
            szv=(int)(sz*4096.0f);
            x0=(szv*camRxi)>>12; z0=(szv*camRzi)>>12;
            tw=(pass==0)?508:252;
            glPolyFmt(POLY_ALPHA(a)|POLY_CULL_NONE|POLY_ID(8+(i%50)));
            glPushMatrix();
            mtxT(floattof32(p->x),floattof32(p->y),floattof32(p->z));
            glBegin(QUADS_MODE);
            glColor3b(tr,tg,tb);
            tv(4,tw,-x0,-szv,-z0); tv(tw,tw,x0,-szv,z0); tv(tw,4,x0,szv,z0); tv(4,4,-x0,szv,-z0);
            glEnd();
            glPopMatrix(1);
        }
    }
}

/* indoor garage for the car-select screen: concrete floor + wall ring */
static void drawGarage(void)
{
    int k;
    useTex(TX_CONCRETE);
    glPushMatrix();
    mtxS(inttof32(16),inttof32(1),inttof32(16));
    glBegin(QUADS_MODE);
    glColor3b(214,214,220);
    tv(0,0,-4096,0,-4096); tv(5461,0,4096,0,-4096); tv(5461,5461,4096,0,4096); tv(0,5461,-4096,0,4096);
    glEnd();
    glPopMatrix(1);
    useTex(TX_CONCRETE);                         /* plain concrete walls ... */
    glPushMatrix();
    mtxT(0,inttof32(6),0);
    mtxS(inttof32(14),inttof32(7),inttof32(14));
    glBegin(QUADS_MODE);
    glColor3b(172,172,180);
    for(k=0;k<12;k++){
        int k1=(k+1)%12, u0=k*1250, u1=(k+1)*1250;
        tv(u0,2400,skyC[k], -4096,skyS[k]);
        tv(u1,2400,skyC[k1],-4096,skyS[k1]);
        tv(u1,6,   skyC[k1], 4096,skyS[k1]);
        tv(u0,6,   skyC[k],  4096,skyS[k]);
    }
    glEnd();
    glPopMatrix(1);
    useTex(TX_WALL);                             /* ... with one hazard-stripe skirting */
    glPushMatrix();
    mtxT(0,floattof32(0.55f),0);
    mtxS(floattof32(13.9f),floattof32(0.45f),floattof32(13.9f));
    glBegin(QUADS_MODE);
    glColor3b(235,235,240);
    for(k=0;k<12;k++){
        int k1=(k+1)%12, u0=k*1536, u1=(k+1)*1536;
        tv(u0,136,skyC[k], -4096,skyS[k]);
        tv(u1,136,skyC[k1],-4096,skyS[k1]);
        tv(u1,8,  skyC[k1], 4096,skyS[k1]);
        tv(u0,8,  skyC[k],  4096,skyS[k]);
    }
    glEnd();
    glPopMatrix(1);
}

static void render(void)
{
    float ex,ey,ez,lx,ly,lz,fdx,fdz,fl;
    int k;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(62.0f,256.0f/192.0f,0.4f,115.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    if(state==ST_MENU){
        if(menuScr==SC_CAR){ ex=4.4f; ey=1.7f; ez=5.8f; lx=0.3f; ly=0.9f; lz=0.0f; }
        else               { ex=2.8f; ey=2.1f; ez=6.6f; lx=0.0f; ly=0.8f; lz=0.0f; }
    } else {
        Car *p=&cars[playerIdx];
        float s,c;
        camH+=wrapa(p->h-camH)*0.1f;
        s=sinf(camH); c=cosf(camH);
        ex=p->x-s*6.0f; ez=p->z+c*6.0f; ey=2.5f+((frameNo&1)?shake:-shake)*0.5f;
        if(gmode==GM_DERBY){ ex=clampf(ex,-(arenaH-1.5f),arenaH-1.5f); ez=clampf(ez,-(arenaH-1.5f),arenaH-1.5f); }
        lx=p->x+s*5.0f; lz=p->z-c*5.0f; ly=0.9f;
        shake*=0.9f;
    }
#ifdef HOST_TEST
    if(dbgCamOn){ ex=dbgCam[0]; ey=dbgCam[1]; ez=dbgCam[2]; lx=dbgCam[3]; ly=dbgCam[4]; lz=dbgCam[5]; }
#endif
    gluLookAt(ex,ey,ez, lx,ly,lz, 0.0f,1.0f,0.0f);

    fdx=lx-ex; fdz=lz-ez; fl=sqrtf(fdx*fdx+fdz*fdz);
    if(fl<0.001f) fl=1.0f;
    fdx/=fl; fdz/=fl;
    camXi=(int)ex; camZi=(int)ez;
    camFxi=(int)(fdx*64.0f); camFzi=(int)(fdz*64.0f);
    camRxi=(int)(-fdz*4096.0f); camRzi=(int)(fdx*4096.0f);     /* right = (-fz, fx) */

    curTex=-1;
    glPolyFmt(POLY_ALPHA(31)|POLY_CULL_NONE);
    if(state==ST_MENU && menuScr==SC_CAR) drawGarage();
    else { drawSky(ex,ez); drawGround(ex,ez); }
    if(state!=ST_MENU){
        if(gmode==GM_RACE){
            drawRoad();
            drawGantry();
            drawBillboards(OB_TREE,mapId==3?TX_CACTUS:TX_PINE,mapId==3?K16(1.125f):K16(1.75f),mapId==3?K16(4.5f):K16(7.0f));
            if(mapId==3) drawBillboards(OB_ROCK,TX_ROCK,K16(1.6f),K16(1.6f));
        } else drawArena();
    }
    for(k=0;k<2;k++){                       /* 0 = clean race paint, 1 = rusty derby paint */
        int j;
        useTex(k?TX_CARD:TX_CAR);
        for(j=0;j<ncars;j++) if((carDefs[cars[j].model].cls==0)==(k==1)) drawCarBody(&cars[j]);
    }
    useTex(TX_WHEEL);
    for(k=0;k<ncars;k++) drawWheels(&cars[k],0);
    useTex(TX_BLACK);
    for(k=0;k<ncars;k++) drawWheels(&cars[k],1);
    useTex(TX_WHITE);
    for(k=0;k<ncars;k++) drawShadow(&cars[k],k);
    drawParticles();
}

/* ============================= HUD + MENUS ================================= */
/* Bottom screen = 256x192 16-bit bitmap, drawn into RAM, DMA-copied ~10x/s.
   Look: hazard-stripe bars, sepia backdrop, italic type with drop shadow.     */
#define HUDW 256
#define HUDH 192
static u16 hudBuf[HUDW*HUDH] __attribute__((aligned(4)));
static u16 menuBg[HUDW*HUDH] __attribute__((aligned(4)));
static u16 barTop[HUDW*24]   __attribute__((aligned(4)));
static u16 barBot[HUDW*22]   __attribute__((aligned(4)));
static u16 *hudFb;
static float trackLen[2];

#define C15(r,g,b) ARGB16(1,(r)>>3,(g)>>3,(b)>>3)
#define COL_WHITE  C15(244,244,244)
#define COL_CREAM  C15(240,230,196)
#define COL_GRAY   C15(166,154,132)
#define COL_YEL    C15(244,190,30)
#define COL_ORANGE C15(255,150,30)
#define COL_GREEN  C15(100,220,90)
#define COL_RED    C15(235,70,60)
#define COL_BLUE   C15(80,150,255)
#define COL_DARK   C15(20,15,8)
#define COL_PANEL  C15(40,32,18)
#define COL_EDGE   C15(150,105,22)

static inline int cb(int v){ return v<0?0:(v>255?255:v); }
static unsigned hh(int x,int y)
{
    unsigned v=(unsigned)x*374761393u+(unsigned)y*668265263u;
    v=(v^(v>>13))*1274126177u;
    return v^(v>>16);
}

static u16 hazardPx(int x,int y,int h,int inner)
{
    int ph=x-iabs(2*y-h)*7/10+400;           /* chevrons pointing right */
    int n=(int)(hh(x,y)&31), r,g,b;
    if((ph/12)&1){ r=236-n/2; g=182-n/2; b=22; }
    else         { r=26+n/4;  g=24+n/4;  b=22+n/4; }
    if(hh(x/3,y)%61==0){ r+=34; g+=30; b+=20; }          /* scratches */
    if(y==inner){ r=214; g=114; b=20; }                   /* orange rim */
    else if(iabs(y-inner)==1){ r=r*5/10; g=g*5/10; b=b*5/10; }
    return C15(cb(r),cb(g),cb(b));
}

static void initHudArt(void)
{
    int x,y;
    for(y=0;y<HUDH;y++) for(x=0;x<HUDW;x++){
        int r=64-y*42/HUDH, g=50-y*33/HUDH, b=34-y*23/HUDH;
        int sh=60-iabs(x+y*3/5-80);
        int n=(int)(hh(x,y)&7)-3;
        if(sh<0) sh=0;
        sh=sh*(HUDH-y)/HUDH*3/5;                          /* soft light shaft */
        menuBg[y*HUDW+x]=C15(cb(r+sh+n),cb(g+sh*9/10+n),cb(b+sh*7/10+n));
    }
    for(y=0;y<24;y++) for(x=0;x<HUDW;x++) barTop[y*HUDW+x]=hazardPx(x,y,24,23);
    for(y=0;y<22;y++) for(x=0;x<HUDW;x++) barBot[y*HUDW+x]=hazardPx(x,y,22,0);
}

static void initMenuData(void)
{
    initHudArt();
    buildTrack(0); trackLen[0]=trackTotal;
    buildTrack(1); trackLen[1]=trackTotal;
}

/* ---- drawing primitives ---- */
static inline void hpx(int x,int y,u16 c){ if((unsigned)x<HUDW && (unsigned)y<HUDH) hudBuf[y*HUDW+x]=c; }
static void hrect(int x,int y,int w,int h,u16 c)
{
    int i,j;
    for(j=y;j<y+h;j++){
        if((unsigned)j>=HUDH) continue;
        for(i=x;i<x+w;i++) if((unsigned)i<HUDW) hudBuf[j*HUDW+i]=c;
    }
}
static void hframe(int x,int y,int w,int h,u16 c)
{
    hrect(x,y,w,1,c); hrect(x,y+h-1,w,1,c); hrect(x,y,1,h,c); hrect(x+w-1,y,1,h,c);
}
static void hline(int x0,int y0,int x1,int y1,u16 c)
{
    int dx=iabs(x1-x0), dy=-iabs(y1-y0), sx=x0<x1?1:-1, sy=y0<y1?1:-1, err=dx+dy, e2;
    for(;;){
        hpx(x0,y0,c);
        if(x0==x1&&y0==y1) break;
        e2=2*err;
        if(e2>=dy){ err+=dy; x0+=sx; }
        if(e2<=dx){ err+=dx; y0+=sy; }
    }
}
static void hdisc(int cx,int cy,int r,u16 c)
{
    int i,j;
    for(j=-r;j<=r;j++) for(i=-r;i<=r;i++) if(i*i+j*j<=r*r+r) hpx(cx+i,cy+j,c);
}
static void hcopy(const u16 *src,int y,int rows){ memcpy(&hudBuf[y*HUDW],src,(size_t)rows*HUDW*2); }

static void htextEx(int x,int y,const char *s,u16 c,int sc,int ital)
{
    for(;*s;s++,x+=8*sc){
        int ch=(unsigned char)*s, r,b,i,j;
        const unsigned char *g;
        if(ch<32||ch>126) ch='?';
        g=&font8x8[(ch-32)*8];
        for(r=0;r<8;r++){
            int bits=g[r], sh=ital?((7-r)*sc)/3:0;
            if(!bits) continue;
            for(b=0;b<8;b++) if(bits&(0x80>>b))
                for(j=0;j<sc;j++) for(i=0;i<sc;i++) hpx(x+sh+b*sc+i,y+r*sc+j,c);
        }
    }
}
static void htext(int x,int y,const char *s,u16 c,int sc){ htextEx(x,y,s,c,sc,0); }
static void htextS(int x,int y,const char *s,u16 c,int sc){ htextEx(x+sc,y+sc,s,C15(8,6,2),sc,1); htextEx(x,y,s,c,sc,1); }
static int  htextW(const char *s,int sc){ return (int)strlen(s)*8*sc; }
static void htextR(int xr,int y,const char *s,u16 c,int sc){ htextS(xr-htextW(s,sc),y,s,c,sc); }
static void htextC(int cx,int y,const char *s,u16 c,int sc){ htextS(cx-htextW(s,sc)/2,y,s,c,sc); }

static void hbar(int x,int y,int w,int h,int val,u16 full,u16 back)
{
    int f=val*(w-2)/100;
    if(f<0) f=0;
    if(f>w-2) f=w-2;
    hrect(x,y,w,h,back);
    hrect(x+1,y+1,f,h-2,full);
    hframe(x,y,w,h,COL_EDGE);
}

static void htri(int x,int y,int dir,u16 c)      /* small triangle, dir -1 = points left */
{
    int i;
    for(i=0;i<5;i++){
        if(dir<0) hrect(x+i,y-i,1,2*i+1,c);
        else      hrect(x+i,y-(4-i),1,2*(4-i)+1,c);
    }
}

/* ---- Flatout-style furniture ---- */
static void hHeader(const char *t)
{
    int w=htextW(t,1)+14;
    hcopy(barTop,0,24);
    hrect(6,5,w,14,COL_DARK); hframe(6,5,w,14,C15(120,80,10));
    htextS(13,8,t,COL_CREAM,1);
}
static void hHint(int x,int y,char key,const char *txt)
{
    char k[2]; k[0]=key; k[1]=0;
    hdisc(x+5,y+4,5,COL_YEL);
    htext(x+2,y,k,COL_DARK,1);
    htextS(x+15,y,txt,COL_WHITE,1);
}
static void hFooter(const char *ok,const char *back,char backKey)
{
    int x2=250-(htextW(back,1)+16), x1=x2-14-(htextW(ok,1)+16);
    hcopy(barBot,170,22);
    hrect(x1-5,173,252-(x1-5),16,COL_DARK); hframe(x1-5,173,252-(x1-5),16,C15(120,80,10));
    hHint(x1,177,'A',ok);
    hHint(x2,177,backKey,back);
}

static void hIcon(int kind,int cx,int cy,u16 c,u16 hole)
{
    int i;
    if(kind==0){                                   /* chequered flag */
        int r,k;
        hrect(cx-8,cy-10,2,21,c);
        for(r=0;r<3;r++) for(k=0;k<4;k++) if(((r+k)&1)==0) hrect(cx-6+k*4,cy-10+r*4,4,4,c);
    } else if(kind==1){                            /* crash burst */
        static const int sx[8]={0,8,12,8,0,-8,-12,-8}, sy[8]={-12,-8,0,8,12,8,0,-8};
        hdisc(cx,cy,6,c);
        for(i=0;i<8;i++){
            hline(cx-1,cy,cx+sx[i],cy+sy[i],c); hline(cx+1,cy,cx+sx[i],cy+sy[i],c);
            hline(cx,cy-1,cx+sx[i],cy+sy[i],c); hline(cx,cy+1,cx+sx[i],cy+sy[i],c);
        }
        hdisc(cx,cy,2,hole);
    } else if(kind==2){                            /* car */
        hrect(cx-11,cy-1,22,6,c); hrect(cx-6,cy-6,12,5,c);
        hline(cx-6,cy-6,cx-9,cy-1,c); hline(cx+6,cy-6,cx+9,cy-1,c);
        hrect(cx-4,cy-5,3,3,hole); hrect(cx+1,cy-5,3,3,hole);
        hdisc(cx-6,cy+6,3,c); hdisc(cx+6,cy+6,3,c);
        hdisc(cx-6,cy+6,1,hole); hdisc(cx+6,cy+6,1,hole);
    } else {                                       /* gear */
        static const int px[8]={0,6,9,6,0,-6,-9,-6}, py[8]={-9,-6,0,6,9,6,0,-6};
        hdisc(cx,cy,7,c);
        for(i=0;i<8;i++) hrect(cx+px[i]-2,cy+py[i]-2,4,4,c);
        hdisc(cx,cy,3,hole);
    }
}
static void hButton(int x,int y,int sel,int kind)
{
    if(sel){
        hrect(x-3,y-3,40,40,C15(255,214,110));
        hrect(x-1,y-1,36,36,COL_WHITE);
        hrect(x,y,34,34,C15(240,230,196));
        hrect(x+1,y+1,32,3,C15(255,252,235));
        hIcon(kind,x+17,y+17,C15(46,34,14),C15(240,230,196));
    } else {
        hrect(x,y,34,34,COL_DARK);
        hframe(x,y,34,34,C15(226,166,22));
        hframe(x+1,y+1,32,32,C15(110,72,10));
        hIcon(kind,x+17,y+17,C15(244,190,30),COL_DARK);
    }
}
static void hCarSil(int cx,int cy,int w,const CarDef *d,u16 col,u16 dark)
{
    float k=(float)w/(2.0f*d->L+0.5f);
    int gy=cy+(int)(0.9f*k), r, x0, x1, ytop, ybot, i;
    float topAvg=(d->yF+d->yR)*0.5f;
    u16 hub=C15(180,180,186);
    #define PX(b) (cx+(int)((b)*k))
    #define PY(y) (gy-(int)((y)*k))
    /* hull */
    hrect(PX(-d->L),PY(topAvg),(int)(2.0f*d->L*k)+1,(int)((topAvg-d->y0)*k)+1,col);
    /* cabin trapezoid */
    ytop=PY(d->cy); ybot=PY(topAvg);
    for(r=ytop;r<=ybot;r++){
        float f=(ybot>ytop)?(float)(r-ytop)/(float)(ybot-ytop):1.0f;
        x0=PX(d->ct0+(d->cb0-d->ct0)*f); x1=PX(d->ct1+(d->cb1-d->ct1)*f);
        hrect(x0,r,x1-x0+1,1,col);
        if(w>=30 && r>ytop && r<ybot-1) hrect(x0+2,r,x1-x0-3>0?x1-x0-3:1,1,dark);
    }
    if(d->xt&X_WING) hrect(PX(-d->L)-1,PY(d->yR+0.4f),(int)(0.45f*k)+2,2,dark);
    if(d->xt&X_FINS) hrect(PX(-d->L),PY(d->yR+0.62f),(int)(0.62f*k)+1,(int)(0.62f*k)+1,col);
    if(d->xt&X_BED){
        int bw=(int)((d->cb0+d->L)*k), bh=(int)(0.26f*k)+1;
        hrect(PX(-d->L),PY(d->yR+0.26f),bw,bh,col);
        if(w>=30) hrect(PX(-d->L)+1,PY(d->yR+0.26f),bw-2>0?bw-2:1,bh-1>0?bh-1:1,dark);
        if(d->xt&X_CRATE) hrect(PX(-d->L+0.35f),PY(d->yR+0.55f),(int)((d->cb0+d->L-0.6f)*k),(int)(0.55f*k),C15(150,108,58));
    }
    if(d->xt&X_PIPES) for(i=0;i<2;i++) hrect(PX(d->cb1+0.3f)+i*2,PY(d->yF+0.34f),1+(w>=60),(int)(0.34f*k)+1,hub);
    if(d->xt&X_BAR) hrect(PX(d->L),PY(0.66f),(int)(0.12f*k)+1,(int)(0.36f*k)+1,dark);
    /* wheels */
    for(i=-1;i<=1;i+=2){
        int wr=(int)(d->wh*k); if(wr<2) wr=2;
        hdisc(PX(i*d->wb),gy-wr,wr,dark); hdisc(PX(i*d->wb),gy-wr,wr/2,hub);
    }
    #undef PX
    #undef PY
}
static void hTrackThumb(int which,int x,int y,int w,int h,u16 c)
{
    const TrackDef *td=&trackDefs[which];
    int n=td->n,k,s,a,have=0,px=0,py=0,fx=0,fy=0;
    float mnx=1e9f,mxx=-1e9f,mnz=1e9f,mxz=-1e9f,sc,sc2;
    for(k=0;k<n;k++){
        if(td->p[k][0]<mnx)mnx=td->p[k][0];
        if(td->p[k][0]>mxx)mxx=td->p[k][0];
        if(td->p[k][1]<mnz)mnz=td->p[k][1];
        if(td->p[k][1]>mxz)mxz=td->p[k][1];
    }
    sc=(float)(w-8)/(mxx-mnx); sc2=(float)(h-8)/(mxz-mnz);
    if(sc2<sc) sc=sc2;
    for(k=0;k<n;k++){
        const float *p0=td->p[(k+n-1)%n], *p1=td->p[k], *p2=td->p[(k+1)%n], *p3=td->p[(k+2)%n];
        for(s=0;s<4;s++){
            float t=(float)s/4.0f, t2=t*t, t3=t2*t, q[2];
            int ix,iy;
            for(a=0;a<2;a++)
                q[a]=0.5f*((2.0f*p1[a])+(-p0[a]+p2[a])*t+(2.0f*p0[a]-5.0f*p1[a]+4.0f*p2[a]-p3[a])*t2
                    +(-p0[a]+3.0f*p1[a]-3.0f*p2[a]+p3[a])*t3);
            ix=x+w/2+(int)((q[0]-(mnx+mxx)*0.5f)*sc); iy=y+h/2+(int)((q[1]-(mnz+mxz)*0.5f)*sc);
            if(have){ hline(px,py,ix,iy,c); hline(px+1,py,ix+1,iy,c); } else { fx=ix; fy=iy; }
            px=ix; py=iy; have=1;
        }
    }
    hline(px,py,fx,fy,c); hline(px+1,py,fx+1,fy,c);
    hrect(fx-2,fy-2,5,5,COL_WHITE);
}
static void hArenaThumb(int map,int x,int y,int w,int h)
{
    int i,half,sq;
    float sc;
    buildArena(map);
    sc=(float)(((w<h)?w:h)-8)/(2.0f*arenaH);
    half=(int)(arenaH*sc); sq=2*half;
    hrect(x+w/2-half,y+h/2-half,sq,sq,C15(104,84,56));
    hframe(x+w/2-half,y+h/2-half,sq,sq,COL_GRAY);
    for(i=0;i<nobs;i++){
        int rr=(int)(obs[i].r*sc); if(rr<1) rr=1;
        hdisc(x+w/2+(int)(obs[i].x*sc),y+h/2+(int)(obs[i].z*sc),rr,C15(176,174,182));
    }
    nobs=0;
}

/* ============================== MENU SCREENS =============================== */
static const char *mainName[4]={"RALLY","DERBY","GARAGE","OPTIONS"};

static void hudMain(void)
{
    int i,w; char b[48];
    hcopy(menuBg,0,HUDH);
    hHeader("MAIN MENU");
    htextC(128,31,"RUMBLE FOREST",COL_ORANGE,2);
    htextC(128,51,"SMASH CARS   WIN RACES",COL_GRAY,1);
    for(i=0;i<4;i++) hButton(33+i*52,80,i==mainSel,i);
    w=htextW(mainName[mainSel],2);
    htextC(128,126,mainName[mainSel],COL_WHITE,2);
    htri(128-w/2-14,134,-1,COL_YEL); htri(128+w/2+9,134,1,COL_YEL);
    switch(mainSel){
        case 0: siprintf(b,"%d LAPS ON DIRT TRACKS",lapsN); break;
        case 1: siprintf(b,"LAST CAR RUNNING WINS"); break;
        case 2: siprintf(b,"PICK YOUR RIDE"); break;
        default: siprintf(b,"AI, LAPS AND SOUND"); break;
    }
    htextC(128,146,b,COL_CREAM,1);
    siprintf(b,"YOUR CAR  %s",carDefs[carSel].name);
    htextC(128,158,b,COL_YEL,1);
    hFooter("SELECT","QUICK START",'S');
}

static void hInfoRow(int y,const char *label,const char *val)
{
    htextS(18,y,label,COL_WHITE,1);
    htextR(238,y,val,COL_YEL,1);
    hline(14,y+10,242,y+10,C15(96,78,46));
}

static void hudMaps(void)
{
    int i,race=(modeSel==0); char b[32];
    hcopy(menuBg,0,HUDH);
    hHeader(race?"SELECT TRACK":"SELECT ARENA");
    for(i=0;i<2;i++){
        int x=i?134:10, y=32, sel=(i==mapSel);
        hrect(x-3,y-3,118,82,sel?COL_YEL:C15(84,66,32));
        hrect(x-1,y-1,114,78,C15(24,19,11));
        hrect(x+4,y+4,104,52,race?C15(34,62,34):C15(40,34,24));
        if(race) hTrackThumb(i,x+4,y+4,104,52,C15(200,178,140));
        else     hArenaThumb(i,x+4,y+4,104,52);
        htextS(x+8,y+63,mapName[race?2+i:i],sel?COL_YEL:COL_CREAM,1);
    }
    if(race){
        hInfoRow(118,"TRACK",mapName[2+mapSel]);
        siprintf(b,"%d M",(int)(trackLen[mapSel]+0.5f)); hInfoRow(130,"LENGTH",b);
        siprintf(b,"%d",lapsN); hInfoRow(142,"LAPS",b);
        hInfoRow(154,"AI LEVEL",diffName[diff]);
    } else {
        int half=(mapSel==0)?45:52;
        hInfoRow(118,"ARENA",mapName[mapSel]);
        siprintf(b,"%d X %d M",half*2,half*2); hInfoRow(130,"SIZE",b);
        hInfoRow(142,"OPPONENTS","7");
        hInfoRow(154,"AI LEVEL",diffName[diff]);
    }
    hFooter("START","BACK",'B');
}

static void hudCar(void)
{
    static const char *lab[6]={"TOP SPEED","ACCELERATION","HANDLING","STRENGTH","WEIGHT","NITRO"};
    const CarDef *d=&carDefs[carSel];
    float v[6]; int i,j; char b[32];
    u16 cc=C15(d->r,d->g,d->b);
    v[0]=d->top; v[1]=d->acc; v[2]=d->han; v[3]=d->str; v[4]=d->wgt; v[5]=d->nit;
    hcopy(menuBg,0,HUDH);
    hHeader("SELECT CAR");
    for(i=0;i<6;i++){
        int y=28+i*18, w=(int)(v[i]*5.8f);
        htextS(8,y,lab[i],COL_WHITE,1);
        hrect(8,y+9,60,8,C15(34,26,12));
        for(j=0;j<w;j++) hrect(9+j,y+10,1,6,C15(244-j/3,150+j/2,22+j/3));
        hframe(8,y+9,60,8,C15(110,76,12));
        siprintf(b,"%d.%d",(int)v[i],(int)(v[i]*10.0f)%10);
        htextS(72,y+9,b,COL_YEL,1);
    }
    hCarSil(182,50,100,d,cc,COL_DARK);
    siprintf(b,"%s CLASS",className[d->cls]);
    htextR(248,78,b,COL_WHITE,1);
    if(strlen(d->name)<=9) htextR(248,89,d->name,COL_YEL,2); else htextR(248,93,d->name,COL_YEL,1);
    hrect(112,110,136,14,COL_DARK); hframe(112,110,136,14,C15(120,80,10));
    htextS(118,113,"DRIVE",COL_WHITE,1); htextR(242,113,d->drive,COL_YEL,1);
    for(i=0;i<NCARDEF;i++){
        int x=13+i*29, sel=(i==carSel);
        const CarDef *e=&carDefs[i];
        hrect(x-2,140,30,30,sel?COL_YEL:C15(96,96,102));
        hrect(x,142,26,26,sel?C15(70,56,30):C15(26,22,14));
        hCarSil(x+13,154,22,e,C15(e->r,e->g,e->b),COL_DARK);
    }
    hFooter("SELECT","BACK",'B');
}

static void hudOpt(void)
{
    static const char *lab[3]={"AI LEVEL","RALLY LAPS","SOUND"};
    int i; char b[32];
    hcopy(menuBg,0,HUDH);
    hHeader("OPTIONS");
    for(i=0;i<3;i++){
        int y=40+i*30, sel=(i==optSel);
        hrect(14,y,228,24,sel?C15(220,128,18):C15(34,27,15));
        hframe(14,y,228,24,sel?COL_WHITE:C15(120,84,14));
        if(i==0) siprintf(b,"<  %s  >",diffName[diff]);
        else if(i==1) siprintf(b,"<  %d  >",lapsN);
        else siprintf(b,"<  %s  >",sndOn?"ON":"OFF");
        htextEx(24,y+8,lab[i],sel?COL_DARK:COL_CREAM,1,1);
        htextEx(232-htextW(b,1),y+8,b,sel?COL_DARK:COL_YEL,1,1);
    }
    hFooter("CHANGE","BACK",'B');
}

/* ============================== IN-GAME HUD ================================ */
static void hudGameA(void)
{
    Car *p=&cars[playerIdx];
    char b[48];
    int i;
    hcopy(menuBg,0,HUDH);
    siprintf(b,"%s  %s",gmode==GM_DERBY?"DERBY":"RALLY",mapName[mapId]);
    hHeader(b);
    hrect(204,5,46,14,C15(110,40,24)); hframe(204,5,46,14,COL_EDGE); htextS(210,8,"MENU",COL_WHITE,1);
    hrect(8,28,116,112,C15(18,15,10)); hframe(7,27,118,114,COL_EDGE);
    if(gmode==GM_RACE){
        for(i=0;i<npts;i++){
            int j=(i+1)%npts;
            int x0=(int)(mapOx+roadX[i]*mapS), y0=(int)(mapOy+roadZ[i]*mapS);
            int x1=(int)(mapOx+roadX[j]*mapS), y1=(int)(mapOy+roadZ[j]*mapS);
            hline(x0,y0,x1,y1,C15(184,160,120)); hline(x0+1,y0,x1+1,y1,C15(184,160,120));
            hline(x0,y0+1,x1,y1+1,C15(184,160,120));
        }
        hrect((int)(mapOx+roadX[0]*mapS)-2,(int)(mapOy+roadZ[0]*mapS)-2,5,5,COL_WHITE);
    } else {
        int ax0=(int)(mapOx-arenaH*mapS), ay0=(int)(mapOy-arenaH*mapS), aw=(int)(2.0f*arenaH*mapS);
        hrect(ax0,ay0,aw,aw,C15(86,70,48));
        hframe(ax0,ay0,aw,aw,COL_GRAY);
        for(i=0;i<nobs;i++){
            int rr=(int)(obs[i].r*mapS); if(rr<1) rr=1;
            hdisc((int)(mapOx+obs[i].x*mapS),(int)(mapOy+obs[i].z*mapS),rr,C15(160,158,166));
        }
    }
    for(i=0;i<ncars;i++){
        Car *c=&cars[i];
        if(i==playerIdx) continue;
        hdisc((int)(mapOx+c->x*mapS),(int)(mapOy+c->z*mapS),1,c->alive?C15(c->r,c->g,c->b):C15(70,66,60));
    }
    {
        int mx=(int)(mapOx+p->x*mapS), my=(int)(mapOy+p->z*mapS);
        hdisc(mx,my,3,COL_WHITE); hdisc(mx,my,2,C15(p->r,p->g,p->b));
    }
}

static void hudGameB(void)
{
    Car *p=&cars[playerIdx];
    char b[48];
    int hp=(int)p->hp, nt=(int)p->nitro;
    u16 hc=hp>50?COL_GREEN:(hp>25?COL_YEL:COL_RED);
    htextS(134,28,"SPEED",COL_GRAY,1);
    siprintf(b,"%d",(int)(fabsf(p->spd)*4.0f));
    htextS(134,38,b,COL_WHITE,3);
    htextS(208,52,"KM/H",COL_GRAY,1);
    htextS(134,64,"HEALTH",COL_GRAY,1);
    hbar(134,74,114,10,hp,hc,C15(46,20,16));
    htextS(134,88,"NITRO",COL_GRAY,1);
    hbar(134,98,114,10,nt,COL_BLUE,C15(16,22,44));
    if(gmode==GM_RACE){
        int lap=p->lap; if(lap<1)lap=1; if(lap>lapsN)lap=lapsN;
        siprintf(b,"LAP %d/%d",lap,lapsN); htextS(134,112,b,COL_ORANGE,2);
        siprintf(b,"POS %d/%d",racePlace(playerIdx),ncars); htextS(134,130,b,COL_WHITE,2);
    } else {
        siprintf(b,"LEFT %d",countAlive()); htextS(134,112,b,COL_ORANGE,2);
        siprintf(b,"KILLS %d",wrecks); htextS(134,130,b,COL_WHITE,2);
    }
    if(state==ST_COUNT){
        siprintf(b,"%d",countT/60+1);
        htextC(128,148,b,COL_YEL,4);
    } else if(state==ST_PLAY){
        if(frameNo<90) htextC(128,148,"GO!",COL_GREEN,4);
        else htextC(128,182,"A GAS B BRAKE Y NITRO L DRIFT",COL_GRAY,1);
    } else if(state==ST_END){
        htextC(128,146,endMsg1,COL_YEL,2);
        htextC(128,166,endMsg2,COL_WHITE,1);
        htextC(128,182,"TOUCH OR START FOR MENU",COL_GRAY,1);
    }
}

static void hudGame(void){ hudGameA(); hudGameB(); }

static void hudPresent(void)
{
    DC_FlushRange(hudBuf,sizeof(hudBuf));
    dmaCopy(hudBuf,hudFb,sizeof(hudBuf));
}
static void hudUpdate(void)         /* everything in one go (menus, screen changes) */
{
    if(state!=ST_MENU) hudGame();
    else if(menuScr==SC_MAIN) hudMain();
    else if(menuScr==SC_MAPS) hudMaps();
    else if(menuScr==SC_CAR)  hudCar();
    else hudOpt();
    hudPresent();
}
static int hudStage;               /* 0 = idle, 1 = background drawn, gauges + present next frame */

/* ============================== MENU LOGIC ================================= */
static int previewMap(void)
{
    if(menuScr==SC_MAPS) return modeSel==0?2+mapSel:mapSel;
    return lastMap;
}
static void optChange(int dir)
{
    if(optSel==0) diff=(diff+dir+3)%3;
    else if(optSel==1) lapsN=(dir>0)?(lapsN<3?3:(lapsN<5?5:2)):(lapsN>3?3:(lapsN>2?2:5));
    else { sndOn^=1; if(!sndOn) sndEngineStop(); }
    hudTimer=0;
}
static void menuOk(void)
{
    if(menuScr==SC_MAIN){
        if(mainSel<2){ modeSel=mainSel; mapSel=0; menuScr=SC_MAPS; }
        else if(mainSel==2) menuScr=SC_CAR;
        else menuScr=SC_OPT;
    } else if(menuScr==SC_MAPS){
        int map=(modeSel==0)?2+mapSel:mapSel;
        lastMap=map; startGame(map);
    } else if(menuScr==SC_CAR){
        menuScr=SC_MAIN;
    }
    hudTimer=0;
}
static void menuBack(void){ if(menuScr!=SC_MAIN){ menuScr=SC_MAIN; hudTimer=0; } }

static void menuTouch(int tx,int ty)
{
    int i;
    if(ty>=170){
        if(tx<128) menuOk();
        else if(menuScr==SC_MAIN) startGame(lastMap);
        else menuBack();
        return;
    }
    if(menuScr==SC_MAIN){
        for(i=0;i<4;i++) if(tx>=33+i*52&&tx<67+i*52&&ty>=80&&ty<114){ mainSel=i; menuOk(); }
    } else if(menuScr==SC_MAPS){
        for(i=0;i<2;i++){
            int x=i?134:10;
            if(tx>=x&&tx<x+112&&ty>=32&&ty<110){ if(mapSel==i) menuOk(); else mapSel=i; hudTimer=0; }
        }
    } else if(menuScr==SC_CAR){
        for(i=0;i<NCARDEF;i++) if(tx>=13+i*29&&tx<39+i*29&&ty>=140&&ty<170){ carSel=i; hudTimer=0; }
    } else {
        for(i=0;i<3;i++) if(ty>=40+i*30&&ty<64+i*30){ optSel=i; optChange(tx<128?-1:1); }
    }
}

static void menuInput(u32 down,int touched,int tx,int ty)
{
    int left=(down&KEY_LEFT)!=0, right=(down&KEY_RIGHT)!=0, up=(down&KEY_UP)!=0, dn=(down&KEY_DOWN)!=0;
    if(menuScr==SC_MAIN){
        if(left)  mainSel=(mainSel+3)%4;
        if(right) mainSel=(mainSel+1)%4;
        if(down&KEY_START){ startGame(lastMap); return; }
    } else if(menuScr==SC_MAPS){
        if(left||right||up||dn) mapSel^=1;
    } else if(menuScr==SC_CAR){
        if(left)  carSel=(carSel+NCARDEF-1)%NCARDEF;
        if(right) carSel=(carSel+1)%NCARDEF;
    } else {
        if(up) optSel=(optSel+2)%3;
        if(dn) optSel=(optSel+1)%3;
        if(left)  optChange(-1);
        if(right) optChange(1);
    }
    if(left||right||up||dn) hudTimer=0;
    if(down&KEY_A) menuOk();
    if(state==ST_MENU && (down&KEY_B)) menuBack();
    if(state==ST_MENU && touched) menuTouch(tx,ty);
}

/* ================================= LOOP ==================================== */
static void gameFrame(u32 held,u32 down,int touched,int tx,int ty)
{
    frameNo++;
    sndTick();
    if(state==ST_MENU){
        menuInput(down,touched,tx,ty);
        if(state==ST_MENU){
            mapId=previewMap(); gmode=(mapId<2)?GM_DERBY:GM_RACE;
            if(cars[0].model!=carSel) initCar(&cars[0],0,0.0f,0.0f,menuSpin);
            menuSpin+=0.02f; cars[0].h=menuSpin; cars[0].sn=sinf(menuSpin); cars[0].cs=cosf(menuSpin);
            if(hudTimer--<=0){ hudUpdate(); hudTimer=5; hudStage=0; }
        }
    } else {
        Car *p=&cars[playerIdx];
        if((down&KEY_START) || (touched && tx>=204 && ty<24) || (touched && state==ST_END && endT>30)){
            goMenu(); render();
            hudUpdate(); hudStage=0;
            return;
        }
        if(state==ST_COUNT){
            if(countT%60==59) sndBeep(0);
            countT--;
            if(countT<=0){ state=ST_PLAY; frameNo=0; sndBeep(1); hudTimer=0; }
        } else {
            updatePlay(held,down);
            if(state==ST_END) endT++;
        }
        sndEngine(p->spd,p->nitroOn,(state!=ST_COUNT)&&p->alive);
        if(hudStage==1){
            hudGameB(); hudPresent(); hudStage=0; hudTimer=4;
        } else if(hudTimer--<=0){
            hudGameA(); hudStage=1;
        }
    }
    render();
}

#ifndef HOST_TEST
int main(void)
{
    touchPosition tp;
    memset(&tp,0,sizeof(tp));
    lcdMainOnTop();
    videoSetMode(MODE_0_3D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    hudFb=(u16*)bgGetGfxPtr(bgInitSub(3,BgType_Bmp16,BgSize_B16_256x256,0,0));

    glInit();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_ANTIALIAS);
    glEnable(GL_BLEND);
    glClearColor(9,15,25,31);
    glClearPolyID(63);
    glClearDepth(0x7FFF);
    glViewport(0,0,255,191);

    loadTextures();
    initMenuData();
    sndInit();
    goMenu();

    while(1){
        u32 held, down;
        scanKeys();
        held=keysHeld(); down=keysDown();
        if(down&KEY_TOUCH) touchRead(&tp);
        gameFrame(held,down,(down&KEY_TOUCH)!=0,tp.px,tp.py);
        glFlush(0);
        swiWaitForVBlank();
    }
    return 0;
}
#endif
