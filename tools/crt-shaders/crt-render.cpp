/// @file crt-render.cpp
/// @brief unreal-qt's own CRT picture of still frames, for the gallery and the comparison with librashader
/// (tools/crt-shaders/README.md): the software screen's composition (CRTRenderer for none / basic, CRTBeamModel for
/// the shader models), which the GPU screen draws bit for bit.
///
/// usage: crt-render <profile key> <width>x<height> <in.png> <out.png> [<in.png> <out.png> ...]

#include <QImage>
#include <QString>
#include <cstdio>
#include <vector>

#include "widgets/crtbeammodel.h"
#include "widgets/crtrenderer.h"

int main(int argc, char** argv)
{
    if (argc < 5 || (argc - 3) % 2 != 0)
    {
        std::fprintf(stderr, "usage: crt-render <profile key> <width>x<height> <in.png> <out.png> [<in> <out> ...]\n");
        return 1;
    }
    const QString key = QString::fromUtf8(argv[1]);
    bool known = false;
    CRTProfile profile = CRTProfile::None;
    for (CRTProfile p : CRTProfileParams::AllProfiles())
    {
        if (CRTProfileParams::ProfileKey(p) == key)
        {
            profile = p;
            known = true;
        }
    }
    const QStringList size = QString::fromUtf8(argv[2]).split('x');
    const int width = size.size() == 2 ? size[0].toInt() : 0;
    const int height = size.size() == 2 ? size[1].toInt() : 0;
    if (!known || width <= 0 || height <= 0)
    {
        std::fprintf(stderr, "crt-render: unknown profile %s or bad size %s\n", argv[1], argv[2]);
        return 1;
    }

    const CRTProfileParams params = CRTProfileParams::FromProfile(profile);
    CRTRenderer renderer;
    CRTBeamModel model;
    for (int i = 3; i < argc; i += 2)
    {
        const QImage frame = QImage(QString::fromUtf8(argv[i])).convertToFormat(QImage::Format_RGBA8888);
        if (frame.isNull())
        {
            std::fprintf(stderr, "crt-render: cannot read %s\n", argv[i]);
            return 2;
        }
        CRTRenderer::Layout layout;
        layout.outWidth = width;
        layout.outHeight = height;
        layout.source = frame.rect();
        layout.frameWidth = frame.width();
        layout.frameHeight = frame.height();
        QImage out(width, height, QImage::Format_RGBA8888);
        const int stride = static_cast<int>(frame.bytesPerLine());
        if (CRTBeamModel::isModel(profile))
        {
            model.setLayout(layout, profile);
            model.prepare(frame.constBits(), stride);
            model.compose(out.bits(), static_cast<int>(out.bytesPerLine()));
        }
        else
        {
            renderer.setLayout(layout, params);
            renderer.grade(frame.constBits(), stride, params);
            renderer.compose(out.bits(), static_cast<int>(out.bytesPerLine()));
        }
        if (!out.convertToFormat(QImage::Format_RGB888).save(QString::fromUtf8(argv[i + 1])))
        {
            std::fprintf(stderr, "crt-render: cannot write %s\n", argv[i + 1]);
            return 2;
        }
    }
    return 0;
}
