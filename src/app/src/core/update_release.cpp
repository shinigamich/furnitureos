// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/app/update_release.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>

namespace fos::app {
namespace {

using core::ErrorCode;
using core::makeError;

std::string s(const QString& q) { return q.toStdString(); }

bool isSha256Hex(const QString& h) {
    return h.size() == 64 && std::all_of(h.begin(), h.end(), [](QChar c) {
               return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
           });
}

std::optional<ReleaseInfo> releaseFromJson(const QJsonObject& o) {
    ReleaseInfo r;
    r.tag = o.value(QLatin1String("tag_name")).toString();
    const auto v = parseSemVer(r.tag.toStdString());
    if (!v) return std::nullopt;
    r.version = *v;
    r.prerelease = o.value(QLatin1String("prerelease")).toBool();
    r.draft = o.value(QLatin1String("draft")).toBool();
    r.htmlUrl = o.value(QLatin1String("html_url")).toString();
    r.publishedAt = o.value(QLatin1String("published_at")).toString();
    for (const QJsonValue& av : o.value(QLatin1String("assets")).toArray()) {
        const QJsonObject a = av.toObject();
        ReleaseAsset asset;
        asset.name = a.value(QLatin1String("name")).toString();
        asset.size = static_cast<qint64>(a.value(QLatin1String("size")).toDouble());
        asset.downloadUrl = QUrl(a.value(QLatin1String("browser_download_url")).toString());
        if (asset.name.isEmpty() || !asset.downloadUrl.isValid() || asset.downloadUrl.scheme() != QLatin1String("https"))
            continue; // only https downloads are ever used
        r.assets.push_back(std::move(asset));
    }
    return r;
}

} // namespace

QUrl releasesApiUrl(bool includePrereleases) {
    const QString base = QStringLiteral("https://api.github.com/repos/%1/releases").arg(QLatin1String(kUpdateRepository));
    return QUrl(includePrereleases ? base + QStringLiteral("?per_page=30") : base + QStringLiteral("/latest"));
}

const ReleaseAsset* ReleaseInfo::findAsset(const QString& name) const {
    for (const ReleaseAsset& a : assets)
        if (a.name == name) return &a;
    return nullptr;
}

core::Result<std::vector<ReleaseInfo>> parseGitHubReleases(const QByteArray& json) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError)
        return makeError(ErrorCode::ParseError, "The GitHub release list is not valid JSON (" + s(err.errorString()) +
                                                    " at byte " + std::to_string(err.offset) + ").");
    std::vector<QJsonObject> objects;
    if (doc.isArray()) {
        for (const QJsonValue& v : doc.array())
            if (v.isObject()) objects.push_back(v.toObject());
    } else if (doc.isObject()) {
        const QJsonObject o = doc.object();
        if (!o.contains(QLatin1String("tag_name"))) {
            const QString message = o.value(QLatin1String("message")).toString();
            return makeError(ErrorCode::ParseError,
                             message.isEmpty() ? std::string("The GitHub answer is not a release (no tag_name).")
                                               : "GitHub answered: " + s(message));
        }
        objects.push_back(o);
    } else {
        return makeError(ErrorCode::ParseError, "The GitHub release list is neither a JSON object nor an array.");
    }
    std::vector<ReleaseInfo> out;
    for (const QJsonObject& o : objects)
        if (auto r = releaseFromJson(o)) out.push_back(std::move(*r));
    return out;
}

std::optional<ReleaseInfo> selectUpdate(const std::vector<ReleaseInfo>& releases, const SemVer& current,
                                        bool includePrereleases) {
    const ReleaseInfo* best = nullptr;
    for (const ReleaseInfo& r : releases) {
        if (r.draft) continue;
        if ((r.prerelease || r.version.isPrerelease()) && !includePrereleases) continue;
        if (compareSemVer(r.version, current) <= 0) continue;
        if (!r.findAsset(QLatin1String(kUpdateManifestName)) || !r.findAsset(QLatin1String(kUpdateChecksumsName)))
            continue;
        if (!best || compareSemVer(best->version, r.version) < 0) best = &r;
    }
    if (!best) return std::nullopt;
    return *best;
}

const UpdateAsset* UpdateManifest::asset(const char* key) const {
    const auto it = assets.find(QString::fromLatin1(key));
    return it == assets.end() ? nullptr : &it->second;
}

core::Result<UpdateManifest> parseUpdateManifest(const QByteArray& json, const ReleaseInfo& release) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return makeError(ErrorCode::ParseError, "update.json of release " + s(release.tag) + " is not a valid JSON object.");
    const QJsonObject o = doc.object();
    UpdateManifest m;
    m.versionText = o.value(QLatin1String("version")).toString();
    const auto v = parseSemVer(m.versionText.toStdString());
    if (!v)
        return makeError(ErrorCode::ParseError, "update.json of release " + s(release.tag) + " has no valid \"version\" ('" +
                                                    s(m.versionText) + "').");
    if (compareSemVer(*v, release.version) != 0)
        return makeError(ErrorCode::CorruptData, "update.json says version " + v->toString() + " but the release tag is " +
                                                     s(release.tag) + "; the release is inconsistent and is not offered.");
    m.version = *v;
    m.published = o.value(QLatin1String("published")).toString();
    m.notes = o.value(QLatin1String("notes")).toString();
    const QJsonObject assets = o.value(QLatin1String("assets")).toObject();
    for (auto it = assets.begin(); it != assets.end(); ++it) {
        const QJsonObject a = it.value().toObject();
        UpdateAsset u;
        u.name = a.value(QLatin1String("name")).toString();
        u.sha256 = a.value(QLatin1String("sha256")).toString().toLower();
        u.size = static_cast<qint64>(a.value(QLatin1String("size")).toDouble());
        const std::string key = s(it.key());
        if (u.name.isEmpty() || u.name.contains(QLatin1Char('/')) || u.name.contains(QLatin1Char('\\')))
            return makeError(ErrorCode::CorruptData, "update.json asset '" + key + "' has no valid file name.");
        if (!isSha256Hex(u.sha256))
            return makeError(ErrorCode::CorruptData, "update.json asset '" + key + "' (" + s(u.name) +
                                                         ") has no valid SHA-256 (64 hex digits expected).");
        if (u.size <= 0)
            return makeError(ErrorCode::CorruptData, "update.json asset '" + key + "' (" + s(u.name) + ") has no positive size.");
        const ReleaseAsset* ra = release.findAsset(u.name);
        if (!ra)
            return makeError(ErrorCode::NotFound, "update.json lists " + s(u.name) + " but release " + s(release.tag) +
                                                      " has no such asset.");
        if (ra->size != u.size)
            return makeError(ErrorCode::CorruptData, s(u.name) + " is " + std::to_string(ra->size) +
                                                         " bytes on GitHub but update.json says " + std::to_string(u.size) + " bytes.");
        u.downloadUrl = ra->downloadUrl;
        m.assets.emplace(it.key(), std::move(u));
    }
    return m;
}

core::Result<std::map<QString, QString>> parseSha256Sums(const QByteArray& text) {
    std::map<QString, QString> out;
    const QList<QByteArray> lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = QString::fromUtf8(lines[i]).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const std::string where = "SHA256SUMS.txt line " + std::to_string(i + 1);
        if (line.size() < 66 || line.at(64) != QLatin1Char(' '))
            return makeError(ErrorCode::ParseError, where + " is not '<sha256>  <file name>'.");
        const QString hash = line.left(64).toLower();
        if (!isSha256Hex(hash)) return makeError(ErrorCode::ParseError, where + " does not start with 64 hex digits.");
        QString name = line.mid(65);
        if (name.startsWith(QLatin1Char(' ')) || name.startsWith(QLatin1Char('*'))) name = name.mid(1);
        if (name.isEmpty()) return makeError(ErrorCode::ParseError, where + " has no file name.");
        if (!out.emplace(name, hash).second)
            return makeError(ErrorCode::CorruptData, where + " lists " + s(name) + " a second time.");
    }
    return out;
}

core::Result<void> checkChecksumsAgree(const UpdateAsset& asset, const std::map<QString, QString>& sums) {
    const auto it = sums.find(asset.name);
    if (it == sums.end())
        return makeError(ErrorCode::NotFound, "SHA256SUMS.txt has no entry for " + s(asset.name) + "; the update is refused.");
    if (it->second != asset.sha256)
        return makeError(ErrorCode::CorruptData, "update.json and SHA256SUMS.txt disagree on the SHA-256 of " + s(asset.name) +
                                                     " (" + s(asset.sha256) + " vs " + s(it->second) +
                                                     "); the update is refused.");
    return {};
}

core::Result<void> verifyDownload(qint64 actualSize, const QString& actualSha256, const UpdateAsset& asset,
                                  const std::map<QString, QString>& sums) {
    if (actualSize != asset.size)
        return makeError(ErrorCode::CorruptData, "The download of " + s(asset.name) + " is " + std::to_string(actualSize) +
                                                     " bytes but " + std::to_string(asset.size) + " bytes were expected.");
    if (auto agree = checkChecksumsAgree(asset, sums); !agree) return agree;
    const QString actual = actualSha256.toLower();
    if (actual != asset.sha256)
        return makeError(ErrorCode::CorruptData, "The SHA-256 of the downloaded " + s(asset.name) + " is " + s(actual) +
                                                     " but " + s(asset.sha256) + " was expected.");
    return {};
}

core::Result<QString> sha256OfFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return makeError(ErrorCode::IoError, "Cannot read " + s(path) + ": " + s(f.errorString()) + ".");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) return makeError(ErrorCode::IoError, "Reading " + s(path) + " failed: " + s(f.errorString()) + ".");
    return QString::fromLatin1(hash.result().toHex());
}

core::Result<void> verifyDownloadedFile(const QString& path, const UpdateAsset& asset,
                                        const std::map<QString, QString>& sums) {
    const auto hash = sha256OfFile(path);
    if (!hash) return hash.error();
    return verifyDownload(QFile(path).size(), hash.value(), asset, sums);
}

} // namespace fos::app
