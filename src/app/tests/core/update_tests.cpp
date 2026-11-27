// SPDX-License-Identifier: AGPL-3.0-only
// In-app updater logic: semantic versions, GitHub release JSON, update.json, SHA256SUMS.txt,
// download verification, ZIP/DEFLATE extraction, install-kind detection, swap plan, the
// generated Windows scripts and the startup consent. No network: sample JSON is inline.
#include "fos/app/update_plan.hpp"
#include "fos/app/update_release.hpp"
#include "fos/app/update_version.hpp"
#include "fos/app/update_zip.hpp"

#include "test_support.hpp"
#include "update_test_data.hpp"

#include <QDir>
#include <QFile>

#include <gtest/gtest.h>

namespace fos::app {
namespace {

using test::TempDir;

SemVer v(const char* text) {
    auto r = parseSemVer(text);
    EXPECT_TRUE(r.has_value()) << text;
    return r.value_or(SemVer{});
}

QByteArray bytes(const unsigned char* data, std::size_t n) {
    return QByteArray(reinterpret_cast<const char*>(data), static_cast<qsizetype>(n));
}

QByteArray sampleText() {
    QByteArray s;
    for (int i = 0; i < 300; ++i) s += "line " + QByteArray::number(i) + " of the FurnitureOS sample\n";
    return s;
}

QString qs(const std::filesystem::path& p) { return QString::fromStdString(p.string()); }

bool writeFile(const QString& path, const QByteArray& data) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

QByteArray readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// SHA-256("abc") from FIPS 180-2, appendix B.1.
const QString kAbcSha = QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
const QString kZipSha = QString(64, QLatin1Char('a'));
const QString kSetupSha = QString(64, QLatin1Char('b'));

// ----------------------------------------------------------------- versions
TEST(UpdateVersion, ParsesTagsWithPrefixPrereleaseAndBuild) {
    const SemVer a = v("v1.2.3-beta.1+build.7");
    EXPECT_EQ(a.major, 1u);
    EXPECT_EQ(a.minor, 2u);
    EXPECT_EQ(a.patch, 3u);
    ASSERT_EQ(a.prerelease.size(), 2u);
    EXPECT_EQ(a.prerelease[0], "beta");
    EXPECT_EQ(a.prerelease[1], "1");
    EXPECT_EQ(a.toString(), "1.2.3-beta.1"); // build metadata dropped
    EXPECT_EQ(v("0.10.0").toString(), "0.10.0");
}

TEST(UpdateVersion, RejectsMalformedVersions) {
    for (const char* bad : {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.02.3", "1.2.3-", "1.2.3-beta..1", "1.2.3-beta.01",
                            "1.2.3-be$ta", "1.2.3+", "a.b.c", "v", "1.2.-3", "latest"})
        EXPECT_FALSE(parseSemVer(bad).has_value()) << bad;
}

TEST(UpdateVersion, OrdersPerSemverRule11) {
    // The chain from semver.org section 11, each strictly smaller than the next.
    const char* chain[] = {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2",
                           "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "1.0.1", "1.1.0", "1.10.0", "2.0.0"};
    for (std::size_t i = 0; i + 1 < std::size(chain); ++i) {
        EXPECT_LT(compareSemVer(v(chain[i]), v(chain[i + 1])), 0) << chain[i] << " < " << chain[i + 1];
        EXPECT_GT(compareSemVer(v(chain[i + 1]), v(chain[i])), 0);
    }
    EXPECT_EQ(compareSemVer(v("v1.0.0+a"), v("1.0.0+b")), 0); // build metadata has no precedence
    EXPECT_TRUE(v("0.2.0-beta.1") < v("0.2.0"));               // final beats its pre-release
    EXPECT_TRUE(v("0.1.9") < v("0.2.0-beta.1"));               // ...but not an older final
}

// ----------------------------------------------------------------- GitHub JSON
const char* kLatestJson = R"({
  "tag_name": "v0.2.0", "name": "FurnitureOS 0.2.0", "draft": false, "prerelease": false,
  "html_url": "https://github.com/shinigamich/furnitureos/releases/tag/v0.2.0",
  "published_at": "2026-11-20T10:00:00Z",
  "assets": [
    {"name": "FurnitureOS-0.2.0-windows-x64.zip", "size": 1000,
     "browser_download_url": "https://github.com/shinigamich/furnitureos/releases/download/v0.2.0/FurnitureOS-0.2.0-windows-x64.zip"},
    {"name": "FurnitureOS-0.2.0-windows-x64-setup.exe", "size": 2000,
     "browser_download_url": "https://github.com/shinigamich/furnitureos/releases/download/v0.2.0/FurnitureOS-0.2.0-windows-x64-setup.exe"},
    {"name": "SHA256SUMS.txt", "size": 200,
     "browser_download_url": "https://github.com/shinigamich/furnitureos/releases/download/v0.2.0/SHA256SUMS.txt"},
    {"name": "update.json", "size": 300,
     "browser_download_url": "https://github.com/shinigamich/furnitureos/releases/download/v0.2.0/update.json"},
    {"name": "insecure.txt", "size": 1, "browser_download_url": "http://example.com/insecure.txt"}
  ]})";

QByteArray releaseJson(const char* tag, bool prerelease, bool draft, bool withMetadata) {
    QByteArray assets = withMetadata ? QByteArray(R"([{"name":"update.json","size":10,"browser_download_url":"https://x.test/u"},)"
                                                  R"({"name":"SHA256SUMS.txt","size":10,"browser_download_url":"https://x.test/s"}])")
                                     : QByteArray("[]");
    return QByteArray("{\"tag_name\":\"") + tag + "\",\"prerelease\":" + (prerelease ? "true" : "false") +
           ",\"draft\":" + (draft ? "true" : "false") + ",\"assets\":" + assets + "}";
}

TEST(UpdateRelease, ParsesLatestReleaseObject) {
    const auto r = parseGitHubReleases(kLatestJson);
    ASSERT_TRUE(r) << r.error().message;
    ASSERT_EQ(r.value().size(), 1u);
    const ReleaseInfo& rel = r.value()[0];
    EXPECT_EQ(rel.tag, QStringLiteral("v0.2.0"));
    EXPECT_EQ(rel.version.toString(), "0.2.0");
    EXPECT_FALSE(rel.prerelease);
    EXPECT_EQ(rel.assets.size(), 4u); // the http:// asset is dropped
    ASSERT_NE(rel.findAsset(QStringLiteral("update.json")), nullptr);
    EXPECT_EQ(rel.findAsset(QStringLiteral("FurnitureOS-0.2.0-windows-x64.zip"))->size, 1000);
    EXPECT_EQ(rel.findAsset(QStringLiteral("insecure.txt")), nullptr);
}

TEST(UpdateRelease, ParsesArrayAndSkipsNonSemverTags) {
    const QByteArray list = "[" + releaseJson("v0.3.0-beta.2", true, false, true) + "," +
                            releaseJson("nightly", true, false, true) + "," + releaseJson("v0.2.0", false, false, true) + "]";
    const auto r = parseGitHubReleases(list);
    ASSERT_TRUE(r);
    ASSERT_EQ(r.value().size(), 2u);
    EXPECT_EQ(r.value()[0].tag, QStringLiteral("v0.3.0-beta.2"));
    EXPECT_TRUE(r.value()[0].prerelease);
}

TEST(UpdateRelease, ReportsBadJsonAndGitHubErrors) {
    EXPECT_FALSE(parseGitHubReleases("{not json"));
    const auto limited = parseGitHubReleases(R"({"message":"API rate limit exceeded for 1.2.3.4."})");
    ASSERT_FALSE(limited);
    EXPECT_NE(limited.error().message.find("rate limit"), std::string::npos);
    const auto notFound = parseGitHubReleases(R"({"message":"Not Found"})");
    ASSERT_FALSE(notFound);
    EXPECT_NE(notFound.error().message.find("Not Found"), std::string::npos);
    EXPECT_FALSE(parseGitHubReleases("42"));
    const auto empty = parseGitHubReleases("[]");
    ASSERT_TRUE(empty);
    EXPECT_TRUE(empty.value().empty());
}

TEST(UpdateRelease, SelectsNewestEligibleRelease) {
    const QByteArray list = "[" + releaseJson("v0.4.0", false, true, true) + "," +      // draft: never
                            releaseJson("v0.3.0-beta.2", true, false, true) + "," +     // pre-release
                            releaseJson("v0.2.5", false, false, false) + "," +          // no update.json
                            releaseJson("v0.2.1", false, false, true) + "," +
                            releaseJson("v0.2.0", false, false, true) + "]";
    const auto all = parseGitHubReleases(list).value();
    const auto stable = selectUpdate(all, v("0.2.0"), false);
    ASSERT_TRUE(stable.has_value());
    EXPECT_EQ(stable->tag, QStringLiteral("v0.2.1"));
    const auto beta = selectUpdate(all, v("0.2.0"), true);
    ASSERT_TRUE(beta.has_value());
    EXPECT_EQ(beta->tag, QStringLiteral("v0.3.0-beta.2"));
    EXPECT_FALSE(selectUpdate(all, v("0.2.1"), false).has_value());        // up to date
    EXPECT_FALSE(selectUpdate(all, v("0.3.0-beta.2"), true).has_value());  // same version is not an update
    EXPECT_FALSE(selectUpdate(all, v("1.0.0"), true).has_value());         // newer than everything published
    // Running a beta: the final release of the same version is an update on the stable channel.
    const auto fin = parseGitHubReleases("[" + releaseJson("v0.3.0", false, false, true) + "]").value();
    ASSERT_TRUE(selectUpdate(fin, v("0.3.0-beta.2"), false).has_value());
}

TEST(UpdateRelease, ApiUrlDependsOnChannel) {
    EXPECT_EQ(releasesApiUrl(false).toString(), QStringLiteral("https://api.github.com/repos/shinigamich/furnitureos/releases/latest"));
    EXPECT_EQ(releasesApiUrl(true).toString(),
              QStringLiteral("https://api.github.com/repos/shinigamich/furnitureos/releases?per_page=30"));
}

// ----------------------------------------------------------------- update.json / SHA256SUMS
QByteArray manifestJson(const char* version, qint64 zipSize = 1000, const QString& zipSha = kZipSha) {
    return QStringLiteral(R"({"version":"%1","published":"2026-11-20","notes":"## Changes\n- Faster nesting",
        "assets":{"windows-x64-zip":{"name":"FurnitureOS-0.2.0-windows-x64.zip","sha256":"%2","size":%3},
                  "windows-x64-setup":{"name":"FurnitureOS-0.2.0-windows-x64-setup.exe","sha256":"%4","size":2000}}})")
        .arg(QLatin1String(version), zipSha.toUpper(), QString::number(zipSize), kSetupSha)
        .toUtf8();
}

ReleaseInfo latestRelease() { return parseGitHubReleases(kLatestJson).value()[0]; }

TEST(UpdateManifest, ParsesAndCrossChecksWithRelease) {
    const auto m = parseUpdateManifest(manifestJson("0.2.0"), latestRelease());
    ASSERT_TRUE(m) << m.error().message;
    EXPECT_EQ(m.value().version.toString(), "0.2.0");
    EXPECT_EQ(m.value().published, QStringLiteral("2026-11-20"));
    EXPECT_TRUE(m.value().notes.startsWith(QStringLiteral("## Changes")));
    const UpdateAsset* zip = m.value().asset(kAssetWindowsZip);
    ASSERT_NE(zip, nullptr);
    EXPECT_EQ(zip->sha256, kZipSha); // upper case in the file, normalised to lower case
    EXPECT_EQ(zip->size, 1000);
    EXPECT_TRUE(zip->downloadUrl.toString().endsWith(QStringLiteral("/v0.2.0/FurnitureOS-0.2.0-windows-x64.zip")));
    ASSERT_NE(m.value().asset(kAssetWindowsSetup), nullptr);
}

TEST(UpdateManifest, RefusesInconsistentManifests) {
    const ReleaseInfo rel = latestRelease();
    EXPECT_FALSE(parseUpdateManifest(manifestJson("0.2.1"), rel));           // version != tag
    EXPECT_FALSE(parseUpdateManifest(manifestJson("0.2.0", 999), rel));      // size != GitHub asset size
    EXPECT_FALSE(parseUpdateManifest(manifestJson("0.2.0", 0), rel));        // no positive size
    EXPECT_FALSE(parseUpdateManifest(manifestJson("0.2.0", 1000, QStringLiteral("abc")), rel)); // short hash
    EXPECT_FALSE(parseUpdateManifest("[]", rel));
    const QByteArray missing = R"({"version":"0.2.0","assets":{"windows-x64-zip":{"name":"other.zip","sha256":")" +
                               kZipSha.toUtf8() + R"(","size":5}}})";
    const auto r = parseUpdateManifest(missing, rel);
    ASSERT_FALSE(r);
    EXPECT_NE(r.error().message.find("other.zip"), std::string::npos);
}

TEST(UpdateChecksums, ParsesSha256SumFormat) {
    const QByteArray text = kZipSha.toUtf8() + "  FurnitureOS-0.2.0-windows-x64.zip\n" + kSetupSha.toUpper().toUtf8() +
                            " *FurnitureOS-0.2.0-windows-x64-setup.exe\r\n\n# comment\n";
    const auto sums = parseSha256Sums(text);
    ASSERT_TRUE(sums) << sums.error().message;
    ASSERT_EQ(sums.value().size(), 2u);
    EXPECT_EQ(sums.value().at(QStringLiteral("FurnitureOS-0.2.0-windows-x64.zip")), kZipSha);
    EXPECT_EQ(sums.value().at(QStringLiteral("FurnitureOS-0.2.0-windows-x64-setup.exe")), kSetupSha);
    EXPECT_FALSE(parseSha256Sums("abc  file.zip\n"));
    EXPECT_FALSE(parseSha256Sums(kZipSha.toUtf8() + "\n"));
    EXPECT_FALSE(parseSha256Sums(kZipSha.toUtf8() + "  a.zip\n" + kSetupSha.toUtf8() + "  a.zip\n")); // duplicate
}

TEST(UpdateChecksums, VerifiesSizeAndBothHashes) {
    UpdateAsset a{QStringLiteral("f.zip"), kAbcSha, 3, {}};
    const std::map<QString, QString> sums{{QStringLiteral("f.zip"), kAbcSha}};
    EXPECT_TRUE(verifyDownload(3, kAbcSha.toUpper(), a, sums));
    EXPECT_FALSE(verifyDownload(4, kAbcSha, a, sums));                    // size
    EXPECT_FALSE(verifyDownload(3, kZipSha, a, sums));                    // hash
    EXPECT_FALSE(verifyDownload(3, kAbcSha, a, {}));                      // not in SHA256SUMS
    EXPECT_FALSE(verifyDownload(3, kAbcSha, a, {{QStringLiteral("f.zip"), kZipSha}})); // sources disagree
    const auto disagree = checkChecksumsAgree(a, {{QStringLiteral("f.zip"), kZipSha}});
    ASSERT_FALSE(disagree);
    EXPECT_NE(disagree.error().message.find("disagree"), std::string::npos);

    TempDir dir;
    const QString file = qs(dir.path / "f.zip");
    ASSERT_TRUE(writeFile(file, "abc"));
    EXPECT_EQ(sha256OfFile(file).value(), kAbcSha);
    EXPECT_TRUE(verifyDownloadedFile(file, a, sums));
    ASSERT_TRUE(writeFile(file, "abd"));
    EXPECT_FALSE(verifyDownloadedFile(file, a, sums));
    EXPECT_FALSE(sha256OfFile(qs(dir.path / "missing.zip")));
}

// ----------------------------------------------------------------- DEFLATE / ZIP
TEST(UpdateZip, Crc32KnownValues) {
    EXPECT_EQ(crc32(QByteArray("123456789")), 0xCBF43926u); // standard CRC-32 check value
    EXPECT_EQ(crc32(QByteArray()), 0u);
}

TEST(UpdateZip, InflatesDynamicFixedAndStoredBlocks) {
    const QByteArray sample = sampleText();
    const auto dyn = inflateRaw(bytes(test::kDeflateDynamic, sizeof test::kDeflateDynamic), sample.size());
    ASSERT_TRUE(dyn) << dyn.error().message;
    EXPECT_EQ(dyn.value(), sample);
    const auto fix = inflateRaw(bytes(test::kDeflateFixed, sizeof test::kDeflateFixed), sample.size());
    ASSERT_TRUE(fix) << fix.error().message;
    EXPECT_EQ(fix.value(), sample);
    const auto sto = inflateRaw(bytes(test::kDeflateStored, sizeof test::kDeflateStored), 1000);
    ASSERT_TRUE(sto) << sto.error().message;
    EXPECT_EQ(sto.value(), sample.left(1000));
}

TEST(UpdateZip, InflateRejectsDamagedOrOversizedData) {
    const QByteArray good = bytes(test::kDeflateDynamic, sizeof test::kDeflateDynamic);
    const qint64 n = sampleText().size();
    EXPECT_FALSE(inflateRaw(good.left(good.size() / 2), n));  // truncated
    EXPECT_FALSE(inflateRaw(good, n - 1));                    // larger than declared: stops, no growth
    EXPECT_FALSE(inflateRaw(good, n + 1));                    // smaller than declared
    EXPECT_FALSE(inflateRaw(QByteArray("\x07", 1), 10));      // block type 3
    EXPECT_FALSE(inflateRaw(QByteArray(), 0));                // no data at all
}

TEST(UpdateZip, ExtractsRealArchive) {
    TempDir dir;
    const QString zip = qs(dir.path / "good.zip");
    ASSERT_TRUE(writeFile(zip, bytes(test::kZipGood, sizeof test::kZipGood)));
    const QString out = qs(dir.path / "out");
    ASSERT_TRUE(QDir().mkpath(out));
    const auto stats = extractZip(zip, out);
    ASSERT_TRUE(stats) << stats.error().message;
    EXPECT_EQ(stats.value().files, 3);
    EXPECT_EQ(stats.value().directories, 1);
    const QString root = out + QStringLiteral("/FurnitureOS-0.2.0-windows-x64/");
    EXPECT_EQ(readFile(root + QStringLiteral("furnitureos.exe")), sampleText());
    EXPECT_EQ(readFile(root + QStringLiteral("readme.txt")), QByteArray("FurnitureOS portable\n"));
    QByteArray dll;
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < 256; ++i) dll.append(static_cast<char>(i));
    EXPECT_EQ(readFile(root + QStringLiteral("plugins/a.dll")), dll);
    // 9000-ish sample + 21 + 1024 bytes.
    EXPECT_EQ(stats.value().bytes, sampleText().size() + 21 + 1024);
}

TEST(UpdateZip, RefusesPathTraversalAndDamage) {
    TempDir dir;
    const QString out = qs(dir.path / "out");
    ASSERT_TRUE(QDir().mkpath(out));
    const QString evil = qs(dir.path / "evil.zip");
    ASSERT_TRUE(writeFile(evil, bytes(test::kZipEvil, sizeof test::kZipEvil)));
    const auto r = extractZip(evil, out);
    ASSERT_FALSE(r);
    EXPECT_NE(r.error().message.find("outside"), std::string::npos);
    EXPECT_FALSE(QFile::exists(qs(dir.path / "evil.txt")));

    QByteArray damaged = bytes(test::kZipGood, sizeof test::kZipGood);
    damaged[200] = static_cast<char>(damaged[200] ^ 0x55); // inside the deflated exe data
    const QString bad = qs(dir.path / "bad.zip");
    ASSERT_TRUE(writeFile(bad, damaged));
    const QString out2 = qs(dir.path / "out2");
    ASSERT_TRUE(QDir().mkpath(out2));
    EXPECT_FALSE(extractZip(bad, out2));

    ASSERT_TRUE(writeFile(bad, "PK not really a zip file at all"));
    EXPECT_FALSE(extractZip(bad, out2));
    EXPECT_FALSE(extractZip(qs(dir.path / "missing.zip"), out2));
}

TEST(UpdateZip, AcceptsBackslashNamesAndChecksSafety) {
    TempDir dir;
    const QString zip = qs(dir.path / "bs.zip");
    ASSERT_TRUE(writeFile(zip, bytes(test::kZipBackslash, sizeof test::kZipBackslash)));
    const QString out = qs(dir.path / "out");
    ASSERT_TRUE(QDir().mkpath(out));
    ASSERT_TRUE(extractZip(zip, out));
    EXPECT_EQ(readFile(out + QStringLiteral("/FurnitureOS/furnitureos.exe")), QByteArray("exe\n"));

    EXPECT_EQ(safeZipEntryPath(QStringLiteral("a/./b\\c.txt")), QStringLiteral("a/b/c.txt"));
    EXPECT_TRUE(safeZipEntryPath(QStringLiteral("/etc/passwd")).isEmpty());
    EXPECT_TRUE(safeZipEntryPath(QStringLiteral("C:/Windows/x.dll")).isEmpty());
    EXPECT_TRUE(safeZipEntryPath(QStringLiteral("a/../../b")).isEmpty());
}

// ----------------------------------------------------------------- plan and scripts
TEST(UpdatePlan, DetectsInstallKindAndChoosesAsset) {
    TempDir dir;
    const QString app = qs(dir.path);
    EXPECT_EQ(detectInstallKind(app, false), InstallKind::Unsupported);
    EXPECT_EQ(detectInstallKind(app, true), InstallKind::WindowsPortable);
    ASSERT_TRUE(writeFile(app + QStringLiteral("/installed.txt"), "Inno Setup\n"));
    EXPECT_EQ(detectInstallKind(app, true), InstallKind::WindowsInstalled);
    EXPECT_EQ(detectInstallKind(app, false), InstallKind::Unsupported);

    UpdateManifest m = parseUpdateManifest(manifestJson("0.2.0"), latestRelease()).value();
    auto c = chooseUpdate(InstallKind::WindowsInstalled, m);
    EXPECT_EQ(c.method, UpdateMethod::RunInstaller);
    EXPECT_EQ(c.asset->name, QStringLiteral("FurnitureOS-0.2.0-windows-x64-setup.exe"));
    c = chooseUpdate(InstallKind::WindowsPortable, m);
    EXPECT_EQ(c.method, UpdateMethod::SwapPortable);
    EXPECT_EQ(c.asset->name, QStringLiteral("FurnitureOS-0.2.0-windows-x64.zip"));
    EXPECT_EQ(chooseUpdate(InstallKind::Unsupported, m).method, UpdateMethod::ManualOnly);
    m.assets.erase(QString::fromLatin1(kAssetWindowsSetup)); // installed copy, no installer published
    c = chooseUpdate(InstallKind::WindowsInstalled, m);
    EXPECT_EQ(c.method, UpdateMethod::ManualOnly);
    EXPECT_EQ(c.asset, nullptr);
}

TEST(UpdatePlan, PlansUniqueSiblingFoldersAndFindsStagedRoot) {
    TempDir dir;
    const QString app = qs(dir.path / "FurnitureOS");
    ASSERT_TRUE(QDir().mkpath(app));
    ASSERT_TRUE(QDir().mkpath(qs(dir.path / "FurnitureOS.old"))); // left over: must not be reused
    const auto plan = planPortableSwap(app, app + QStringLiteral("/furnitureos.exe"), v("0.2.0-beta.1"));
    ASSERT_TRUE(plan) << plan.error().message;
    EXPECT_EQ(plan.value().exeRelative, QStringLiteral("furnitureos.exe"));
    EXPECT_EQ(plan.value().stagingDir, qs(dir.path / "FurnitureOS.update-0.2.0-beta.1"));
    EXPECT_EQ(plan.value().backupDir, qs(dir.path / "FurnitureOS.old-2"));
    EXPECT_FALSE(planPortableSwap(app, qs(dir.path / "elsewhere.exe"), v("0.2.0")));
    EXPECT_FALSE(planPortableSwap(QStringLiteral("relative/dir"), QStringLiteral("x.exe"), v("0.2.0")));

    // Extract the sample package into staging; the exe lies in its single top-level folder.
    const QString staging = plan.value().stagingDir;
    ASSERT_TRUE(QDir().mkpath(staging));
    const QString zip = qs(dir.path / "good.zip");
    ASSERT_TRUE(writeFile(zip, bytes(test::kZipGood, sizeof test::kZipGood)));
    ASSERT_TRUE(extractZip(zip, staging));
    const auto root = locateStagedRoot(staging, QStringLiteral("furnitureos.exe"));
    ASSERT_TRUE(root) << root.error().message;
    EXPECT_EQ(root.value(), staging + QStringLiteral("/FurnitureOS-0.2.0-windows-x64"));
    EXPECT_FALSE(locateStagedRoot(staging, QStringLiteral("other.exe")));
    // Flat archive: the exe at the top of staging.
    ASSERT_TRUE(writeFile(staging + QStringLiteral("/furnitureos.exe"), "x"));
    EXPECT_EQ(locateStagedRoot(staging, QStringLiteral("furnitureos.exe")).value(), QDir(staging).absolutePath());
}

TEST(UpdatePlan, PortableScriptSwapsWithRollback) {
    PortableSwapPlan plan{QStringLiteral("C:/Tools/FurnitureOS"), QStringLiteral("C:/Tools/FurnitureOS.update-0.2.0"),
                          QStringLiteral("C:/Tools/FurnitureOS.old"), QStringLiteral("furnitureos.exe")};
    const QString script = portableSwapScript(plan, QStringLiteral("C:/Tools/FurnitureOS.update-0.2.0/FurnitureOS-0.2.0-windows-x64"),
                                              4242, QStringLiteral("0.2.0"), QStringLiteral("C:/Cache/update-result.txt"));
    EXPECT_TRUE(script.contains(QStringLiteral("set \"FOS_PID=4242\"\r\n")));
    EXPECT_TRUE(script.contains(QStringLiteral("set \"FOS_APP=") + QDir::toNativeSeparators(QStringLiteral("C:/Tools/FurnitureOS")) + QLatin1Char('"')));
    // Order: wait for exit, move old away, move new in, roll back on failure, relaunch.
    const auto at = [&](const char* text) { return script.indexOf(QLatin1String(text)); };
    ASSERT_GE(at(":wait_exit"), 0);
    EXPECT_LT(at(":wait_exit"), at("move \"%FOS_APP%\" \"%FOS_BACKUP%\""));
    EXPECT_LT(at("move \"%FOS_APP%\" \"%FOS_BACKUP%\""), at("move \"%FOS_STAGED%\" \"%FOS_APP%\""));
    EXPECT_LT(at("move \"%FOS_STAGED%\" \"%FOS_APP%\""), at("move \"%FOS_BACKUP%\" \"%FOS_APP%\""));
    EXPECT_LT(at(":swapped"), at("rd /s /q \"%FOS_BACKUP%\"")); // old folder deleted only after success
    EXPECT_TRUE(script.contains(QStringLiteral("echo ok %FOS_VERSION%")));
    EXPECT_TRUE(script.contains(QStringLiteral(":fail_rollback")));
    EXPECT_FALSE(script.contains(QStringLiteral("timeout "))); // fails without a console
    EXPECT_TRUE(script.endsWith(QStringLiteral("\r\n")));
    EXPECT_EQ(script.count(QStringLiteral("\n")), script.count(QStringLiteral("\r\n"))); // CRLF only
    // Staging cleanup on success only when the new folder was nested inside it.
    const int successCleanups = script.mid(at(":swapped"), at(":fail_precheck") - at(":swapped"))
                                    .count(QStringLiteral("rd /s /q \"%FOS_STAGING%\""));
    EXPECT_EQ(successCleanups, 1);
    const QString flat = portableSwapScript(plan, plan.stagingDir, 1, QStringLiteral("0.2.0"), QStringLiteral("r.txt"));
    const int s0 = flat.indexOf(QLatin1String(":swapped")), s1 = flat.indexOf(QLatin1String(":fail_precheck"));
    EXPECT_EQ(flat.mid(s0, s1 - s0).count(QStringLiteral("rd /s /q \"%FOS_STAGING%\"")), 0);
}

TEST(UpdatePlan, InstallerScriptAndEscaping) {
    const QString script = installerScript(QStringLiteral("C:/Cache/FurnitureOS-0.2.0-windows-x64-setup.exe"),
                                           QStringLiteral("C:/Users/A 100%/Programs/FurnitureOS/furnitureos.exe"), 7,
                                           QStringLiteral("0.2.0"), QStringLiteral("C:/Cache/r.txt"));
    EXPECT_TRUE(script.contains(QStringLiteral("\"%FOS_SETUP%\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART")));
    EXPECT_TRUE(script.contains(QStringLiteral("100%%"))); // '%' in a path doubled
    EXPECT_LT(script.indexOf(QLatin1String(":exited")), script.indexOf(QLatin1String("\"%FOS_SETUP%\" /VERYSILENT")));
    EXPECT_TRUE(script.contains(QStringLiteral("start \"\" /D \"%FOS_EXE_DIR%\" \"%FOS_EXE_PATH%\"")));
    EXPECT_EQ(cmdEscape(QStringLiteral("a%b%")), QStringLiteral("a%%b%%"));
}

TEST(UpdatePlan, OutcomeAndConsent) {
    auto ok = parseUpdateOutcome(QStringLiteral("ok 0.2.0\r\n"));
    ASSERT_TRUE(ok.has_value());
    EXPECT_TRUE(ok->ok);
    EXPECT_EQ(ok->detail, QStringLiteral("0.2.0"));
    auto bad = parseUpdateOutcome(QStringLiteral("failed The folder stayed in use.\r\n"));
    ASSERT_TRUE(bad.has_value());
    EXPECT_FALSE(bad->ok);
    EXPECT_EQ(bad->detail, QStringLiteral("The folder stayed in use."));
    EXPECT_FALSE(parseUpdateOutcome(QString()).has_value());
    EXPECT_FALSE(parseUpdateOutcome(QStringLiteral("garbage")).has_value());

    for (UpdateConsent c : {UpdateConsent::NotAsked, UpdateConsent::Allowed, UpdateConsent::Declined})
        EXPECT_EQ(updateConsentFromKey(updateConsentKey(c)), c);
    EXPECT_EQ(updateConsentFromKey(QStringLiteral("maybe")), UpdateConsent::NotAsked);
    EXPECT_TRUE(shouldAskUpdateConsent(UpdateConsent::NotAsked));
    EXPECT_FALSE(shouldCheckAtStartup(UpdateConsent::NotAsked)); // never before the user answered
    EXPECT_FALSE(shouldCheckAtStartup(UpdateConsent::Declined));
    EXPECT_FALSE(shouldAskUpdateConsent(UpdateConsent::Declined));
    EXPECT_TRUE(shouldCheckAtStartup(UpdateConsent::Allowed));
}

} // namespace
} // namespace fos::app
