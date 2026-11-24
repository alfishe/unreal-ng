#pragma once

#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

#include "gpu/gpudevice.h"

#include "emulator/video/smoothlayers.h"
#include "emulator/video/smoothmotion.h"

class GpuPassTimes;

/// @brief Smooth motion's heavy steps on the GPU (smoothmotion::Device): the blend, the motion field's passes and the
/// composition as fragment passes over integer textures on the GPU layer (gpu/gpudevice.h: GL 3.3 core or Vulkan;
/// no compute: macOS GL stops at 4.1), the CPU
/// reference's steps to the bit. The search and the refinement run per block on the GPU; the votes between the passes
/// (a few hundred blocks) are counted on the CPU from the field read back, with the reference's own code. The
/// pictures come back into the caller's memory: the screen's CRT composition grades on the CPU.
///
/// LayerFlow (smoothlayers.h) the same way: the per-pixel passes over every vector on the GPU (a tile of one texture
/// per vector, all in one draw), the block decisions on the CPU with the reference's own code (LayerFlow::Decide) from the
/// counts read back; the aliases and regions rules' per-pixel questions answered by passes of their own, all at once.
///
/// Every pass costs a bounded number of reads per fragment, whatever the picture shows (LayerFlow::kMaxVectors
/// vectors, LayerFlow::kMaxAllowed per block, the cover rule's windows from per-row masks), and a draw is cut into
/// stripes of at most kMaxReadsPerDraw texel reads, each flushed: no submission of a slow GPU nears a system's
/// watchdog (Windows resets a GPU after ~2 s).
///
/// Every call on the device's thread (GL: its context current). A step returns false (the engine runs it on the CPU)
/// until Create() succeeded
class SmoothMotionGPU final : public smoothmotion::Device, private smoothmotion::LayerFlow::Evidence
{
public:
    ~SmoothMotionGPU() override;

    /// Compile the programs on `device` (it outlives this, or Destroy() comes first). @return false with the reason
    /// in Log()
    bool Create(gpu::Device* device);
    /// Delete the programs and textures (before the device goes; GL: its context current)
    void Destroy();
    bool IsCreated() const { return _device != nullptr; }
    const QString& Log() const { return _log; }
    /// The big textures back to 1 x 1 (smooth motion turned off); the next step allocates them again
    void ReleaseBuffers();
    /// Some texture is past 1 x 1: ReleaseBuffers has something to free
    bool HoldsBuffers() const { return _holdsBuffers; }

    /// The motion field on the GPU only when asked: UNREAL_SMOOTH_FIELDS=gpu at Create(), or SetFieldsOnGpu. Measured
    /// slower than the CPU's (three syncs, one fragment per block and chunk), and the composition takes the field from
    /// memory either way
    bool Estimate(const smoothmotion::Picture& a, const smoothmotion::Picture& b, int range,
                  smoothmotion::MotionField& field) override;
    void SetFieldsOnGpu(bool on) { _fieldsOnGpu = on; }
    bool FieldsOnGpu() const { return _fieldsOnGpu; }
    bool Blend(const smoothmotion::Picture& a, const smoothmotion::Picture& b, int w, uint32_t* out) override;
    bool Compose(const smoothmotion::MotionField& field, const smoothmotion::Picture& a, const smoothmotion::Picture& b,
                 int w, uint32_t* out, const uint8_t* mask) override;
    bool ComposeLayers(const smoothmotion::Picture& p, const smoothmotion::Picture& a, const smoothmotion::Picture& b,
                       const smoothmotion::MotionField& prev, const smoothmotion::MotionField& next, int w,
                       uint32_t* out) override;

    /// The GPU time of the passes, without the CPU's work between them: GpuPassTimes::Smooth segments (null: none).
    /// The caller opens and closes the measurement round a refresh
    void SetPassTimes(GpuPassTimes* times) { _times = times; }

    /// The last ComposeLayers: its vectors and the questions the block decisions asked the GPU
    struct LayerCounts
    {
        int vectors = 0;
        int ownAsks = 0;  ///< blocks the aliases rule asked about
        int edges = 0;    ///< region edges the regions rule asked about
    };
    const LayerCounts& LastLayerCounts() const { return _layerCounts; }

    /// A draw's texel reads at most: a longer pass is drawn in stripes, each flushed
    static constexpr int64_t kMaxReadsPerDraw = 256 << 20;
    /// Another bound for the stripes (tests: many stripes on a small picture)
    void SetMaxReadsPerDraw(int64_t reads) { _maxReadsPerDraw = reads; }

private:
    enum Texture
    {
        FrameA,
        FrameB,
        Base,      // Compose: what stays outside the mask
        Mask,      // R8UI
        Order,     // the search order, RG32I, one row
        FieldChunks,  // pass 1 per block and chunk of the search order, RGBA32I: vector, mismatches (capped)
        FieldOne,     // pass 1, RGBA32I per block
        FieldPing,
        FieldPong,
        Output,  // RGBA8UI
        // LayerFlow; the per-vector images are tiles, _tileCols a row, room for LayerFlow::kMaxVectors
        FrameP,
        Colour,         // R8UI: the palette index
        Changed,        // R8UI: a != b
        Explained,      // R8UI: a moving vector explains the change
        RowSums,        // RG16UI, tiled: the window sums along the row
        StillSums,      // RG16UI: the changes round the pixel, windows kRadius and kSupportRadius
        ExplainedSums,  // RG16UI: the explained changes, window kSupportRadius (green)
        FitBits,        // R8UI, tiled: fits, fitting, changes, the cover's balance + 1 (two bits)
        Sums,           // RGBA16UI, tiled: score, support, fits | usable << 1
        CoverMasks,     // RG32UI, tiled: the cover's balance wins, losses of the pixels x - 16 .. x + 15 of the row
        RowCounts,      // R32I, tiled (blocks wide x the picture's rows): per block row of pixels, fit | usable << 8
        Counts,         // RGBA32I: per block, four vectors a row of blocks: fit count | usable count << 8
        AskAt,          // R32I per block: the aliases rule's ask, -1
        Asks,           // RG32I, one row: block, vector
        Top,            // RGBA32I per pixel of the asked blocks: the three fitting vectors with the most support
        OwnAnswers,     // RGBA32I: per ask and vector s: own(s, none), own(c, s)
        BlockColours,   // R32I per pixel: its colour in its block and the four round it
        Edges,          // RGBA32I, one row: bi, bj, (di + 1) + 3 (dj + 1)
        EdgeAnswers,    // RGBA32I: per edge and vector, continued
        BlockInfo,      // RGBA32I per block: vector, tied, explains, allowed count
        Allowed,        // R32I: per block a row of kMaxAllowed
        Vectors,        // RG32I, one row
        TextureCount
    };

    void UploadPicture(Texture texture, const smoothmotion::Picture& picture);
    /// LayerFlow's three frames, those already on the GPU kept
    void UploadFrames(const smoothmotion::Picture& p, const smoothmotion::Picture& a, const smoothmotion::Picture& b);
    /// The copy of what a frame texture holds (FrameA, FrameB, FrameP), null for the others; with its id
    std::vector<uint32_t>* FrameCopy(Texture texture);
    uint64_t* FrameId(Texture texture);
    bool Holds(Texture texture, const smoothmotion::Picture& picture);
    void UploadMask(const uint8_t* mask, int width, int height);
    static gpu::Format FormatOf(Texture texture);
    int Width(Texture texture) const { return _textures[texture] ? _textures[texture]->Width() : 0; }
    int Height(Texture texture) const { return _textures[texture] ? _textures[texture]->Height() : 0; }
    /// The texture at exactly width x height (allocated again when its size changes), `data` (may be null) uploaded
    void Allocate(Texture texture, int width, int height, const void* data);
    /// The texture at least width x height (it only grows), `data` (may be null) uploaded into its corner
    void Grow(Texture texture, int width, int height, const void* data);
    /// Draw `program` into `target` (width x height); the inputs bound by the caller. `readsPerFragment`: the most
    /// texel reads a fragment does, the draw cut into stripes by it
    bool DrawInto(gpu::Program* program, Texture target, int width, int height, int readsPerFragment);
    /// From the target DrawInto drew last
    bool ReadField(int blocksWide, int blocksHigh, std::vector<smoothmotion::MotionField::Block>& out);
    bool ReadPicture(int width, int height, uint32_t* out);
    /// The sampler `name` of `program` reads `texture`
    void BindTexture(gpu::Program* program, const char* name, Texture texture);
    /// The window sums of the bits of `source`'s tiles (masks, radii per channel) into `target`, row pass then column
    /// pass; with `flags` (the vectors' tiles) what each vector fits and may take besides
    bool WindowSums(Texture source, unsigned mask0, int radius0, unsigned mask1, int radius1, bool flags,
                    Texture target, int width, int height);
    /// An RGBA32I texture's corner, four ints a texel
    bool ReadInts(Texture texture, int width, int height, std::vector<int32_t>& out);
    /// The frames, the vectors and the per-pixel evidence, the refresh's uniforms
    void BindLayerFields(gpu::Program* program);
    std::vector<std::unique_ptr<gpu::Program>*> Programs();
    /// The tiled textures for LayerFlow::kMaxVectors at the picture's size: allocated once per size
    bool AllocateLayers(int width, int height);
    bool ComposeLayersHere(const smoothmotion::Picture& p, const smoothmotion::Picture& a,
                           const smoothmotion::Picture& b, const smoothmotion::MotionField& prev,
                           const smoothmotion::MotionField& next, int w, uint32_t* out);
    /// A stretch of GPU work begins / ends (the times' segments)
    void GpuBegin();
    void GpuEnd();

    // LayerFlow::Evidence: the answers of passes of their own
    void Ask(const std::vector<std::pair<size_t, int>>& blocks,
             const std::vector<smoothmotion::LayerFlow::Edge>& edges) override;
    int Own(size_t block, int c, int s) override;
    bool Continues(size_t index, int vector) override;

    gpu::Device* _device = nullptr;
    std::unique_ptr<gpu::Program> _blend;
    std::unique_ptr<gpu::Program> _searchChunks;
    std::unique_ptr<gpu::Program> _search;
    std::unique_ptr<gpu::Program> _refine;
    std::unique_ptr<gpu::Program> _compose;
    std::unique_ptr<gpu::Program> _changed;
    std::unique_ptr<gpu::Program> _rowSums;
    std::unique_ptr<gpu::Program> _columnSums;
    std::unique_ptr<gpu::Program> _fit;
    std::unique_ptr<gpu::Program> _coverMasks;
    std::unique_ptr<gpu::Program> _explained;
    std::unique_ptr<gpu::Program> _rowCounts;
    std::unique_ptr<gpu::Program> _counts;
    std::unique_ptr<gpu::Program> _blockColours;
    std::unique_ptr<gpu::Program> _top;
    std::unique_ptr<gpu::Program> _own;
    std::unique_ptr<gpu::Program> _continues;
    std::unique_ptr<gpu::Program> _layers;
    std::unique_ptr<gpu::Texture> _textures[TextureCount];
    Texture _lastTarget = Output;  // what DrawInto drew last: the Read* calls read it
    int _maxSize = 0;
    // LayerFlow's refresh: the vectors, the counts read back, the answers
    smoothmotion::LayerFlow::Layers _layerSet;
    smoothmotion::LayerFlow::DecideScratch _decide;
    std::vector<int> _blockVector;
    std::vector<uint8_t> _tied;
    std::vector<int32_t> _allowedOf;
    std::vector<uint32_t> _palette;
    std::vector<uint8_t> _fitCounts, _usableCounts;
    std::vector<int32_t> _ints;
    std::vector<int> _askOf;  // per block: the ask's index, -1
    std::vector<int32_t> _ownAnswers, _edgeAnswers;
    std::vector<int32_t> _fieldValues;
    int _n = 0;
    int _weight = 0;
    int _tileCols = 1;
    int _layersWidth = 0, _layersHeight = 0;  // the size AllocateLayers allocated for
    std::vector<uint8_t> _colourAt;
    bool _answered = true;  // false: a question's pass failed, the refresh goes to the CPU
    LayerCounts _layerCounts;
    std::vector<uint32_t> _frameCopies[3];
    uint64_t _frameIds[3] = {};
    bool _fieldsOnGpu = false;
    int _orderRange = -1;
    int _orderCount = 0;
    GpuPassTimes* _times = nullptr;
    int64_t _maxReadsPerDraw = kMaxReadsPerDraw;
    bool _holdsBuffers = false;
    QString _log;
};
