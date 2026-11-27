// SPDX-License-Identifier: AGPL-3.0-only
// Restart / start-up command line (relaunch.hpp) and the bundled sample projects.
#include "fos/app/relaunch.hpp"
#include "fos/app/samples.hpp"
#include "test_support.hpp"

#include <QFile>

namespace fos::app::test {
namespace {

namespace fs = std::filesystem;

QStringList L(std::initializer_list<const char*> items) {
    QStringList out;
    for (const char* s : items) out << QString::fromUtf8(s);
    return out;
}

TEST(Relaunch, OptionsWithSeparateValues) {
    EXPECT_TRUE(optionTakesValue(QStringLiteral("-platform")));
    EXPECT_TRUE(optionTakesValue(QStringLiteral("--style")));
    EXPECT_FALSE(optionTakesValue(QStringLiteral("-style=fusion"))); // value in the same argument
    EXPECT_FALSE(optionTakesValue(QStringLiteral("-reverse")));      // flag
    EXPECT_FALSE(optionTakesValue(QStringLiteral("platform")));      // not an option
}

TEST(Relaunch, ProjectFileArgumentSkipsOptionsAndTheirValues) {
    EXPECT_EQ(projectFileArgument(L({"furnitureos"})), QString());
    EXPECT_EQ(projectFileArgument(L({"furnitureos", "a.fosp", "b.fosp"})), QStringLiteral("a.fosp"));
    // "offscreen" is the value of -platform, not a file.
    EXPECT_EQ(projectFileArgument(L({"furnitureos", "-platform", "offscreen", "a.fosp"})), QStringLiteral("a.fosp"));
    EXPECT_EQ(projectFileArgument(L({"furnitureos", "-reverse", "-style=fusion"})), QString());
    // After "--" an argument starting with '-' is a file.
    EXPECT_EQ(projectFileArgument(L({"furnitureos", "--", "-odd.fosp"})), QStringLiteral("-odd.fosp"));
    // A trailing option that expects a value but has none.
    EXPECT_EQ(projectFileArgument(L({"furnitureos", "-style"})), QString());
}

TEST(Relaunch, ArgumentsKeepOptionsAndReplaceTheDocument) {
    // argv[0] dropped, options kept in order, the old file replaced by the current document.
    EXPECT_EQ(relaunchArguments(L({"/opt/fos/furnitureos", "-style", "fusion", "old.fosp", "-reverse"}),
                                QStringLiteral("/home/u/new.fosp")),
              L({"-style", "fusion", "-reverse", "/home/u/new.fosp"}));
    // Untitled document (nothing to reopen): only the options.
    EXPECT_EQ(relaunchArguments(L({"furnitureos", "old.fosp"}), QString()), QStringList());
    EXPECT_EQ(relaunchArguments(L({"furnitureos"}), QString()), QStringList());
    // Windows paths with spaces stay one argument (QProcess quotes them).
    EXPECT_EQ(relaunchArguments(L({"C:\\FurnitureOS\\furnitureos.exe"}), QStringLiteral("C:/My Projects/k.fosp")),
              L({"C:/My Projects/k.fosp"}));
    // "--" of the old command line dropped; written again for a document named like an option.
    EXPECT_EQ(relaunchArguments(L({"furnitureos", "--", "-a.fosp"}), QStringLiteral("-b.fosp")), L({"--", "-b.fosp"}));
}

TEST(Relaunch, ProgramPrefersAnExistingAppImage) {
    TempDir dir;
    const QString exe = QStringLiteral("/tmp/.mount_abc/usr/bin/furnitureos");
    EXPECT_EQ(relaunchProgram(exe, QString()), exe);
    const QString missing = QString::fromStdString((dir.path / "missing.AppImage").string());
    EXPECT_EQ(relaunchProgram(exe, missing), exe);
    const QString image = QString::fromStdString((dir.path / "FurnitureOS.AppImage").string());
    QFile f(image);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.close();
    EXPECT_EQ(relaunchProgram(exe, image), image);
}

TEST(Samples, EveryBundledSampleOpensUntitledAndValidatesWithoutErrors) {
    const auto samples = sampleProjects();
    ASSERT_EQ(samples.size(), 3u);
    // Item counts from the generation commands (README "Sample projects"): kitchen 4 cabinets
    // + worktop, wardrobe 1, desk 1.
    const std::size_t expectedItems[] = {5, 1, 1};
    for (std::size_t i = 0; i < samples.size(); ++i) {
        SCOPED_TRACE(samples[i].key.toStdString());
        const QByteArray text = sampleProjectText(samples[i]);
        ASSERT_FALSE(text.isEmpty());
        Session s;
        auto r = s.openUntitledCopy(std::string_view(text.constData(), static_cast<std::size_t>(text.size())),
                                    samples[i].resourcePath.toStdString());
        ASSERT_TRUE(r) << r.error().message;
        EXPECT_FALSE(core::hasErrors(r.value()));
        EXPECT_TRUE(s.filePath().empty()); // untitled: Save asks for a location
        EXPECT_FALSE(s.isDirty());
        EXPECT_EQ(s.project().items.size(), expectedItems[i]);
        EXPECT_TRUE(s.validationReport().ok());
    }
}

TEST(Samples, UntitledCopyGetsAFreshIdAndKeepsTheName) {
    const auto samples = sampleProjects();
    ASSERT_FALSE(samples.empty());
    const QByteArray text = sampleProjectText(samples[0]);
    Session s;
    ASSERT_TRUE(s.openUntitledCopy(std::string_view(text.constData(), static_cast<std::size_t>(text.size())), "k"));
    EXPECT_EQ(s.project().metadata.name, "Kitchen run");
    EXPECT_NE(s.project().metadata.id, "11111111-1111-4111-8111-111111111111"); // id stored in the sample file
    // Bad text: error, document unchanged.
    const std::string before = s.project().metadata.id;
    EXPECT_FALSE(s.openUntitledCopy("{ not json", "broken"));
    EXPECT_EQ(s.project().metadata.id, before);
}

} // namespace
} // namespace fos::app::test
