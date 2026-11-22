// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QHash>
#include <QImage>
#include <QSet>
#include <QString>

#include <array>
#include <string>
#include <vector>

/// Image textures of materials (Material::texture) for the Realistic display mode (private to
/// the viewport module, Qt GUI only, no GL). Resolves, loads and caches the images; the
/// renderer uploads them as layers of one mip-mapped GL_TEXTURE_2D_ARRAY.
namespace fos::viewport::detail {

/// Maximum number of distinct textures shown (more are drawn with the procedural grain).
inline constexpr int kMaxTextureLayers = 16;
/// Every image is resampled to kTextureLayerSize x kTextureLayerSize texels.
inline constexpr int kTextureLayerSize = 1024;
/// Length (mm) one image covers along the grain (image x axis); the image y axis covers
/// kTextureTileMm * height / width mm across the grain (aspect ratio kept).
inline constexpr double kTextureTileMm = 1000.0;

class MaterialTextures {
public:
    struct Layout {
        std::vector<QImage> images;              ///< RGBA8888, kTextureLayerSize², one per layer.
        std::array<int, kMaxTextureLayers> layer; ///< Scene texture index -> layer, -1 = not loaded.
        std::array<float, kMaxTextureLayers> aspect; ///< Scene texture index -> height / width.
    };
    /// `refs` = Scene::textures(). A reference is used as is when absolute, otherwise relative to
    /// `baseDirectory` (the project folder; the working directory when empty). Unreadable files
    /// and library keys log one warning per reference (category "viewport") and get layer -1.
    Layout resolve(const std::vector<std::string>& refs, const QString& baseDirectory);

private:
    struct Entry {
        QImage image; // resampled; null = unreadable
        float aspect = 1;
    };
    QHash<QString, Entry> cache_; // by resolved absolute path
    QSet<QString> logged_;
};

} // namespace fos::viewport::detail
