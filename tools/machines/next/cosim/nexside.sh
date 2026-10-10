#!/bin/bash
# usage: side.sh file.nex [frames] -> /tmp/claude-501/cmp/side.png (ours | reference)
cd "$(dirname "$0")/../../.."
tools/machines/next/cosim/nexcmp.sh "$1" ${2:-250}
python3 -c "
from PIL import Image
a=Image.open('/tmp/claude-501/cmp/ours_last.png'); r=Image.open('/tmp/claude-501/cmp/ref_last.png')
c=Image.new('RGB',(a.width*2+8,a.height)); c.paste(a,(0,0)); c.paste(r,(a.width+8,0)); c=c.resize((c.width*3//4,c.height*3//4)); c.save('/tmp/claude-501/cmp/side.png')"
