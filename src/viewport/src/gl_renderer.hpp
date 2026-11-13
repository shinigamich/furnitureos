// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/viewport/camera.hpp"
#include "fos/viewport/scene.hpp"
#include "fos/viewport/viewport_widget.hpp"

#include <QOpenGLBuffer>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>

#include <memory>
#include <string>

/// OpenGL 3.3 core renderer of a Scene (private to the viewport module). All methods require
/// the widget's GL context to be current.
namespace fos::viewport::detail {

/// Highlight colour of selected parts (sRGB 0..255).
inline constexpr int kHighlightR = 255, kHighlightG = 140, kHighlightB = 0;
/// Background colour (sRGB 0..255).
inline constexpr int kBackgroundR = 236, kBackgroundG = 238, kBackgroundB = 242;

class GlRenderer : protected QOpenGLExtraFunctions {
public:
    GlRenderer() = default;
    ~GlRenderer();
    GlRenderer(const GlRenderer&) = delete;
    GlRenderer& operator=(const GlRenderer&) = delete;

    /// Compiles the shaders and creates the GL objects. Returns false (and fills `error`) when
    /// the context is older than OpenGL 3.3 / GLSL 330 or a shader fails; the renderer then
    /// draws nothing but the background.
    bool initialize(std::string& error);
    bool ready() const noexcept { return ready_; }

    /// Uploads part geometry and the ground grid (on model change only).
    void uploadGeometry(const Scene& scene);
    /// Uploads per-part colour/visibility/selection (cheap; on state change).
    void uploadState(const Scene& scene);

    /// Draws one frame. `deviceWidth/Height` are the framebuffer size in device pixels.
    void render(const Camera& camera, DisplayMode mode, int deviceWidth, int deviceHeight, double dpr);

    /// Release the GL objects (context must be current).
    void destroy();

private:
    bool ready_ = false;
    QOpenGLShaderProgram meshProgram_, lineProgram_, colorProgram_;
    QOpenGLVertexArrayObject meshVao_, lineVao_, gridVao_, triadVao_;
    QOpenGLBuffer meshVbo_{QOpenGLBuffer::VertexBuffer}, meshIbo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLBuffer lineVbo_{QOpenGLBuffer::VertexBuffer}, gridVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer triadVbo_{QOpenGLBuffer::VertexBuffer};
    GLuint stateTexture_ = 0;
    int stateRows_ = 0;
    int meshIndexCount_ = 0, lineVertexCount_ = 0, gridVertexCount_ = 0;
};

} // namespace fos::viewport::detail
