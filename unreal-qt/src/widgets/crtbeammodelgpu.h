#pragma once

#include <QString>
#include <memory>

#include "crtbeammodel.h"
#include "gpu/gpudevice.h"

/// @brief CRTBeamModel on the GPU layer (GL or Vulkan): its tables as integer textures and CRTBeamModel::kShader on a
/// quad over the viewport, which must be exactly the layout's output size. The picture is compose()'s, bit for bit.
/// Every call on the device's thread
class CRTBeamModelGPU
{
public:
    /// Compile the program on `device` (which outlives it or its destroy()). @return false with the reason in log()
    bool create(gpu::Device* device);
    /// The device's objects go
    void destroy();
    bool isCreated() const { return _program != nullptr; }
    const QString& log() const { return _log; }

    /// The tables of the model's layout and profile: after setLayout() rebuilt them
    void uploadTables(const CRTBeamModel& model);
    /// The prepared frame (prepare(), the glow's columns not needed)
    void uploadFrame(const CRTBeamModel& model);
    /// The picture into `viewport` of `target` (GL rows), the layout's output size
    void draw(const CRTBeamModel& model, gpu::Texture& target, gpu::Rect viewport);

private:
    static constexpr int kTextures = 14;

    gpu::Device* _device = nullptr;
    std::unique_ptr<gpu::Program> _program;
    std::unique_ptr<gpu::Texture> _textures[kTextures];
    QString _log;
};
