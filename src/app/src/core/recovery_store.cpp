// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/recovery_store.hpp"

#include "fos/app/version.hpp"
#include "fos/formats/atomic_file.hpp"
#include "fos/project/file.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace fos::app {

namespace fs = std::filesystem;

namespace {

constexpr std::uintmax_t kMaxSidecarBytes = 64 * 1024;

QString toQString(const fs::path& p) { return QString::fromStdU16String(p.u16string()); }

QString normalisedPath(const fs::path& p) {
    return QDir::cleanPath(QFileInfo(toQString(p)).absoluteFilePath());
}

/// Reads at most kMaxSidecarBytes; empty when missing, unreadable or larger than the cap.
QByteArray readSmallFile(const fs::path& p) {
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec || size > kMaxSidecarBytes) return {};
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return QByteArray::fromStdString(bytes);
}

QDateTime modifiedUtc(const fs::path& p) { return QFileInfo(toQString(p)).lastModified().toUTC(); }

bool hasSuffix(const std::string& s, const std::string& suffix) {
    return s.size() > suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

QByteArray serializeRecoverySidecar(const RecoverySidecar& sidecar) {
    QJsonObject o;
    o.insert(QStringLiteral("kind"), QString::fromLatin1(kRecoverySidecarKind));
    o.insert(QStringLiteral("version"), kRecoverySidecarVersion);
    o.insert(QStringLiteral("originalPath"), sidecar.originalPath);
    o.insert(QStringLiteral("projectName"), sidecar.projectName);
    o.insert(QStringLiteral("savedAt"), sidecar.savedAt.toUTC().toString(Qt::ISODateWithMs));
    o.insert(QStringLiteral("appVersion"), sidecar.appVersion);
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

std::optional<RecoverySidecar> parseRecoverySidecar(const QByteArray& json) {
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    const QJsonObject o = doc.object();
    if (o.value(QStringLiteral("kind")).toString() != QLatin1String(kRecoverySidecarKind)) return std::nullopt;
    const auto optionalString = [&o](const char* key, QString& out) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined() || v.isNull()) return true;
        if (!v.isString()) return false;
        out = v.toString();
        return true;
    };
    RecoverySidecar s;
    QString savedAt;
    if (!optionalString("originalPath", s.originalPath) || !optionalString("projectName", s.projectName) ||
        !optionalString("appVersion", s.appVersion) || !optionalString("savedAt", savedAt))
        return std::nullopt;
    s.savedAt = QDateTime::fromString(savedAt, Qt::ISODateWithMs);
    if (!s.savedAt.isValid()) return std::nullopt;
    s.savedAt = s.savedAt.toUTC();
    return s;
}

std::string recoveryKey(const fs::path& originalPath) {
    const QByteArray hash =
        QCryptographicHash::hash(normalisedPath(originalPath).toUtf8(), QCryptographicHash::Sha256).toHex();
    return hash.left(16).toStdString();
}

std::string untitledRecoveryKey(const std::string& untitledId) {
    std::string clean;
    for (char c : untitledId)
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') clean += c;
    return "untitled-" + (clean.empty() ? std::string("unnamed") : clean);
}

RecoveryStore::RecoveryStore(fs::path directory) : dir_(std::move(directory)) {}

fs::path RecoveryStore::fileFor(const std::string& key) const { return dir_ / (key + project::kProjectFileExtension); }
fs::path RecoveryStore::sidecarFor(const std::string& key) const { return dir_ / (key + ".json"); }

core::Result<fs::path> RecoveryStore::write(const std::string& key, const project::Project& project,
                                            const fs::path& originalPath, const QDateTime& savedAtUtc) const {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    if (ec)
        return core::makeError(core::ErrorCode::IoError, "Cannot create the recovery folder '" + dir_.string() +
                                                             "': " + ec.message() + ".");
    // The project's own backupCount is not used: a recovery copy never has backups (and the
    // project setting is kept unchanged inside the copy).
    formats::SaveOptions options;
    options.backupCount = 0;
    const fs::path file = fileFor(key);
    if (auto r = formats::saveDocument(file, project::toDocument(project), options); !r) return r.error();

    RecoverySidecar sidecar;
    sidecar.originalPath = originalPath.empty() ? QString() : normalisedPath(originalPath);
    sidecar.projectName = QString::fromStdString(project.metadata.name);
    sidecar.savedAt = savedAtUtc.toUTC();
    sidecar.appVersion = QString::fromLatin1(applicationVersion());
    const QByteArray json = serializeRecoverySidecar(sidecar);
    if (auto r = formats::atomicWriteFile(sidecarFor(key), std::string_view(json.constData(), static_cast<std::size_t>(json.size())), options);
        !r)
        return r.error();
    return file;
}

std::optional<RecoveryEntry> RecoveryStore::find(const std::string& key) const {
    const fs::path file = fileFor(key);
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return std::nullopt;
    RecoveryEntry e;
    e.key = key;
    e.file = file;
    e.sidecar = sidecarFor(key);
    if (const auto s = parseRecoverySidecar(readSmallFile(e.sidecar))) {
        e.sidecarValid = true;
        e.originalPath = s->originalPath;
        e.projectName = s->projectName;
        e.savedAt = s->savedAt;
        e.appVersion = s->appVersion;
    } else {
        e.savedAt = modifiedUtc(file);
    }
    return e;
}

std::vector<RecoveryEntry> RecoveryStore::list() const {
    std::vector<RecoveryEntry> out;
    std::error_code ec;
    if (!fs::is_directory(dir_, ec)) return out;
    const std::string ext = project::kProjectFileExtension;
    for (fs::directory_iterator it(dir_, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        // Only "<key>.fosp"; atomic-write temporaries ("<key>.fosp.tmp-...") do not match.
        if (!hasSuffix(name, ext) || name.front() == '.') continue;
        if (auto e = find(name.substr(0, name.size() - ext.size()))) out.push_back(std::move(*e));
    }
    std::sort(out.begin(), out.end(), [](const RecoveryEntry& a, const RecoveryEntry& b) {
        if (a.savedAt != b.savedAt) return a.savedAt > b.savedAt;
        return a.key < b.key;
    });
    return out;
}

bool RecoveryStore::remove(const std::string& key) const {
    std::error_code ec;
    fs::remove(fileFor(key), ec);
    fs::remove(sidecarFor(key), ec);
    return !fs::exists(fileFor(key), ec) && !fs::exists(sidecarFor(key), ec);
}

} // namespace fos::app
