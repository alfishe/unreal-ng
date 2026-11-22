import subprocess, sys, numpy as np
CONV="scale=in_range=full:out_range=limited:out_color_matrix=bt709"
def ff(inp, size, vf, pix):
    return np.frombuffer(subprocess.run(["ffmpeg","-v","error","-f","rawvideo","-pix_fmt","rgba","-s",size,"-i",inp,"-vf",vf,"-f","rawvideo","-pix_fmt",pix,"-"],capture_output=True,check=True).stdout,np.uint8)
FB=352*288*4
for name in sys.argv[1:]:
    open("full.rgba","wb").write(open(f"{name}.rgba","rb").read()[FB*100:FB*101])
    for sw,sh in ((352,288),(320,240),(256,192)):
        subprocess.run(["ffmpeg","-v","error","-y","-f","rawvideo","-pix_fmt","rgba","-s","352x288","-i","full.rgba","-vf",f"crop={sw}:{sh}","-f","rawvideo","src.rgba"],check=True)
        for dw,dh in ((3840,2160),(1920,1080)):
            pw,ph,ox,oy,k=map(int,subprocess.run(["./fsdump","src.rgba",str(sw),str(sh),str(dw),str(dh),"big.rgba"],capture_output=True,text=True).stdout.split())
            out=[]
            for mid,pix in (("yuv444p","yuv444p"),("yuv444p","yuv420p"),("yuv444p16le","yuv420p")):
                today=ff("big.rgba",f"{dw}x{dh}",f"{CONV},format={pix}",pix)
                new=ff("src.rgba",f"{sw}x{sh}",f"{CONV},format={mid},scale={pw}:{ph}:flags=neighbor,pad={dw}:{dh}:{ox}:{oy}:black,format={pix}",pix)
                d=np.abs(today.astype(int)-new.astype(int))
                out.append(f"{mid}->{pix}: {'EXACT' if not d.any() else f'diff {np.mean(d>0)*100:.3f}% max {d.max()}'}")
            print(f"{name:11s} {sw}x{sh}->{dw}x{dh} (pic {pw}x{ph}, k={k}): "+"; ".join(out),flush=True)
