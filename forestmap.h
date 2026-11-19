/* ============================================================================
   forestmap.h  -  RALLY FOREST: the lap layout comes from trackDefs[0] in main.c
   (traced from the minimap); this file adds everything beside the road.

   Included by main.c right after drawArena().  Everything is built ONCE in
   fmBuild() as small textured quads with integer vertices, grouped by texture and
   by road segment, so drawing only touches the ~20 road segments around the
   player (short draw distance on purpose).  No floating point at draw time.

   Where things are is controlled by three tables you can edit:
     fmZones[]   what stands left / right of the road (by lap position 0..1)
     fmBuildProps()  signs, cones, towers, stands, bridge, rock arch, barn ...
     fmTrees()   forest density
   Lap position s: 0.0 = start line, goes round clockwise on the minimap.
   Left/right are seen by the driver.
   ============================================================================ */
#include "forestatlas.h"

#define FM_QMAX   2200
#define FM_FTMAX  900
#define FM_WBACK  6            /* draw this many road segments behind the camera ... */
#define FM_WFWD   14           /* ... and this many in front (1 segment is ~4.6 m)   */

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

static unsigned int fmRnd(void){ fmSeed=fmSeed*1664525u+1013904223u; return fmSeed>>16; }
static float fmRf(void){ return (float)(fmRnd()&1023)/1024.0f; }
static int fmH(int i,int k){ unsigned int h=(unsigned int)(i*2654435761u+k*40503u); h^=h>>13; h*=2246822519u; return (int)((h>>16)&255); }

/* ---------- lap-position table: [a,b) -> barrier + backdrop on each side ------------ */
typedef struct { float a,b; unsigned char bl,kl,br,kr; } FZone;
static const FZone fmZones[] = {
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
#define FM_NZONES (int)(sizeof(fmZones)/sizeof(fmZones[0]))

/* ---------------------------------- geometry helpers --------------------------- */
static void fmP(float *o,int i,int sd,float off,float y){ o[0]=roadX[i]+nrmX[i]*(float)sd*off; o[1]=y; o[2]=roadZ[i]+nrmZ[i]*(float)sd*off; }

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
    q=&fq[nfq]; fqG[nfq]=(unsigned char)g; fqS[nfq]=(short)si; nfq++;
    q->tx=floattof32(cx); q->ty=floattof32(cy); q->tz=floattof32(cz);
    for(k=0;k<4;k++){ q->v[k*3]=(short)cl16(c[k][0]-cx); q->v[k*3+1]=(short)cl16(c[k][1]-cy); q->v[k*3+2]=(short)cl16(c[k][2]-cz); }
    q->u0=(short)u0; q->u1=(short)u1; q->vb=(short)vb; q->vt=(short)vt;
    q->r=(unsigned char)r; q->g=(unsigned char)gg; q->b=(unsigned char)b;
}

/* c = bottom-left, bottom-right, top-right, top-left.  Big quads are cut into pieces
   of at most ~11 m so the 16-bit vertex offsets never overflow. */
static void fmAdd(int g,int si,float c[4][3],int u0,int u1,int vb,int vt,int r,int gg,int b)
{
    float d01=sqrtf((c[1][0]-c[0][0])*(c[1][0]-c[0][0])+(c[1][1]-c[0][1])*(c[1][1]-c[0][1])+(c[1][2]-c[0][2])*(c[1][2]-c[0][2]));
    float d12=sqrtf((c[2][0]-c[1][0])*(c[2][0]-c[1][0])+(c[2][1]-c[1][1])*(c[2][1]-c[1][1])+(c[2][2]-c[1][2])*(c[2][2]-c[1][2]));
    int nu=(int)(d01/10.0f)+1, nv=(int)(d12/10.0f)+1, iu, iv, a;
    if(nu==1 && nv==1){ fmAddOne(g,si,c,u0,u1,vb,vt,r,gg,b); return; }
    for(iv=0;iv<nv;iv++) for(iu=0;iu<nu;iu++){
        float s0=(float)iu/(float)nu, s1=(float)(iu+1)/(float)nu, t0=(float)iv/(float)nv, t1=(float)(iv+1)/(float)nv;
        float d[4][3], s[4], t[4]; int k;
        s[0]=s0; t[0]=t0; s[1]=s1; t[1]=t0; s[2]=s1; t[2]=t1; s[3]=s0; t[3]=t1;
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

static void fmSegQuads(int i,int sx,int type)
{
    int j=(i+1)%npts, sd=sx?1:-1, fl=fmH(i,1)&1, u0,u1,vb,vt, sh=225+(fmH(i,2)&30);
    float c[4][3];
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
        fmP(c[0],i,sd,9.2f,0.0f); fmP(c[1],j,sd,9.2f,0.0f); fmP(c[2],j,sd,9.2f,1.3f); fmP(c[3],i,sd,9.2f,1.3f);
        fmUV(FL_WOOD,0,&u0,&u1,&vb,&vt); fmAdd(FG_ATLAS,i,c,u0,u1,vb,vt,255,255,255);
        break;
    case FT_FMET:
        fmP(c[0],i,sd,9.2f,0.0f); fmP(c[1],j,sd,9.2f,0.0f); fmP(c[2],j,sd,9.2f,2.4f); fmP(c[3],i,sd,9.2f,2.4f);
        fmUV(FL_MET,0,&u0,&u1,&vb,&vt); fmAdd(FG_ATLAS,i,c,u0,u1,vb,vt,255,255,255);
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
    c[0][0]=p[0]-nx*w; c[0][1]=y0; c[0][2]=p[2]-nz*w;
    c[1][0]=p[0]+nx*w; c[1][1]=y0; c[1][2]=p[2]+nz*w;
    c[2][0]=p[0]+nx*w; c[2][1]=y1; c[2][2]=p[2]+nz*w;
    c[3][0]=p[0]-nx*w; c[3][1]=y1; c[3][2]=p[2]-nz*w;
    if(g==FG_ATLAS) fmUV(tile,flip,&u0,&u1,&vb,&vt);
    else if(g==FG_TIRES){ u0=0; u1=512; vb=512; vt=0; }
    fmAdd(g,i,c,u0,u1,vb,vt,r,gg,b);
}

static void fmSign(int i,int sd,float off,int tile,int flip,float w,float y0,float y1)
{
    fmFace(FG_ATLAS,i,sd,off,0.0f,w,y0,y1,tile,flip,255,255,255);
    if(y0>0.2f) fmFace(FG_FLAT,i,sd,off,0.02f,0.07f,0.0f,y0,0,0,92,92,98);
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
    float cx=roadX[i]+fx*fo+nx*no, cz=roadZ[i]+fz*fo+nz*no, c[4][3];
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
            c[k][1]=F[f][k][2]?y1:y0;
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
    c[0][0]=p[0]-dx*len-ex*0.15f; c[0][1]=0.05f; c[0][2]=p[2]-dz*len-ez*0.15f;
    c[1][0]=p[0]+dx*len-ex*0.15f; c[1][1]=0.05f; c[1][2]=p[2]+dz*len-ez*0.15f;
    c[2][0]=p[0]+dx*len+ex*0.15f; c[2][1]=0.05f; c[2][2]=p[2]+dz*len+ez*0.15f;
    c[3][0]=p[0]-dx*len+ex*0.15f; c[3][1]=0.05f; c[3][2]=p[2]-dz*len+ez*0.15f;
    fmUV(FL_LOG,0,&u0,&u1,&vb,&vt); fmAdd(FG_ATLAS,i,c,u0,u1,vb,vt,255,255,255);
}

static void fmBuildProps(void)
{
    int i,k; float s;
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
    fmCones(0.150f,0.190f,-1,7.0f,0.004f);
    fmTower(fmIdx(0.170f),-1,14.0f,0);
    /* ---- gorge with the bridge ---- */
    fmBox(FG_ATLAS,fmIdx(0.305f),0.0f,0.0f,2.4f,15.0f,6.8f,7.7f,63,FL_CONC,190,184,172);
    /* ---- bottom-right corner: row of chevrons on the left wall ---- */
    for(s=0.396f;s<0.436f;s+=0.010f) fmSign(fmIdx(s),-1,8.6f,FL_CHEV,0,0.7f,0.9f,2.4f);
    fmPlank(fmIdx(0.452f),-1,3.0f,0.3f,1.7f); fmPlank(fmIdx(0.456f),-1,1.0f,-0.5f,1.9f); fmPlank(fmIdx(0.459f),-1,4.2f,1.2f,1.4f);
    /* ---- the S-bump: big LED chevron boards + cones ---- */
    fmCones(0.470f,0.520f,-1,7.0f,0.004f);
    for(s=0.485f;s<0.515f;s+=0.010f) fmFace(FG_ATLAS,fmIdx(s),-1,9.8f,0.0f,1.5f,0.7f,2.5f,FL_LED,0,255,255,255);
    fmSign(fmIdx(0.535f),-1,8.8f,FL_CHEV,0,0.75f,1.0f,2.5f);
    /* ---- rock arch ---- */
    fmBox(FG_ATLAS,fmIdx(0.562f),0.0f,0.0f,4.2f,16.0f,7.4f,12.5f,63,FL_ROCK,214,212,206);
    /* ---- boards + cones after the arch ---- */
    fmCones(0.600f,0.640f,-1,7.0f,0.004f);
    for(s=0.605f;s<0.635f;s+=0.010f) fmFace(FG_ATLAS,fmIdx(s),-1,9.8f,0.0f,1.5f,0.7f,2.5f,FL_LED,0,255,255,255);
    for(s=0.655f;s<0.695f;s+=0.010f) fmSign(fmIdx(s),-1,9.0f,FL_CHEV,0,0.7f,1.0f,2.4f);
    /* ---- left-hand straight up the left edge ---- */
    fmPlank(fmIdx(0.842f),-1,7.8f,1.4f,1.8f); fmPlank(fmIdx(0.845f),-1,6.5f,0.2f,1.6f); fmPlank(fmIdx(0.848f),-1,8.4f,-1.0f,1.5f);
    /* ---- last corner + finishing straight ---- */
    fmTower(fmIdx(0.905f),-1,14.0f,0);
    fmCones(0.905f,0.962f,-1,7.0f,0.004f);
    for(s=0.925f;s<0.955f;s+=0.010f) fmFace(FG_ATLAS,fmIdx(s),-1,9.8f,0.0f,1.5f,0.7f,2.5f,FL_LED,0,255,255,255);
    fmCones(0.965f,0.996f,-1,7.0f,0.004f);
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
            float dens=1.9f, minOff=12.5f, maxOff=36.0f;
            if(s>0.95f||s<0.03f) dens=0.45f;
            else if(bar==FT_ROCKW) dens=2.3f;
            if(bar==FT_ROCKW){ minOff=11.5f; maxOff=24.0f; }
            if(bt==FT_HILL){ minOff=14.0f; maxOff=35.0f; }
            if(bt==FT_ROCKB){ minOff=16.5f; maxOff=26.0f; }
            cnt=(int)dens; if(fmRf()<dens-(float)cnt) cnt++;
            for(k=0;k<cnt && nft<FM_FTMAX;k++){
                float off=minOff+fmRf()*(maxOff-minOff), jitter=(fmRf()-0.5f)*4.4f;
                float x=roadX[i]+nrmX[i]*(float)sd*off+nrmZ[i]*jitter, z=roadZ[i]+nrmZ[i]*(float)sd*off-nrmX[i]*jitter;
                float y=fmBackY(bt,off), lim=off*0.9f; int xi=(int)x, zi=(int)z, ok=1;
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
            if(near<110 && fmSolid[sx][i]==0.0f && s>0.04f && s<0.95f && (fmRnd()&255)<85){
                float off=9.6f+fmRf()*3.6f, jitter=(fmRf()-0.5f)*3.0f;
                float x=roadX[i]+nrmX[i]*(float)sd*off+nrmZ[i]*jitter, z=roadZ[i]+nrmZ[i]*(float)sd*off-nrmX[i]*jitter;
                int ok=1, xi=(int)x, zi=(int)z; float lim=off*0.85f;
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
    fmSeed=0x2F6E1A9u; nfq=0; nft=0; nobs=0;
    fmAcc[0]=0.0f;
    for(i=0;i<npts;i++) fmAcc[i+1]=fmAcc[i]+sqrtf(seg2[i]);
    /* 1. what stands beside each road segment */
    for(i=0;i<npts;i++){
        float s=(float)i/(float)npts;
        for(z=0;z<FM_NZONES;z++) if(s>=fmZones[z].a && s<fmZones[z].b) break;
        if(z>=FM_NZONES) z=FM_NZONES-1;
        fmBar[0][i]=fmZones[z].bl; fmBack[0][i]=fmZones[z].kl; fmBar[1][i]=fmZones[z].br; fmBack[1][i]=fmZones[z].kr;
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
    useTex(TX_PINE);
    for(m=-FM_WBACK;m<=FM_WFWD;m++){
        int i=(base+m+npts)%npts;
        for(k=ftGS[i];k<ftGS[i+1];k++){
            const FTree *t=&ft[ftOrd[k]];
            int dx=(t->tx>>12)-camXi, dz=(t->tz>>12)-camZi, x0,z0,ua,ub,tr,tg,tb;
            if(dx*dx+dz*dz>70*70) continue;
            if(dx*camFxi+dz*camFzi< -384) continue;
            if(t->var==0){ tr=255;tg=255;tb=255; } else if(t->var==1){ tr=228;tg=238;tb=214; } else { tr=255;tg=226;tb=176; }
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
        else useTex(g==FG_GRASS?TX_GRASS:(g==FG_FLAT?TX_WHITE:TX_TIRES));
        for(m=-FM_WBACK;m<=FM_WFWD;m++){
            int i=(base+m+npts)%npts;
            for(k=fmGS[g][i];k<fmGS[g][i+1];k++){
                const FQ *q=&fq[fmOrd[k]];
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
static void fmCollide(Car *c)
{
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
            dmg=(vn-4.0f)*(fmHard[sx][i]?0.30f:0.12f);
            if(dmg>0.0f){ hitSrc=SRC_WALL; hitFx(c,dmg,nx,nz); }
        }
    }
}
