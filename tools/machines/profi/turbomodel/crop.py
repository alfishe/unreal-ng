# usage: crop.py pdf page x0 y0 x1 y1 out [dpi]   (coords in 60-dpi overview pixels)
import sys, fitz
pdf,pg,x0,y0,x1,y1,out=sys.argv[1:8]; dpi=int(sys.argv[8]) if len(sys.argv)>8 else 200
k=72/60
r=fitz.Rect(float(x0)*k,float(y0)*k,float(x1)*k,float(y1)*k)
fitz.open(pdf)[int(pg)].get_pixmap(dpi=dpi,clip=r).save(out)
