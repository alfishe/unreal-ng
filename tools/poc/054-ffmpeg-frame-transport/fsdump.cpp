// fsdump SRC.rgba SW SH DW DH OUT.rgba : FrameScaler::ScaleInto output; prints the layout
#include "../../../core/recording/src/common/framescaler.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int, char** a)
{
    const uint32_t sw = atoi(a[2]), sh = atoi(a[3]), dw = atoi(a[4]), dh = atoi(a[5]);
    std::vector<uint8_t> src(size_t(sw) * sh * 4), dst(size_t(dw) * dh * 4);
    FILE* f = fopen(a[1], "rb"); if (fread(src.data(), 1, src.size(), f) != src.size()) return 1; fclose(f);
    FrameScaler::ScaleInto(src.data(), sw, sh, dst.data(), size_t(dw) * 4, dw, dh, false);
    const auto l = FrameScaler::ComputeLayout(sw, sh, dw, dh);
    printf("%u %u %u %u %u\n", l.width, l.height, l.offsetX, l.offsetY, l.scale);
    f = fopen(a[6], "wb"); fwrite(dst.data(), 1, dst.size(), f); fclose(f);
}
