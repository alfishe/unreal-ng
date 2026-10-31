#pragma once

#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QString>

#include <cstdint>
#include <vector>

#include "emulator/video/smoothlayers.h"
#include "emulator/video/smoothmotion.h"

/// @brief Smooth motion's heavy steps on the GPU (smoothmotion::Device): the blend, the motion field's passes and the
/// composition as fragment passes over integer textures (GL 3.3 core, no compute: macOS stops at 4.1), the CPU
/// reference's steps to the bit. The search and the refinement run per block on the GPU; the votes between the passes
/// (a few hundred blocks) are counted on the CPU from the field read back, with the reference's own code. The
/// pictures come back into the caller's memory: the screen's CRT composition grades on the CPU.
///
/// LayerFlow (smoothlayers.h) the same way: the per-pixel passes over every vector on the GPU (a tile of one texture
/// per vector, all in one draw), the block decisions on the CPU with the reference's own code (LayerFlow::Decide) from the
/// counts read back; the aliases and regions rules' per-pixel questions answered by passes of their own, all at once.
///
/// Every call needs the context it was created in current. A step returns false (the engine runs it on the CPU) until
/// create() succeeded
class SmoothMotionGPU final : public smoothmotion::Device, private smoothmotion::LayerFlow::Evidence
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
    bool ComposeLayers(const smoothmotion::Picture& p, const smoothmotion::Picture& a, const smoothmotion::Picture& b,
                       const smoothmotion::MotionField& prev, const smoothmotion::MotionField& next, int w,
                       uint32_t* out) override;

    /// The last ComposeLayers: its vectors and the questions the block decisions asked the GPU
    struct LayerCounts
    {
        int vectors = 0;
        int ownAsks = 0;  ///< blocks the aliases rule asked about
        int edges = 0;    ///< region edges the regions rule asked about
    };
    const LayerCounts& layerCounts() const { return _layerCounts; }

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
        // LayerFlow; the per-vector images are tiles, _tileCols a row
        FrameP,
        Colour,         // R8UI: the palette index
        Changed,        // R8UI: a != b
        Explained,      // R8UI: a moving vector explains the change
        RowSums,        // RG16UI, tiled: the window sums along the row
        StillSums,      // RG16UI: the changes round the pixel, windows kRadius and kSupportRadius
        ExplainedSums,  // RG16UI: the explained changes, window kSupportRadius (green)
        FitBits,        // R8UI, tiled: fits, fitting, changes, the cover's balance + 1 (two bits)
        Sums,           // RGBA16UI, tiled: score, support, fits | usable << 1
        RowCounts,      // R32I, tiled (blocks wide x the picture's rows): per block row of pixels, fit | usable << 8 |
                        // any cover balance << 16
        Counts,         // RGBA32I: per block, four vectors a row of blocks: fit count | usable count << 8
        AskAt,          // R32I per block: the aliases rule's ask, -1
        Asks,           // RG32I, one row: block, vector
        Top,            // RGBA32I per pixel of the asked blocks: the three fitting vectors with the most support
        OwnAnswers,     // RGBA32I: per ask and vector s: own(s, none), own(c, s)
        BlockColours,   // R32I per pixel: its colour in its block and the four round it
        Edges,          // RGBA32I, one row: bi, bj, (di + 1) + 3 (dj + 1)
        EdgeAnswers,    // RGBA32I: per edge and vector, continued
        BlockInfo,      // RGBA32I per block: vector, tied, explains, allowed count
        Allowed,        // R32I: per block a row of the most allowed
        Vectors,        // RG32I, one row
        TextureCount
    };

    void uploadPicture(Texture texture, const smoothmotion::Picture& picture);
    /// LayerFlow's three frames, those already on the GPU kept
    void uploadFrames(const smoothmotion::Picture& p, const smoothmotion::Picture& a, const smoothmotion::Picture& b);
    /// The copy of what a frame texture holds (FrameA, FrameB, FrameP), null for the others
    std::vector<uint32_t>* frameCopy(Texture texture);
    bool holds(Texture texture, const smoothmotion::Picture& picture);
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
    /// The window sums of the bits of `source`'s tiles (masks, radii per channel) into `target`, row pass then column
    /// pass; with `flags` (the vectors' tiles) what each vector fits and may take besides
    bool windowSums(Texture source, unsigned mask0, int radius0, unsigned mask1, int radius1, bool flags,
                    Texture target, int width, int height);
    bool readInts(int width, int height, std::vector<int32_t>& out);
    /// The frames, the vectors and the per-pixel evidence on units 0..7, the refresh's uniforms
    void bindLayerFields(QOpenGLShaderProgram* program);
    std::vector<QOpenGLShaderProgram*> programs() const;
    bool composeLayers(const smoothmotion::Picture& p, const smoothmotion::Picture& a, const smoothmotion::Picture& b,
                       const smoothmotion::MotionField& prev, const smoothmotion::MotionField& next, int w,
                       uint32_t* out);

    // LayerFlow::Evidence: the answers of passes of their own
    void Ask(const std::vector<std::pair<size_t, int>>& blocks,
             const std::vector<smoothmotion::LayerFlow::Edge>& edges) override;
    int Own(size_t block, int c, int s) override;
    bool Continues(size_t index, int vector) override;

    QOpenGLExtraFunctions* _gl = nullptr;
    QOpenGLShaderProgram* _blend = nullptr;
    QOpenGLShaderProgram* _search = nullptr;
    QOpenGLShaderProgram* _refine = nullptr;
    QOpenGLShaderProgram* _compose = nullptr;
    QOpenGLShaderProgram* _changed = nullptr;
    QOpenGLShaderProgram* _rowSums = nullptr;
    QOpenGLShaderProgram* _columnSums = nullptr;
    QOpenGLShaderProgram* _fit = nullptr;
    QOpenGLShaderProgram* _explained = nullptr;
    QOpenGLShaderProgram* _rowCounts = nullptr;
    QOpenGLShaderProgram* _counts = nullptr;
    QOpenGLShaderProgram* _blockColours = nullptr;
    QOpenGLShaderProgram* _top = nullptr;
    QOpenGLShaderProgram* _own = nullptr;
    QOpenGLShaderProgram* _continues = nullptr;
    QOpenGLShaderProgram* _layers = nullptr;
    QOpenGLVertexArrayObject _vao;
    unsigned _framebuffer = 0;
    unsigned _textures[TextureCount] = {};
    int _sizes[TextureCount][2] = {};
    int _maxSize = 0;
    // LayerFlow's refresh: the vectors, the counts read back, the answers
    smoothmotion::LayerFlow::Layers _layerSet;
    std::vector<uint8_t> _fitCounts, _usableCounts;
    std::vector<int32_t> _ints;
    std::vector<int> _askOf;  // per block: the ask's index, -1
    std::vector<int32_t> _ownAnswers, _edgeAnswers;
    int _n = 0;
    int _weight = 0;
    int _tileCols = 1;
    std::vector<uint8_t> _colourAt;
    bool _answered = true;  // false: a question's pass failed, the refresh goes to the CPU
    LayerCounts _layerCounts;
    std::vector<uint32_t> _readBack;
    std::vector<uint32_t> _frameCopies[3];
    int _orderRange = -1;
    int _orderCount = 0;
    QString _log;
};
