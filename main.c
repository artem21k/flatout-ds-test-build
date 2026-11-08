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
#define LAPS     3
#define MAXOBS   200
#define MAXPART  40
#define CAR_R    1.1f

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

/* -------------------------------- globals --------------------------------- */
static int   gmode, mapId, diff=1, state, menuSel, playerIdx, ncars, nobs, npts;
static int   frameNo, countT, endT, finishCount, wrecks, hudTimer;
static float camH, shake, menuSpin, arenaH=45.0f;
static char  endMsg1[32], endMsg2[32];
static Car   cars[MAXC];
static Obs   obs[MAXOBS];
static Part  parts[MAXPART];
static int   partNext;

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
    if(!on){ sndEngineStop(); return; }
    if(engineCh<0) engineCh=soundPlayPSG(DutyCycle_25,90,0,64);
    if(engineCh>=0){
        soundSetFreq(engineCh,(int)(62.0f+fabsf(spd)*7.0f+(nit?45.0f:0.0f)));
        soundSetVolume(engineCh,(int)(26.0f+fabsf(spd)*1.1f));
    }
}
static void sndCrash(float dmg)
{
    int s=(crashT[0]<=crashT[1])?0:1;
    if(crashCh[s]>=0) soundKill(crashCh[s]);
    crashCh[s]=soundPlayNoise(2500+(int)(dmg*60.0f),(int)clampf(40.0f+dmg*5.0f,40.0f,127.0f),64);
    crashT[s]=6+(int)clampf(dmg*0.4f,0.0f,10.0f);
}
static void sndBeep(int hi)
{
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
        float w=mxx-mnx, h=mxz-mnz, s1=112.0f/w, s2=112.0f/h;
        mapS=(s1<s2)?s1:s2;
        mapOx=8.0f+(120.0f-w*mapS)*0.5f-mnx*mapS;
        mapOy=20.0f+(120.0f-h*mapS)*0.5f-mnz*mapS;
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
    c->r=carCol[k][0]; c->g=carCol[k][1]; c->b=carCol[k][2];
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
    c->nitro=clampf(c->nitro+dmg,0.0f,100.0f);
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
    float maxsp=MAXSPD*power*c->skill, acc=ACCEL, grip, spdf, yaw;

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
    yaw=c->steer*2.5f*clampf(spdf/5.0f,0.0f,1.0f)/(1.0f+spdf*0.025f);
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
    float lim=2.0f*CAR_R, d,nx,nz,pen,rvn,j,dmg;
    if(!a->alive && !b->alive) return;
    if(d2>=lim*lim || d2<0.0001f) return;
    d=sqrtf(d2); nx=dx/d; nz=dz/d;
    pen=(lim-d)*0.5f;
    a->x-=nx*pen; a->z-=nz*pen; b->x+=nx*pen; b->z+=nz*pen;
    rvn=(b->vx-a->vx)*nx+(b->vz-a->vz)*nz;
    if(rvn<0.0f){
        j=-(1.4f)*rvn*0.5f;
        a->vx-=nx*j; a->vz-=nz*j; b->vx+=nx*j; b->vz+=nz*j;
        dmg=(-rvn-2.0f)*0.5f;
        if(dmg>0.0f){ hitFx(a,dmg,nx,nz); hitFx(b,dmg,-nx,-nz); }
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

static void setupDerby(void)
{
    int k;
    float spawnR;
    nobs=0; ncars=MAXC; playerIdx=0;
    if(mapId==0){
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
    spawnR=arenaH*0.62f;
    for(k=0;k<ncars;k++){
        float a=(float)k*TWOPI_F/(float)ncars;
        initCar(&cars[k],k,sinf(a)*spawnR,-cosf(a)*spawnR,a+PI_F);
    }
    mapS=112.0f/(2.0f*arenaH); mapOx=8.0f+60.0f; mapOy=20.0f+60.0f;
}

static void setupRace(void)
{
    int k;
    ncars=MAXC; playerIdx=5;
    buildTrack(mapId-2);
    buildScenery();
    for(k=0;k<ncars;k++){
        int row=k/2, col=k%2, i=npts-1-row;
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
    state=ST_MENU; ncars=1; playerIdx=0; nobs=0; menuSpin=0.0f;
    mapId=menuSel; gmode=(mapId<2)?GM_DERBY:GM_RACE;
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
    float bx=c->x-c->sn*1.3f, bz=c->z+c->cs*1.3f;          /* behind the car */
    if(!c->alive){
        if((frameNo&3)==0) spawnPart(PT_BLACK,c->x,0.7f,c->z,(rndf()-0.5f)*0.8f,1.4f,(rndf()-0.5f)*0.8f,1.3f);
        return;
    }
    if(spd>9.0f && (frameNo&3)==(int)(c->r&3))
        spawnPart(PT_DUST,bx,0.2f,bz,(rndf()-0.5f)*1.5f,0.8f,(rndf()-0.5f)*1.5f,0.7f);
    if(c->hp<45.0f && (frameNo&3)==0)
        spawnPart(c->hp<20.0f?PT_BLACK:PT_SMOKE,c->x+c->sn*0.9f,0.9f,c->z-c->cs*0.9f,(rndf()-0.5f)*0.5f,1.5f,(rndf()-0.5f)*0.5f,1.0f);
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
            if(c->lap>LAPS && !c->finished){ c->finished=1; c->finishRank=++finishCount; }
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

static void drawCarBody(const Car *c)
{
    int C=(int)(c->cs*4096.0f), S=(int)(c->sn*4096.0f);
    int W=K16(0.62f), L=K16(1.15f), Y0=K16(0.28f), Y1=K16(0.72f);
    int tr,tg,tb,i,f,k,p[8][3],q[8][3],d,y2;
    static const int shade[5]={100,85,70,60,60};
    carShade(c,&tr,&tg,&tb);
    for(i=0;i<8;i++){
        int sa=(i&1)?1:-1, sb=(i&2)?1:-1, a=sa*W, b=sb*L, y=Y0;
        if(i&4){ int dd=c->def[i&3]; a=sa*(W-(dd>>2)); b=sb*(L-(dd>>2)); y=Y1-dd; }
        p[i][0]=a; p[i][1]=y; p[i][2]=b;
    }
    d=(c->def[0]+c->def[1]+c->def[2]+c->def[3])>>3;
    y2=K16(1.12f)-d;
    for(i=0;i<8;i++){
        int sa=(i&1)?1:-1, top=(i&4)!=0, front=(i&2)!=0;
        q[i][0]=sa*(top?K16(0.40f):K16(0.50f));
        q[i][1]=top?y2:Y1;
        q[i][2]=front?(top?K16(0.10f):K16(0.30f)):(top?-K16(0.55f):-K16(0.75f));
    }
    glPushMatrix();
    mtxT(floattof32(c->x),0,floattof32(c->z));
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
    glEnd();
    glPopMatrix(1);
}

/* pass 0: outer wheel faces (rim texture)   pass 1: tread faces (black texture) */
static void drawWheels(const Car *c,int pass)
{
    int C=(int)(c->cs*4096.0f), S=(int)(c->sn*4096.0f);
    int sA=(int)(c->steer*0.4f*4096.0f), cA=4096-((sA*sA)>>13);
    int s,t,i;
    glPushMatrix();
    mtxT(floattof32(c->x),0,floattof32(c->z));
    glBegin(QUADS_MODE);
    glColor3b(255,255,255);
    for(s=-1;s<=1;s+=2) for(t=-1;t<=1;t+=2){
        int w[8][3], ca=s*(K16(0.62f)+K16(0.10f)), cb=t*K16(0.78f);
        int sa2=(t>0)?sA:0, ca2=(t>0)?cA:4096;
        for(i=0;i<8;i++){
            int da=(i&1)?K16(0.11f):-K16(0.11f), db=(i&2)?K16(0.36f):-K16(0.36f);
            int ra=(da*ca2+db*sa2)>>12, rb=(-da*sa2+db*ca2)>>12;
            w[i][0]=ca+ra; w[i][1]=(i&4)?K16(0.72f):0; w[i][2]=cb+rb;
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
    int C=(int)(c->cs*4096.0f), S=(int)(c->sn*4096.0f);
    int a=K16(0.80f), b=K16(1.40f), y=K16(0.08f);
    glPolyFmt(POLY_ALPHA(13)|POLY_CULL_NONE|POLY_ID(1+k));
    glPushMatrix();
    mtxT(floattof32(c->x),0,floattof32(c->z));
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

static void render(void)
{
    float ex,ey,ez,lx,ly,lz,fdx,fdz,fl;
    int k;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(65.0f,256.0f/192.0f,0.5f,115.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    if(state==ST_MENU){
        ex=2.2f; ey=2.2f; ez=5.6f; lx=0.0f; ly=0.6f; lz=0.0f;
    } else {
        Car *p=&cars[playerIdx];
        float s,c;
        camH+=wrapa(p->h-camH)*0.1f;
        s=sinf(camH); c=cosf(camH);
        ex=p->x-s*8.0f; ez=p->z+c*8.0f; ey=3.6f+((frameNo&1)?shake:-shake)*0.5f;
        if(gmode==GM_DERBY){ ex=clampf(ex,-(arenaH-1.5f),arenaH-1.5f); ez=clampf(ez,-(arenaH-1.5f),arenaH-1.5f); }
        lx=p->x+s*6.0f; lz=p->z-c*6.0f; ly=1.0f;
        shake*=0.9f;
    }
    gluLookAt(ex,ey,ez, lx,ly,lz, 0.0f,1.0f,0.0f);

    fdx=lx-ex; fdz=lz-ez; fl=sqrtf(fdx*fdx+fdz*fdz);
    if(fl<0.001f) fl=1.0f;
    fdx/=fl; fdz/=fl;
    camXi=(int)ex; camZi=(int)ez;
    camFxi=(int)(fdx*64.0f); camFzi=(int)(fdz*64.0f);
    camRxi=(int)(-fdz*4096.0f); camRzi=(int)(fdx*4096.0f);     /* right = (-fz, fx) */

    curTex=-1;
    glPolyFmt(POLY_ALPHA(31)|POLY_CULL_NONE);
    drawSky(ex,ez);
    drawGround(ex,ez);
    if(state!=ST_MENU){
        if(gmode==GM_RACE){
            drawRoad();
            drawGantry();
            drawBillboards(OB_TREE,mapId==3?TX_CACTUS:TX_PINE,mapId==3?K16(1.125f):K16(1.75f),mapId==3?K16(4.5f):K16(7.0f));
            if(mapId==3) drawBillboards(OB_ROCK,TX_ROCK,K16(1.6f),K16(1.6f));
        } else drawArena();
    }
    useTex(TX_CAR);
    for(k=0;k<ncars;k++) drawCarBody(&cars[k]);
    useTex(TX_WHEEL);
    for(k=0;k<ncars;k++) drawWheels(&cars[k],0);
    useTex(TX_BLACK);
    for(k=0;k<ncars;k++) drawWheels(&cars[k],1);
    useTex(TX_WHITE);
    for(k=0;k<ncars;k++) drawShadow(&cars[k],k);
    drawParticles();
}

/* ================================== HUD ==================================== */
/* Bottom screen: 256x192 16-bit bitmap, drawn into RAM and DMA-copied ~10x/s. */
#define HUDW 256
#define HUDH 192
static u16 hudBuf[HUDW*HUDH] __attribute__((aligned(4)));
static u16 *hudFb;

#define C15(r,g,b) ARGB16(1,(r)>>3,(g)>>3,(b)>>3)
#define COL_WHITE  C15(244,244,244)
#define COL_GRAY   C15(150,160,180)
#define COL_ORANGE C15(255,160,40)
#define COL_GREEN  C15(90,230,110)
#define COL_RED    C15(235,70,60)
#define COL_YELLOW C15(250,220,60)
#define COL_BLUE   C15(70,150,255)
#define COL_PANEL  C15(40,52,80)
#define COL_EDGE   C15(100,120,160)

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
static void hgrad(int y0,int y1,int r0,int g0,int b0,int r1,int g1,int b1)
{
    int y;
    for(y=y0;y<y1;y++){
        int t=(y-y0)*256/(y1-y0>0?y1-y0:1);
        u16 c=C15(r0+(r1-r0)*t/256,g0+(g1-g0)*t/256,b0+(b1-b0)*t/256);
        u32 v=(u32)c|((u32)c<<16);
        u32 *row=(u32*)&hudBuf[y*HUDW];
        int i;
        for(i=0;i<HUDW/2;i++) row[i]=v;
    }
}
static void htext(int x,int y,const char *s,u16 c,int sc)
{
    for(;*s;s++,x+=8*sc){
        int ch=(unsigned char)*s, r,b,i,j;
        const unsigned char *g;
        if(ch<32||ch>126) ch='?';
        g=&font8x8[(ch-32)*8];
        for(r=0;r<8;r++){
            int bits=g[r];
            if(!bits) continue;
            for(b=0;b<8;b++) if(bits&(0x80>>b))
                for(j=0;j<sc;j++) for(i=0;i<sc;i++) hpx(x+b*sc+i,y+r*sc+j,c);
        }
    }
}
static void htextc(int cx,int y,const char *s,u16 c,int sc){ htext(cx-(int)strlen(s)*4*sc,y,s,c,sc); }

static void hbar(int x,int y,int w,int h,int val,u16 full,u16 back)
{
    int f=val*(w-2)/100;
    if(f<0) f=0;
    if(f>w-2) f=w-2;
    hrect(x,y,w,h,back);
    hrect(x+1,y+1,f,h-2,full);
    hframe(x,y,w,h,COL_EDGE);
}

static const int btnY[4]={40,70,100,130};

static void hudMenu(void)
{
    int i;
    char buf[40];
    hgrad(0,HUDH,22,30,52,8,10,20);
    htextc(128,6,"RUMBLE FOREST",COL_ORANGE,2);
    htextc(128,26,"SMASH CARS  -  WIN RACES",COL_GRAY,1);
    for(i=0;i<4;i++){
        int sel=(i==menuSel);
        hrect(16,btnY[i],224,26,sel?C15(214,104,20):COL_PANEL);
        hframe(16,btnY[i],224,26,sel?COL_WHITE:COL_EDGE);
        siprintf(buf,"%s  %s",i<2?"DERBY":"RALLY",mapName[i]);
        htext(28,btnY[i]+9,buf,COL_WHITE,1);
        htext(184,btnY[i]+9,i<2?"8 CARS":"3 LAPS",sel?COL_WHITE:COL_GRAY,1);
    }
    hrect(16,162,224,16,COL_PANEL); hframe(16,162,224,16,COL_EDGE);
    siprintf(buf,"<  AI: %s  >",diffName[diff]);
    htextc(128,166,buf,COL_YELLOW,1);
    htextc(128,182,"A GAS B BRAKE Y NITRO L DRIFT",COL_GRAY,1);
}

static void hudGameA(void);
static void hudGameB(void);
static void hudGame(void){ hudGameA(); hudGameB(); }

static void hudGameA(void)
{
    Car *p=&cars[playerIdx];
    char b[48];
    int i;

    hgrad(0,HUDH,18,24,40,8,10,20);
    hrect(0,0,HUDW,16,COL_PANEL);
    siprintf(b,"%s  %s",gmode==GM_DERBY?"DERBY":"RALLY",mapName[mapId]);
    htext(6,4,b,COL_WHITE,1);
    hrect(206,2,46,12,C15(120,50,40)); hframe(206,2,46,12,COL_EDGE); htext(212,4,"MENU",COL_WHITE,1);

    /* minimap */
    hrect(8,20,120,120,C15(16,24,20)); hframe(7,19,122,122,COL_EDGE);
    if(gmode==GM_RACE){
        for(i=0;i<npts;i++){
            int j=(i+1)%npts;
            int x0=(int)(mapOx+roadX[i]*mapS), y0=(int)(mapOy+roadZ[i]*mapS);
            int x1=(int)(mapOx+roadX[j]*mapS), y1=(int)(mapOy+roadZ[j]*mapS);
            hline(x0,y0,x1,y1,C15(176,156,126)); hline(x0+1,y0,x1+1,y1,C15(176,156,126));
            hline(x0,y0+1,x1,y1+1,C15(176,156,126));
        }
        hrect((int)(mapOx+roadX[0]*mapS)-2,(int)(mapOy+roadZ[0]*mapS)-2,5,5,COL_WHITE);
    } else {
        int ax0=(int)(mapOx-arenaH*mapS), ay0=(int)(mapOy-arenaH*mapS), aw=(int)(2.0f*arenaH*mapS);
        hrect(ax0,ay0,aw,aw,C15(70,58,44));
        hframe(ax0,ay0,aw,aw,COL_GRAY);
        for(i=0;i<nobs;i++){
            int rr=(int)(obs[i].r*mapS); if(rr<1) rr=1;
            hdisc((int)(mapOx+obs[i].x*mapS),(int)(mapOy+obs[i].z*mapS),rr,C15(150,150,160));
        }
    }
    for(i=0;i<ncars;i++){
        Car *c=&cars[i];
        int mx=(int)(mapOx+c->x*mapS), my=(int)(mapOy+c->z*mapS);
        if(i==playerIdx) continue;
        hdisc(mx,my,1,c->alive?C15(c->r,c->g,c->b):C15(70,70,70));
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
    int x,y,hp=(int)p->hp,nt=(int)p->nitro;
    u16 hc=hp>50?COL_GREEN:(hp>25?COL_YELLOW:COL_RED);
    /* gauges */
    htext(140,22,"SPEED",COL_GRAY,1);
    siprintf(b,"%d",(int)(fabsf(p->spd)*4.0f));
    htext(140,32,b,COL_WHITE,3);
    htext(214,46,"KM/H",COL_GRAY,1);
    htext(140,62,"HEALTH",COL_GRAY,1);
    hbar(140,72,108,10,hp,hc,C15(40,20,20));
    htext(140,88,"NITRO",COL_GRAY,1);
    hbar(140,98,108,10,nt,COL_BLUE,C15(16,24,48));
    if(gmode==GM_RACE){
        int lap=p->lap; if(lap<1)lap=1; if(lap>LAPS)lap=LAPS;
        siprintf(b,"LAP %d/%d",lap,LAPS); htext(140,112,b,COL_ORANGE,2);
        siprintf(b,"POS %d/%d",racePlace(playerIdx),ncars); htext(140,131,b,COL_WHITE,2);
    } else {
        siprintf(b,"LEFT %d",countAlive()); htext(140,112,b,COL_ORANGE,2);
        siprintf(b,"KILLS %d",wrecks); htext(140,131,b,COL_WHITE,2);
    }

    /* messages */
    y=148; x=128;
    if(state==ST_COUNT){
        siprintf(b,"%d",countT/60+1);
        htextc(x,y,b,COL_YELLOW,4);
    } else if(state==ST_PLAY){
        if(frameNo<90) htextc(x,y,"GO!",COL_GREEN,4);
        else htextc(x,182,"A GAS B BRAKE Y NITRO L DRIFT",COL_GRAY,1);
    } else if(state==ST_END){
        htextc(x,148,endMsg1,COL_YELLOW,2);
        htextc(x,168,endMsg2,COL_WHITE,1);
        htextc(x,182,"TOUCH SCREEN OR START: MENU",COL_GRAY,1);
    }
}

static void hudPresent(void)
{
    DC_FlushRange(hudBuf,sizeof(hudBuf));
    dmaCopy(hudBuf,hudFb,sizeof(hudBuf));
}
static void hudUpdate(void)         /* everything in one go (menu, screen changes) */
{
    if(state==ST_MENU) hudMenu(); else hudGame();
    hudPresent();
}
static int hudStage;               /* 0 = idle, 1 = background drawn, gauges + present next frame */

/* ================================= LOOP ==================================== */
static void gameFrame(u32 held,u32 down,int touched,int tx,int ty)
{
    frameNo++;
    sndTick();
    if(state==ST_MENU){
        int i;
        if(down&KEY_UP)   { menuSel=(menuSel+3)%4; hudTimer=0; }
        if(down&KEY_DOWN) { menuSel=(menuSel+1)%4; hudTimer=0; }
        if(down&(KEY_LEFT|KEY_L))  { diff=(diff+2)%3; hudTimer=0; }
        if(down&(KEY_RIGHT|KEY_R)) { diff=(diff+1)%3; hudTimer=0; }
        if(touched){
            for(i=0;i<4;i++) if(tx>=16&&tx<240&&ty>=btnY[i]&&ty<btnY[i]+26){ menuSel=i; startGame(i); }
            if(state==ST_MENU && ty>=162 && ty<178){
                if(tx<128) diff=(diff+2)%3; else diff=(diff+1)%3;
                hudTimer=0;
            }
        }
        if(state==ST_MENU){
            if(down&(KEY_A|KEY_START)) startGame(menuSel);
        }
        if(state==ST_MENU){
            mapId=menuSel; gmode=(mapId<2)?GM_DERBY:GM_RACE;
            menuSpin+=0.02f; cars[0].h=menuSpin; cars[0].sn=sinf(menuSpin); cars[0].cs=cosf(menuSpin);
        }
    } else {
        Car *p=&cars[playerIdx];
        if((down&KEY_START) || (touched && tx>=206 && ty<16) || (touched && state==ST_END && endT>30)){
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
    }
    if(state==ST_MENU){
        if(hudTimer--<=0){ hudUpdate(); hudTimer=5; hudStage=0; }
    } else if(hudStage==1){
        hudGameB(); hudPresent(); hudStage=0; hudTimer=4;
    } else if(hudTimer--<=0){
        hudGameA(); hudStage=1;
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
