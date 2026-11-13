// SPDX-License-Identifier: AGPL-3.0-only
#include "gl_renderer.hpp"

#include <QOpenGLContext>

#include <algorithm>
#include <cmath>
#include <vector>

namespace fos::viewport::detail {
namespace {

constexpr int kStateTexWidth = 1024;

// Per-part state lives in an RGBA8 texture (texel i = part i): rgb = material colour,
// a = flags (bit 0 visible, bit 1 selected). Geometry never changes on selection.
const char* kMeshVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aPart;
uniform mat4 uView;
uniform mat4 uProj;
uniform sampler2D uState;
uniform vec3 uHighlight;
out vec3 vNormal;
out vec3 vColor;
flat out int vSel;
void main() {
    int i = int(aPart + 0.5);
    vec4 s = texelFetch(uState, ivec2(i % 1024, i / 1024), 0);
    int flags = int(s.a * 255.0 + 0.5);
    vSel = (flags >> 1) & 1;
    vColor = vSel == 1 ? uHighlight : s.rgb;
    vNormal = mat3(uView) * aNormal;
    if ((flags & 1) == 0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)";

const char* kMeshFs = R"(#version 330 core
in vec3 vNormal;
in vec3 vColor;
flat in int vSel;
uniform vec3 uLightDir;
out vec4 fragColor;
void main() {
    float d = max(dot(normalize(vNormal), uLightDir), 0.0);
    float k = vSel == 1 ? 0.85 + 0.15 * d : 0.40 + 0.60 * d;
    fragColor = vec4(vColor * k, 1.0);
}
)";

const char* kLineVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aPart;
uniform mat4 uView;
uniform mat4 uProj;
uniform sampler2D uState;
uniform vec3 uHighlight;
uniform vec3 uEdgeColor;
out vec3 vColor;
void main() {
    int i = int(aPart + 0.5);
    vec4 s = texelFetch(uState, ivec2(i % 1024, i / 1024), 0);
    int flags = int(s.a * 255.0 + 0.5);
    vColor = ((flags >> 1) & 1) == 1 ? uHighlight * 0.75 : uEdgeColor;
    if ((flags & 1) == 0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)";

const char* kColorVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMvp;
out vec3 vColor;
void main() { vColor = aColor; gl_Position = uMvp * vec4(aPos, 1.0); }
)";

const char* kColorFs = R"(#version 330 core
in vec3 vColor;
out vec4 fragColor;
void main() { fragColor = vec4(vColor, 1.0); }
)";

QMatrix4x4 toQt(const Mat4& m) {
    // QMatrix4x4(const float*) takes row-major values; Mat4 is column-major.
    return QMatrix4x4(m.data()).transposed();
}

bool build(QOpenGLShaderProgram& p, const char* vs, const char* fs, std::string& error) {
    if (!p.addShaderFromSourceCode(QOpenGLShader::Vertex, vs) ||
        !p.addShaderFromSourceCode(QOpenGLShader::Fragment, fs) || !p.link()) {
        error = "Viewport shader compilation failed: " + p.log().toStdString();
        return false;
    }
    return true;
}

struct ColorVertex {
    float x, y, z, r, g, b;
};

std::vector<ColorVertex> makeGrid(const geometry::Aabb& parts) {
    // Grid on the floor (z = 0) covering the parts plus 500 mm, rounded to whole metres,
    // at least 2 m x 2 m. Minor lines every 100 mm (every 1000 mm beyond 20 m), major every 1 m.
    double x0 = -1000, x1 = 1000, y0 = -1000, y1 = 1000;
    if (!parts.empty()) {
        x0 = std::min(x0, std::floor((parts.min.x - 500) / 1000) * 1000);
        x1 = std::max(x1, std::ceil((parts.max.x + 500) / 1000) * 1000);
        y0 = std::min(y0, std::floor((parts.min.y - 500) / 1000) * 1000);
        y1 = std::max(y1, std::ceil((parts.max.y + 500) / 1000) * 1000);
    }
    const double step = std::max(x1 - x0, y1 - y0) > 20000 ? 1000 : 100;
    std::vector<ColorVertex> v;
    const float minor = 0.84f, major = 0.70f;
    auto line = [&](double ax, double ay, double bx, double by, float c) {
        v.push_back({float(ax), float(ay), 0, c, c, c + 0.02f});
        v.push_back({float(bx), float(by), 0, c, c, c + 0.02f});
    };
    for (double x = x0; x <= x1 + 1e-6; x += step)
        line(x, y0, x, y1, std::fmod(std::abs(x), 1000.0) < 1e-6 ? major : minor);
    for (double y = y0; y <= y1 + 1e-6; y += step)
        line(x0, y, x1, y, std::fmod(std::abs(y), 1000.0) < 1e-6 ? major : minor);
    return v;
}

} // namespace

GlRenderer::~GlRenderer() = default;

bool GlRenderer::initialize(std::string& error) {
    ready_ = false;
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        error = "Viewport: no current OpenGL context.";
        return false;
    }
    const auto fmt = ctx->format();
    if (ctx->isOpenGLES() || fmt.version() < qMakePair(3, 3)) {
        error = "Viewport needs OpenGL 3.3 (desktop); got " + std::string(ctx->isOpenGLES() ? "OpenGL ES " : "OpenGL ") +
                std::to_string(fmt.majorVersion()) + "." + std::to_string(fmt.minorVersion()) +
                ". The 3D view stays empty; picking and all other functions keep working.";
        return false;
    }
    initializeOpenGLFunctions();
    if (!build(meshProgram_, kMeshVs, kMeshFs, error) || !build(lineProgram_, kLineVs, kColorFs, error) ||
        !build(colorProgram_, kColorVs, kColorFs, error))
        return false;

    for (auto* b : {&meshVbo_, &meshIbo_, &lineVbo_, &gridVbo_, &triadVbo_}) {
        b->create();
        b->setUsagePattern(QOpenGLBuffer::StaticDraw);
    }
    meshVao_.create();
    lineVao_.create();
    gridVao_.create();
    triadVao_.create();

    meshVao_.bind();
    meshVbo_.bind();
    meshIbo_.bind();
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), nullptr);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(3 * sizeof(float)));
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(6 * sizeof(float)));
    meshVao_.release();

    lineVao_.bind();
    lineVbo_.bind();
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), nullptr);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(3 * sizeof(float)));
    lineVao_.release();

    for (auto [vao, vbo] : {std::pair{&gridVao_, &gridVbo_}, std::pair{&triadVao_, &triadVbo_}}) {
        vao->bind();
        vbo->bind();
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ColorVertex), nullptr);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ColorVertex), reinterpret_cast<void*>(3 * sizeof(float)));
        vao->release();
    }
    const ColorVertex triad[6] = {{0, 0, 0, 0.85f, 0.1f, 0.1f}, {1, 0, 0, 0.85f, 0.1f, 0.1f},
                                  {0, 0, 0, 0.1f, 0.65f, 0.1f}, {0, 1, 0, 0.1f, 0.65f, 0.1f},
                                  {0, 0, 0, 0.1f, 0.2f, 0.9f},  {0, 0, 1, 0.1f, 0.2f, 0.9f}};
    triadVbo_.bind();
    triadVbo_.allocate(triad, sizeof(triad));
    triadVbo_.release();

    glGenTextures(1, &stateTexture_);
    glBindTexture(GL_TEXTURE_2D, stateTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    ready_ = true;
    return true;
}

void GlRenderer::uploadGeometry(const Scene& scene) {
    if (!ready_) return;
    const auto buf = scene.buildBuffers();
    meshVbo_.bind();
    meshVbo_.allocate(buf.vertices.data(), static_cast<int>(buf.vertices.size() * sizeof(MeshVertex)));
    meshVbo_.release();
    meshVao_.bind(); // the element buffer binding is VAO state
    meshIbo_.bind();
    meshIbo_.allocate(buf.indices.data(), static_cast<int>(buf.indices.size() * sizeof(std::uint32_t)));
    meshVao_.release();
    lineVbo_.bind();
    lineVbo_.allocate(buf.lines.data(), static_cast<int>(buf.lines.size() * sizeof(LineVertex)));
    lineVbo_.release();
    meshIndexCount_ = static_cast<int>(buf.indices.size());
    lineVertexCount_ = static_cast<int>(buf.lines.size());

    const auto grid = makeGrid(scene.allBounds());
    gridVbo_.bind();
    gridVbo_.allocate(grid.data(), static_cast<int>(grid.size() * sizeof(ColorVertex)));
    gridVbo_.release();
    gridVertexCount_ = static_cast<int>(grid.size());
}

void GlRenderer::uploadState(const Scene& scene) {
    if (!ready_) return;
    const std::size_t n = scene.parts().size();
    const int rows = std::max(1, static_cast<int>((n + kStateTexWidth - 1) / kStateTexWidth));
    std::vector<std::uint8_t> texels(static_cast<std::size_t>(rows) * kStateTexWidth * 4, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& c = scene.parts()[i].color;
        std::uint8_t* t = &texels[i * 4];
        t[0] = c.r;
        t[1] = c.g;
        t[2] = c.b;
        t[3] = static_cast<std::uint8_t>((scene.isVisible(i) ? 1 : 0) | (scene.isSelected(i) ? 2 : 0));
    }
    glBindTexture(GL_TEXTURE_2D, stateTexture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (rows != stateRows_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kStateTexWidth, rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
        stateRows_ = rows;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kStateTexWidth, rows, GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GlRenderer::render(const Camera& camera, DisplayMode mode, int dw, int dh, double dpr) {
    glViewport(0, 0, dw, dh);
    glClearColor(kBackgroundR / 255.f, kBackgroundG / 255.f, kBackgroundB / 255.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!ready_) return;

    const QMatrix4x4 view = toQt(camera.viewMatrix());
    const QMatrix4x4 proj = toQt(camera.projectionMatrix());
    const QVector3D highlight(kHighlightR / 255.f, kHighlightG / 255.f, kHighlightB / 255.f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    colorProgram_.bind();
    colorProgram_.setUniformValue("uMvp", proj * view);
    gridVao_.bind();
    glDrawArrays(GL_LINES, 0, gridVertexCount_);
    gridVao_.release();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, stateTexture_);
    if (mode != DisplayMode::Wireframe && meshIndexCount_ > 0) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        // Push faces back so that coincident edge lines win the depth test (no z-fighting).
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);
        meshProgram_.bind();
        meshProgram_.setUniformValue("uView", view);
        meshProgram_.setUniformValue("uProj", proj);
        meshProgram_.setUniformValue("uState", 0);
        meshProgram_.setUniformValue("uHighlight", highlight);
        meshProgram_.setUniformValue("uLightDir", QVector3D(0.35f, 0.55f, 0.76f).normalized());
        meshVao_.bind();
        glDrawElements(GL_TRIANGLES, meshIndexCount_, GL_UNSIGNED_INT, nullptr);
        meshVao_.release();
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_CULL_FACE);
    }
    if (mode != DisplayMode::Shaded && lineVertexCount_ > 0) {
        lineProgram_.bind();
        lineProgram_.setUniformValue("uView", view);
        lineProgram_.setUniformValue("uProj", proj);
        lineProgram_.setUniformValue("uState", 0);
        lineProgram_.setUniformValue("uHighlight", highlight);
        lineProgram_.setUniformValue("uEdgeColor", mode == DisplayMode::Wireframe ? QVector3D(0.2f, 0.22f, 0.26f)
                                                                                 : QVector3D(0.25f, 0.22f, 0.18f));
        lineVao_.bind();
        glDrawArrays(GL_LINES, 0, lineVertexCount_);
        lineVao_.release();
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    // Axis triad (X red, Y green, Z blue) in the bottom-left corner: rotation only.
    const int size = static_cast<int>(std::lround(90 * dpr));
    glViewport(0, 0, size, size);
    glDisable(GL_DEPTH_TEST);
    QMatrix4x4 rot = view;
    rot.setColumn(3, QVector4D(0, 0, 0, 1));
    QMatrix4x4 ortho;
    ortho.ortho(-1.3f, 1.3f, -1.3f, 1.3f, -2.f, 2.f);
    colorProgram_.bind();
    colorProgram_.setUniformValue("uMvp", ortho * rot);
    triadVao_.bind();
    glDrawArrays(GL_LINES, 0, 6);
    triadVao_.release();
    colorProgram_.release();
    glViewport(0, 0, dw, dh);
}

void GlRenderer::destroy() {
    if (stateTexture_) glDeleteTextures(1, &stateTexture_);
    stateTexture_ = 0;
    for (auto* b : {&meshVbo_, &meshIbo_, &lineVbo_, &gridVbo_, &triadVbo_}) b->destroy();
    for (auto* v : {&meshVao_, &lineVao_, &gridVao_, &triadVao_}) v->destroy();
    meshProgram_.removeAllShaders();
    lineProgram_.removeAllShaders();
    colorProgram_.removeAllShaders();
    ready_ = false;
}

} // namespace fos::viewport::detail
