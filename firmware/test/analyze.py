#!/usr/bin/env python3
"""Validate host PCM: L/R tone + correlation vs ffmpeg reference. Exit 0 = PASS."""
import struct, math, sys

def load(p):
    d=open(p,"rb").read(); n=len(d)//4
    v=struct.unpack("<%dh"%(n*2), d[:n*4]); return list(v[0::2]), list(v[1::2])

def goertzel(x,f,sr=48000):
    w=2*math.pi*f/sr; c=2*math.cos(w); s1=s2=0.0
    for v in x: s0=v+c*s1-s2; s2=s1; s1=s0
    return s1*s1+s2*s2-c*s1*s2

def dominant(x,cands):
    seg=x[2000:2000+8192]; return max(cands,key=lambda f:goertzel(seg,f))

def corr(a,b,off,n,base=3000):
    aa=a[base:base+n]; bb=b[base+off:base+off+n]
    sa=sum(aa);sb=sum(bb)
    num=sum(x*y for x,y in zip(aa,bb))-sa*sb/n
    va=sum(x*x for x in aa)-sa*sa/n; vb=sum(y*y for y in bb)-sb*sb/n
    return num/(va*vb)**0.5 if va>0 and vb>0 else 0

hL,hR=load(sys.argv[1]); rL,rR=load(sys.argv[2])
dL=dominant(hL,[220,330,440,550,660,770,880,990]); dR=dominant(hR,[220,330,440,550,660,770,880,990])
n=4096
best=max(range(1500), key=lambda o: corr(hL,rL,o,n))
cL=corr(hL,rL,best,n); cR=corr(hR,rR,best,n)
print(f"host {len(hL)} samples | L tone {dL}Hz (want 440) R tone {dR}Hz (want 880)")
print(f"correlation vs ffmpeg: L={cL:.4f} R={cR:.4f}")
ok = dL==440 and dR==880 and cL>0.95 and cR>0.95
print("PASS" if ok else "FAIL"); sys.exit(0 if ok else 1)
