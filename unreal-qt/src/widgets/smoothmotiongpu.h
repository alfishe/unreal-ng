#pragma once

#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QString>

#include <cstdint>
#include <vector>

#include "emulator/video/smoothmotion.h"

/// @brief Smooth motion's heavy steps on the GPU (smoothmotion::Device): the blend, the motion field's passes and the
/// composition as fragment passes over integer textures (GL 3.3 core, no compute: macOS stops at 4.1), the CPU
/// reference's steps to the bit. The search and the refinement run per block on the GPU; the votes between the passes
/// (a few hundred blocks) are counted on the CPU from the field read back, with the reference's own code. The
/// pictures come back into the caller's memory: the screen's CRT composition grades on the CPU.
///
/// Every call needs the context it was created in current. A step returns false (the engine runs it on the CPU) until
/// create() succeeded
class SmoothMotionGPU final : public smoothmotion::Device
{
public:
    ~SmoothMotionGPU() override;

    /// Compile the programs, the context current. @return false with the reason in log()
    bool create();
    /// Delete the GL objects (the context they were made in current)
    void destroy();
    bool isCreated() const { return _gl != nullptr; }
    const QString& log() const { return _log; }

    bool Estimate(const smoothmotion::Picture& a, const smoothmotion::Picture& b, int range,
                  smoothmotion::MotionField& field) override;
    bool Blend(const smoothmotion::Picture& a, const smoothmotion::Picture& b, int w, uint32_t* out) override;
    bool Compose(const smoothmotion::MotionField& field, const smoothmotion::Picture& a, const smoothmotion::Picture& b,
                 int w, uint32_t* out, const uint8_t* mask) override;

private:
    enum Texture
    {
        FrameA,
        FrameB,
        Base,      // Compose: what stays outside the mask
        Mask,      // R8UI
        Order,     // the search order, RG32I, one row
        FieldOne,  // pass 1, RGBA32I per block
        FieldPing,
        FieldPong,
        Output,  // RGBA8UI
        TextureCount
    };

    void uploadPicture(Texture texture, const smoothmotion::Picture& picture);
    void uploadMask(const uint8_t* mask, int width, int height);
    /// (Re)allocate an integer texture
    void allocate(Texture texture, int width, int height, unsigned internalFormat, unsigned format, unsigned type,
                  const void* data);
    /// Draw `program` into `target` (width x height); the inputs bound by the caller
    bool drawInto(QOpenGLShaderProgram* program, Texture target, int width, int height);
    /// From the target drawInto left bound
    bool readField(int blocksWide, int blocksHigh, std::vector<smoothmotion::MotionField::Block>& out);
    bool readPicture(int width, int height, uint32_t* out);
    void bindTexture(int unit, Texture texture);

    QOpenGLExtraFunctions* _gl = nullptr;
    QOpenGLShaderProgram* _blend = nullptr;
    QOpenGLShaderProgram* _search = nullptr;
    QOpenGLShaderProgram* _refine = nullptr;
    QOpenGLShaderProgram* _compose = nullptr;
    QOpenGLVertexArrayObject _vao;
    unsigned _framebuffer = 0;
    unsigned _textures[TextureCount] = {};
    int _sizes[TextureCount][2] = {};
    std::vector<uint32_t> _readBack;
    int _orderRange = -1;
    int _orderCount = 0;
    QString _log;
};
