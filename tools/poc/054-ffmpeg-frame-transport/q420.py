import subprocess, sys, numpy as np
CONV="scale=in_range=full:out_range=limited:out_color_matrix=bt709"
BACK="scale=in_range=limited:out_range=full:in_color_matrix=bt709"
def run(inp,size,vf,pix,raw="rgba"):
    return subprocess.run(["ffmpeg","-v","error","-f","rawvideo","-pix_fmt",raw,"-s",size,"-i",inp,"-vf",vf,"-f","rawvideo","-pix_fmt",pix,"-"],capture_output=True,check=True).stdout
def rgb(y,w,h):
    open("t.yuv","wb").write(y)
    return np.frombuffer(run("t.yuv",f"{w}x{h}",f"{BACK},format=rgba","rgba","yuv420p"),np.uint8).reshape(h,w,4)[:,:,:3].astype(np.int32)
def psnr(a,b): return 10*np.log10(255*255/((a-b)**2).mean())
FB=352*288*4; W,H=3840,2160
for name in sys.argv[1:]:
    open("src.rgba","wb").write(open(f"{name}.rgba","rb").read()[FB*100:FB*101])
    pw,ph,ox,oy,k=map(int,subprocess.run(["./fsdump","src.rgba","352","288",str(W),str(H),"big.rgba"],capture_output=True,text=True).stdout.split())
    ref=np.fromfile("big.rgba",np.uint8).reshape(H,W,4)[:,:,:3].astype(np.int32)
    res=[f"today {psnr(ref,rgb(run('big.rgba',f'{W}x{H}',f'{CONV},format=yuv420p','yuv420p'),W,H)):.2f}"]
    for fl in ("bicubic","bilinear","area","fast_bilinear","neighbor"):
        y=run("src.rgba","352x288",f"{CONV},format=yuv444p,scale={pw}:{ph}:flags=neighbor,pad={W}:{H}:{ox}:{oy}:black,scale=flags={fl},format=yuv420p","yuv420p")
        res.append(f"{fl} {psnr(ref,rgb(y,W,H)):.2f}")
    print(f"{name:12s} "+"  ".join(res),flush=True)
