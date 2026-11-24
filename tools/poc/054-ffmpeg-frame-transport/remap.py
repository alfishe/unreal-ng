#!/usr/bin/env python3
"""remap.py FRAMES.rgba [INDEX]: is ffmpeg's remap with FrameScaler's mapping the exact fixed frame?

Takes one 352x288 RGBA frame (INDEX, default 100) of FRAMES.rgba, crops it to 352x288 / 320x240 / 256x192 and, for
4K / 1440p / 1080p, compares (4:4:4 bytes) the conversion of FrameScaler's frame (`./fsdump`, built as in run.sh, in the current directory)
with the source picture converted at its size and remapped by gray16le tables of FrameScaler's mapping.
Prints EXACT or the share of differing bytes per layout. Runs in the current directory (writes *.raw / *.rgba).
"""
import subprocess, sys, numpy as np
CONV="scale=in_range=full:out_range=limited:out_color_matrix=bt709"
def maps(sw,sh,W,H):
    pw,ph,ox,oy,k=map(int,subprocess.run(["./fsdump","src.rgba",str(sw),str(sh),str(W),str(H),"big.rgba"],capture_output=True,text=True).stdout.split())
    xs=np.full(W,65535,np.uint16); ys=np.full(H,65535,np.uint16)
    xs[ox:ox+pw]=(np.arange(pw,dtype=np.uint64)*sw//pw); ys[oy:oy+ph]=(np.arange(ph,dtype=np.uint64)*sh//ph)
    np.broadcast_to(xs,(H,W)).astype('<u2').tofile("xmap.raw"); np.broadcast_to(ys[:,None],(H,W)).astype('<u2').tofile("ymap.raw")
def ff(args): return np.frombuffer(subprocess.run(["ffmpeg","-v","error"]+args,capture_output=True,check=True).stdout,np.uint8)
FB=352*288*4
INDEX=int(sys.argv[2]) if len(sys.argv)>2 else 100
full=open(sys.argv[1],"rb").read()[FB*INDEX:FB*(INDEX+1)]; open("full.rgba","wb").write(full)
for sw,sh in ((352,288),(320,240),(256,192)):
    subprocess.run(["ffmpeg","-v","error","-y","-f","rawvideo","-pix_fmt","rgba","-s","352x288","-i","full.rgba","-vf",f"crop={sw}:{sh}","-f","rawvideo","src.rgba"],check=True)
    for W,H in ((3840,2160),(2560,1440),(1920,1080)):
        maps(sw,sh,W,H)
        today=ff(["-f","rawvideo","-pix_fmt","rgba","-s",f"{W}x{H}","-i","big.rgba","-vf",f"{CONV},format=yuv444p","-f","rawvideo","-"])
        new=ff(["-f","rawvideo","-pix_fmt","rgba","-s",f"{sw}x{sh}","-i","src.rgba",
                "-f","rawvideo","-pix_fmt","gray16le","-s",f"{W}x{H}","-i","xmap.raw","-f","rawvideo","-pix_fmt","gray16le","-s",f"{W}x{H}","-i","ymap.raw",
                "-filter_complex",f"[0:v]{CONV},format=yuv444p[s];[s][1:v][2:v]remap=fill=black,format=yuv444p","-f","rawvideo","-"])
        d=(today!=new) if len(today)==len(new) else None
        print(f"{sw}x{sh}->{W}x{H}: {'EXACT' if d is not None and not d.any() else ('len differ' if d is None else f'diff {d.mean()*100:.3f}% ')}",flush=True)
