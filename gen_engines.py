#!/usr/bin/env python3
"""
gen_engines.py - one engine loop per car, cut from the idle part of the recorded engine MP3s (FLATOUTDSCARS.zip).

  python3 gen_engines.py <folder with the unzipped FLATOUTDSCARS>

For every car it finds a seamless loop of the steady idle sound (the game pitches it up with the speed),
converts it to 11025 Hz / 8 bit and rewrites the engine entries of sounds.h:  SFX_ENGINE_0 .. SFX_ENGINE_7 in
the order of carDefs[] (ROAMER, MALICE, NEVADA, INSETTA, BULLET GT, CANYON, TRUCK, FLATMOBILE).
Needs ffmpeg, numpy, scipy.
"""
import re, sys, os, glob, subprocess, wave
import numpy as np
from scipy.signal import resample_poly

CARS=[('ROAMER','Roamer'),('MALICE','Malice'),('NEVADA','Nevada'),('INSETTA','Insetta'),
      ('BULLET_GT','Bullet GT'),('CANYON','Canyon'),('TRUCK','Truck'),('FLATMOBILE','Flatmobile')]
RATE=11025
TARGET_RMS=38.0       # about what the old engine samples had
LIMIT=120.0

def load(folder,dirname):
    f=[p for p in glob.glob(os.path.join(folder,'**',dirname,'*ngine.mp3'),recursive=True)][0]
    tmp='/tmp/_eng.wav'
    subprocess.run(['ffmpeg','-v','error','-y','-i',f,'-ac','1','-ar','22050',tmp],check=True)
    w=wave.open(tmp); a=np.frombuffer(w.readframes(w.getnframes()),np.int16).astype(float)/32768.0
    return resample_poly(a,1,2)                     # 22050 -> 11025 with an anti-alias filter

def best_loop(a):
    from scipy.signal import correlate
    W=int(0.30*RATE); best=None
    nmin,nmax=int(0.9*RATE),int(2.0*RATE)
    for t0 in np.arange(1.0,3.6,0.05):
        i0=int(t0*RATE); ref=a[i0:i0+W]; rn=np.linalg.norm(ref)+1e-9
        seg=a[i0+nmin:i0+nmax+W]
        cc=correlate(seg,ref,mode='valid',method='fft')               # cc[k] = ref . a[i0+nmin+k : ... + W]
        e=np.concatenate([[0],np.cumsum(seg**2)]); en=np.sqrt(e[W:W+len(cc)]-e[:len(cc)])+1e-9
        r=cc/(rn*en); k=int(np.argmax(r))
        if best is None or r[k]>best[0]: best=(float(r[k]),i0,nmin+k)
    return best

def make_loop(a):
    c,i0,N=best_loop(a)
    X=int(0.08*RATE)
    seg=a[i0:i0+N+X].copy()
    t=np.linspace(0,np.pi/2,X)
    loop=seg[:N].copy()
    loop[:X]=seg[:X]*np.sin(t)+seg[N:N+X]*np.cos(t)                  # equal-power blend of the tail into the head: no click, no dip
    loop-=loop.mean()
    loop*=TARGET_RMS/(np.sqrt((loop**2).mean())+1e-9)
    loop=LIMIT*np.tanh(loop/LIMIT)                                    # soft limiter keeps the peaks under control
    rng=np.random.default_rng(1)
    q=np.round(loop+(rng.random(N)-rng.random(N))*0.5).clip(-127,127).astype(int)
    return q,c

def fmt(name,q):
    lines=[]
    for k in range(0,len(q),24): lines.append('  '+','.join(str(int(v)) for v in q[k:k+24])+',')
    return 'static const signed char %s[%d] __attribute__((aligned(4))) = {\n%s\n};\n'%(name,len(q),'\n'.join(lines))

def main():
    folder=sys.argv[1]; hfile=os.path.join(os.path.dirname(os.path.abspath(__file__)),'sounds.h')
    s=open(hfile).read()
    arrays=''; enum=''; defs=''
    for k,(tag,dirname) in enumerate(CARS):
        q,c=make_loop(load(folder,dirname))
        print('%-10s loop %5d samples (%.2f s)  seam correlation %.3f  rms %.1f peak %d'%(tag,len(q),len(q)/RATE,c,np.sqrt((q.astype(float)**2).mean()),np.abs(q).max()))
        nm='snd_engine_%d'%k
        arrays+=fmt(nm,q)+'\n'; enum+='  SFX_ENGINE_%d,   /* %s */\n'%(k,tag); defs+='  { %s, %d, %d, 1 },\n'%(nm,len(q),RATE)
    # drop the old engines and anything from a previous run
    s=re.sub(r'static const signed char snd_engine_[a-z0-9_]+\[\d+\][^{]*\{.*?\};\n\n?','',s,flags=re.S)
    s=re.sub(r'  SFX_ENGINE_[A-Z0-9_]+,[^\n]*\n','',s)
    s=re.sub(r'  \{ snd_engine_[a-z0-9_]+, \d+, \d+, 1 \},\n','',s)
    s=s.replace('static const signed char snd_gas_explosion_2[',arrays+'static const signed char snd_gas_explosion_2[',1)
    s=s.replace('  SFX_GAS_EXPLOSION_2,\n',enum+'  SFX_GAS_EXPLOSION_2,\n',1)
    s=s.replace('  { snd_gas_explosion_2,',defs+'  { snd_gas_explosion_2,',1)
    open(hfile,'w').write(s)
    print('sounds.h updated')
main()
