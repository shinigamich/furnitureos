// SPDX-License-Identifier: AGPL-3.0-only
#include "material_textures.hpp"

#include "fos/core/log.hpp"

#include <QDir>
#include <QFileInfo>

namespace fos::viewport::detail {

MaterialTextures::Layout MaterialTextures::resolve(const std::vector<std::string>& refs, const QString& baseDirectory) {
    Layout out;
    out.layer.fill(-1);
    out.aspect.fill(1.f);
    const QDir base(baseDirectory.isEmpty() ? QDir::currentPath() : baseDirectory);
    for (std::size_t i = 0; i < refs.size(); ++i) {
        const QString ref = QString::fromStdString(refs[i]);
        if (i >= static_cast<std::size_t>(kMaxTextureLayers)) {
            if (!logged_.contains(ref)) {
                logged_.insert(ref);
                core::logWarning("viewport", "Texture '" + refs[i] + "' is not shown: at most " +
                                                 std::to_string(kMaxTextureLayers) +
                                                 " material textures are displayed; using the procedural grain.");
            }
            continue;
        }
        const QString path = QFileInfo(QDir::isAbsolutePath(ref) ? ref : base.filePath(ref)).absoluteFilePath();
        auto it = cache_.find(path);
        if (it == cache_.end()) {
            Entry e;
            QImage img(path);
            if (!img.isNull() && img.width() > 0 && img.height() > 0) {
                e.aspect = float(img.height()) / float(img.width());
                e.image = img.convertToFormat(QImage::Format_RGBA8888)
                              .scaled(kTextureLayerSize, kTextureLayerSize, Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation);
            }
            if (!e.image.isNull()) it = cache_.insert(path, e); // failures are retried next time
        }
        if (it == cache_.end()) {
            if (!logged_.contains(ref)) {
                logged_.insert(ref);
                core::logWarning("viewport", "Texture '" + refs[i] + "' could not be read as an image (looked for '" +
                                                 path.toStdString() + "'); using the procedural grain instead.");
            }
            continue;
        }
        out.layer[i] = static_cast<int>(out.images.size());
        out.aspect[i] = it->aspect;
        out.images.push_back(it->image);
    }
    return out;
}

} // namespace fos::viewport::detail
