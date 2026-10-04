"""Bake the analytic studio fixture and matched Khronos split-sum data; Python/NumPy is offline only.
Khronos cc27919 Apache-2.0; Hammersley: Holger Dammertz CC BY 3.0. See shaders/pbr/NOTICE.md.
"""
from pathlib import Path
import struct
import numpy as np

SIZE, LUT, SAMPLES = 32, 64, 1024

def normalize(v):
    """Normalize final-axis vectors with a finite zero guard."""
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-12)

def studio(d):
    """Continuous scene-linear radiance: cool sky, warm floor and three broad studio emitters."""
    sky = np.clip(d[..., 1:2] * .5 + .5, 0, 1)
    color = np.array([.12, .09, .06]) * (1-sky) + np.array([.25, .36, .55]) * sky
    for direction, rgb, power in [((-.6,.7,-.8),(7,6,4.5),40), ((.8,.2,-.5),(2,3,5),28), ((0,.8,.5),(3,3,3),20)]:
        l = normalize(np.array(direction))
        color = color + np.maximum(d @ l, 0)[...,None] ** power * rgb
    return color

def points():
    """Hammersley sequence, using the Dammertz radical inverse expressed as bit reversal."""
    i=np.arange(SAMPLES,dtype=np.uint32); b=i.copy()
    b=(b<<16)|(b>>16)
    for shift,mask in [(1,0x55555555),(2,0x33333333),(4,0x0f0f0f0f),(8,0x00ff00ff)]:
        b=((b & mask)<<shift)|((b>>shift)&mask)
    return (i+.5)/SAMPLES, b.astype(float)/2**32
X,Y=points()

def hemisphere(kind,r):
    """Khronos cosine/GGX/Charlie importance sampling with perceptual roughness r."""
    a=max(r*r,1e-8)
    if kind==0: c=np.sqrt(1-Y)
    elif kind==1: c=np.sqrt((1-Y)/(1+(a*a-1)*Y))
    else: c=np.sqrt(1-Y**(2*a/(2*a+1)))
    s=np.sqrt(np.maximum(1-c*c,0)); p=2*np.pi*X
    return np.stack((s*np.cos(p),s*np.sin(p),c),-1)

def face_directions(face,size):
    """D3D/WebGPU cubemap face order +X,-X,+Y,-Y,+Z,-Z, image rows downward."""
    u,v=np.meshgrid((np.arange(size)+.5)*2/size-1,(np.arange(size)+.5)*2/size-1)
    one=np.ones_like(u)
    faces=[(one,-v,-u),(-one,-v,u),(u,one,v),(u,-one,-v),(u,-v,one),(-u,-v,-one)]
    return normalize(np.stack(faces[face],-1)).reshape(-1,3)

def filter_face(n,kind,r):
    """Integrate continuous radiance; no input texture LOD approximation is needed for this analytic source."""
    if kind==1 and r==0: return studio(n)
    h=hemisphere(kind,max(r,.04))
    local=h if kind==0 else np.stack((2*h[:,2]*h[:,0],2*h[:,2]*h[:,1],2*h[:,2]**2-1),-1)
    weight=np.ones(SAMPLES) if kind==0 else np.maximum(local[:,2],0)
    up=np.tile([0.,1.,0.],(len(n),1));up[np.abs(n[:,1])>.999]=[0,0,1]
    t=normalize(np.cross(up,n)); b=np.cross(n,t)
    result=[]
    for start in range(0,len(n),64):
        sl=slice(start,start+64)
        directions=local[None,:,0,None]*t[sl,None,:]+local[None,:,1,None]*b[sl,None,:]+local[None,:,2,None]*n[sl,None,:]
        result.append(np.sum(studio(directions)*weight[None,:,None],axis=1)/np.sum(weight))
    return np.concatenate(result)

def lookup():
    """Khronos GGX/Charlie LUT in RGB; A integrates runtime Charlie/Neubelt directional albedo."""
    out=np.zeros((LUT,LUT,4))
    nv=(np.arange(LUT)+.5)/LUT
    v=np.stack((np.sqrt(1-nv*nv),np.zeros(LUT),nv),-1)
    for row in range(LUT):
        r=(row+.5)/LUT
        for kind in [1,2]:
            h=hemisphere(kind,r); vh=np.maximum(v @ h.T,0)
            l=2*vh[:,:,None]*h[None,:,:]-v[:,None,:]
            nl=np.maximum(l[:,:,2],0); nh=h[:,2][None,:]
            if kind==1:
                a2=r**4
                vis=.5/np.maximum(nl*np.sqrt(nv[:,None]**2*(1-a2)+a2)+nv[:,None]*np.sqrt(nl*nl*(1-a2)+a2),1e-12)
                pdf=vis*vh*nl/np.maximum(nh,1e-12);fc=(1-vh)**5
                out[row,:,0]=4*np.mean((1-fc)*pdf,axis=1);out[row,:,1]=4*np.mean(fc*pdf,axis=1)
            else:
                # Reference LUT deliberately uses roughness, not roughness^2, in D_Charlie here.
                d=(2+1/r)*np.maximum(1-nh*nh,0)**(.5/r)/(2*np.pi)
                vis=np.clip(1/np.maximum(4*(nl+nv[:,None]-nl*nv[:,None]),1e-12),0,1)
                out[row,:,2]=8*np.pi*np.mean(vis*d*nl*vh,axis=1)
        # Uniform hemisphere integration of runtime sheen, supplying its energy scaling table.
        z=Y; st=np.sqrt(1-z*z); phi=2*np.pi*X
        l=np.stack((st*np.cos(phi),st*np.sin(phi),z),-1)
        h=normalize(l[None,:,:]+v[:,None,:]);nh=h[:,:,2]
        a=r*r; d=(2+1/a)*np.maximum(1-nh*nh,0)**(.5/a)/(2*np.pi)
        def lam(c):
            q=(1-a)**2
            aa=21.5473+(25.3245-21.5473)*q; bb=3.82987+(3.32435-3.82987)*q
            cc=.19823+(.16801-.19823)*q;dd=-1.97760+(-1.27393+1.97760)*q;ee=-4.32054+(-4.85967+4.32054)*q
            def helper(x): return aa/(1+bb*x**cc)+dd*x+ee
            return np.exp(np.where(c<.5,helper(c),2*helper(.5)-helper(1-c)))
        vis=np.clip(1/np.maximum((1+lam(nv[:,None])+lam(z[None,:]))*4*nv[:,None]*z[None,:],1e-12),0,1)
        out[row,:,3]=np.clip(2*np.pi*np.mean(d*vis*z,axis=1),0,1)
    return out

def main():
    """Write deterministic little-endian half-float fixture in the documented immutable layout."""
    mips=SIZE.bit_length();data=bytearray(b'OFGIBL1\0'+struct.pack('<III',SIZE,mips,LUT))
    for kind in range(3):
        for face in range(6):
            for mip in range(mips):
                size=SIZE>>mip;n=face_directions(face,size)
                rgb=filter_face(n,kind,mip/(mips-1))
                rgba=np.concatenate((rgb,np.ones((len(n),1))),axis=1)
                data.extend(rgba.astype('<f2').tobytes())
        print('cube',kind,flush=True)
    data.extend(lookup().astype('<f2').tobytes())
    Path('assets/pbr-studio.bin').write_bytes(data)
    print(len(data),'bytes',flush=True)
if __name__=='__main__': main()
