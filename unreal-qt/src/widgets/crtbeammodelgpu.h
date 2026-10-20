#pragma once

#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QString>

#include "crtbeammodel.h"

/// @brief CRTBeamModel on the GPU: its tables as integer textures and CRTBeamModel::kShader on a quad over the
/// viewport, which must be exactly the layout's output size. The picture is compose()'s, bit for bit. Every call needs
/// the context current (a 3.3 core one)
class CRTBeamModelGPU
{
public:
    /// Compile the program. @return false with the reason in log()
    bool create();
    /// Delete the GL objects (the context they were made in current)
    void destroy();
    bool isCreated() const { return _program != nullptr; }
    const QString& log() const { return _log; }

    /// The tables of the model's layout and profile: after setLayout() rebuilt them
    void uploadTables(const CRTBeamModel& model);
    /// The prepared frame (prepare(), the glow's columns not needed)
    void uploadFrame(const CRTBeamModel& model);
    /// The picture into the viewport
    void draw(const CRTBeamModel& model);

private:
    static constexpr int kTextures = 14;

    QOpenGLExtraFunctions* _gl = nullptr;
    QOpenGLShaderProgram* _program = nullptr;
    QOpenGLVertexArrayObject _vao;
    QOpenGLTexture* _textures[kTextures] = {};
    QString _log;
};
