// SPDX-License-Identifier: AGPL-3.0-only
#include "test_support.hpp"

#include <QProcess>
#include <QStandardPaths>

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>

namespace fos::printing::test {

drawings::Sheet standardSheet(drawings::PaperFormat format, drawings::PaperOrientation orientation,
                              drawings::CalibrationKind calibration) {
    drawings::SheetOptions o;
    o.format = format;
    o.orientation = orientation;
    o.calibration.kind = calibration;
    o.title.project = "Kitchen";
    o.title.title = "Base cabinet";
    auto sheet = drawings::makeSheet(o, drawings::Scale{1, 1}, std::string("sheet-") + drawings::toString(format));
    EXPECT_TRUE(sheet.ok());
    return sheet.ok() ? sheet.value() : drawings::Sheet{};
}

drawings::Sheet blankSheet(double width, double height, std::string id) {
    drawings::Sheet s;
    s.id = std::move(id);
    s.width = width;
    s.height = height;
    return s;
}

drawings::Line calibrationLine(const drawings::Sheet& sheet) {
    for (const drawings::Primitive& p : sheet.primitives)
        if (const auto* l = std::get_if<drawings::Line>(&p); l && l->elementId == "sheet/calibration.line") {
            EXPECT_NEAR(std::hypot(l->b.x - l->a.x, l->b.y - l->a.y), 100.0, 1e-9);
            return *l;
        }
    ADD_FAILURE() << "sheet has no calibration line";
    return {};
}

bool isInk(const QImage& image, int x, int y) { return qGray(image.pixel(x, y)) < 128; }

std::vector<Run> runs(const QImage& image, int y, int x0, int x1) {
    std::vector<Run> out;
    if (x1 < 0 || x1 > image.width()) x1 = image.width();
    if (y < 0 || y >= image.height()) return out;
    int start = -1;
    for (int x = std::max(0, x0); x <= x1; ++x) {
        const bool ink = x < x1 && isInk(image, x, y);
        if (ink && start < 0) start = x;
        if (!ink && start >= 0) {
            out.push_back({start, x - start});
            start = -1;
        }
    }
    return out;
}

Run longestRun(const QImage& image, int y, int x0, int x1) {
    Run best;
    for (const Run& r : runs(image, y, x0, x1))
        if (r.length > best.length) best = r;
    return best;
}

InkBox inkBox(const QImage& image, int x0, int y0, int x1, int y1) {
    if (x1 < 0 || x1 > image.width()) x1 = image.width();
    if (y1 < 0 || y1 > image.height()) y1 = image.height();
    InkBox b;
    b.minX = image.width();
    b.minY = image.height();
    for (int y = std::max(0, y0); y < y1; ++y)
        for (int x = std::max(0, x0); x < x1; ++x)
            if (isInk(image, x, y)) {
                b.minX = std::min(b.minX, x);
                b.maxX = std::max(b.maxX, x);
                b.minY = std::min(b.minY, y);
                b.maxY = std::max(b.maxY, y);
                ++b.count;
            }
    return b;
}

PdfInfo readPdf(const std::filesystem::path& file) {
    PdfInfo info;
    std::ifstream in(file, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    info.raw = ss.str();
    info.bytes = info.raw.size();
    const std::string& s = info.raw;

    // "/Type /Page" but not "/Type /Pages".
    const std::regex pageRe(R"(/Type\s*/Page(?![A-Za-z]))");
    info.pageCount = static_cast<int>(std::distance(std::sregex_iterator(s.begin(), s.end(), pageRe), {}));
    const std::string n = R"(\s*(-?[0-9]*\.?[0-9]+))";
    const std::regex boxRe("/MediaBox\\s*\\[" + n + n + n + n + "\\s*\\]");
    for (std::sregex_iterator it(s.begin(), s.end(), boxRe), end; it != end; ++it)
        info.mediaBoxes.push_back(
            {std::stod((*it)[1]), std::stod((*it)[2]), std::stod((*it)[3]), std::stod((*it)[4])});
    info.hasImage = std::regex_search(s, std::regex(R"(/Subtype\s*/Image)"));
    info.hasFont = std::regex_search(s, std::regex(R"(/Type\s*/Font(?![A-Za-z]))"));
    return info;
}

std::optional<QImage> rasterizeWithPoppler(const std::filesystem::path& pdf, int page, int dpi) {
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("pdftocairo"));
    if (tool.isEmpty()) return std::nullopt;
    const QString base = QString::fromStdString(pdf.string()) + QStringLiteral(".p") + QString::number(page);
    QProcess proc;
    proc.start(tool, {QStringLiteral("-png"), QStringLiteral("-singlefile"), QStringLiteral("-r"), QString::number(dpi),
                      QStringLiteral("-f"), QString::number(page), QStringLiteral("-l"), QString::number(page),
                      QString::fromStdString(pdf.string()), base});
    if (!proc.waitForFinished(120000) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return std::nullopt;
    QImage image(base + QStringLiteral(".png"));
    if (image.isNull()) return std::nullopt;
    return image;
}

} // namespace fos::printing::test
