// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/viewport/camera.hpp"
#include "fos/viewport/scene.hpp"
#include "fos/viewport/viewport_widget.hpp"
#include "material_textures.hpp"

#include <QOpenGLBuffer>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>

/// OpenGL 3.3 core renderer of a Scene (private to the viewport module). All methods require
/// the widget's GL context to be current.
namespace fos::viewport::detail {

/// Highlight colour of selected parts (sRGB 0..255).
inline constexpr int kHighlightR = 255, kHighlightG = 140, kHighlightB = 0;
/// Flat colour of section caps (sRGB 0..255).
inline constexpr int kCapR = 120, kCapG = 84, kCapB = 60;
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
    /// Sets up shaders and buffers. Returns false (renderer not ready, error explains why) when
    /// the context is older than `minimum` or a shader fails; render() then only clears to the
    /// background colour. `minimum` exists so tests can force the fallback on any machine.
    bool initialize(std::string& error, std::pair<int, int> minimum = {3, 3});
    bool ready() const noexcept { return ready_; }

    /// Uploads part geometry and the ground grid (on model change only).
    void uploadGeometry(const Scene& scene);
    /// Folder against which relative Material::texture paths are resolved (takes effect on the
    /// next uploadGeometry()).
    void setTextureDirectory(const QString& dir) { textureDirectory_ = dir; }
    /// Uploads per-part colour/visibility/selection (cheap; on state change).
    void uploadState(const Scene& scene);

    /// Draws one frame. `deviceWidth/Height` are the framebuffer size in device pixels.
    /// With `section`, part geometry (faces and edges, not the grid) is clipped by
    /// gl_ClipDistance[0]; in the shaded modes the cut faces are capped with kCapColor using the
    /// stencil buffer (parity of the clipped surfaces along each pixel ray) when the bound
    /// framebuffer has stencil bits, otherwise back faces are drawn so cut parts look open.
    void render(const Camera& camera, DisplayMode mode, int deviceWidth, int deviceHeight, double dpr,
                const std::optional<SectionPlane>& section = std::nullopt);
    /// True when the last render() with a section drew stencil caps.
    bool capsDrawn() const noexcept { return capsDrawn_; }

    /// Release the GL objects (context must be current).
    void destroy();

private:
    bool ready_ = false;
    QOpenGLShaderProgram meshProgram_, lineProgram_, colorProgram_;
    QOpenGLVertexArrayObject meshVao_, lineVao_, gridVao_, triadVao_;
    QOpenGLBuffer surfaceVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer meshVbo_{QOpenGLBuffer::VertexBuffer}, meshIbo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLBuffer lineVbo_{QOpenGLBuffer::VertexBuffer}, gridVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer triadVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject capVao_;
    QOpenGLBuffer capVbo_{QOpenGLBuffer::VertexBuffer};
    geometry::Aabb bounds_; // all parts, for the cap quad
    bool capsDrawn_ = false;
    int stencilBits();
    void drawCaps(const SectionPlane& section, const QMatrix4x4& viewProj);
    GLuint stateTexture_ = 0;
    GLuint materialTexture_ = 0; // GL_TEXTURE_2D_ARRAY of material images
    MaterialTextures textures_;
    QString textureDirectory_;
    std::array<int, kMaxTextureLayers> texLayer_{};
    std::array<float, kMaxTextureLayers> texAspect_{};
    void uploadTextures(const MaterialTextures::Layout& layout);
    int stateRows_ = 0;
    int meshIndexCount_ = 0, lineVertexCount_ = 0, gridVertexCount_ = 0;
};

} // namespace fos::viewport::detail
