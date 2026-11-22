// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/production/quote.hpp"

#include "fos/core/units.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>

namespace fos::production {
namespace {

std::string quantityText(double v) { return core::formatLength(v, 3); }

std::size_t displayWidth(const std::string& s) {
    std::size_t n = 0;
    for (const char ch : s)
        if ((static_cast<unsigned char>(ch) & 0xC0u) != 0x80u) ++n; // count UTF-8 code points
    return n;
}

} // namespace

std::string formatMoney(double value) {
    if (!std::isfinite(value)) return "nan";
    const double rounded = std::round(value * 100.0) / 100.0;
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, rounded == 0.0 ? 0.0 : rounded, std::chars_format::fixed, 2);
    return std::string(buf, r.ptr);
}

Table quoteTable(const Quote& q) {
    Table t;
    t.header = {"Category", "Item", "Quantity", "Unit", "Unit price", "Amount", "Note"};
    for (const auto& l : q.lines)
        t.rows.push_back({toString(l.category), l.description, quantityText(l.quantity), l.unit,
                          l.priced() ? formatMoney(*l.unitPrice) : "UNPRICED", formatMoney(l.amount), l.note});
    auto total = [&](std::string label, double amount, std::string note = {}) {
        t.rows.push_back({"", std::move(label), "", "", "", formatMoney(amount), std::move(note)});
    };
    total("Subtotal", q.subtotal,
          q.complete ? std::string{} : std::to_string(q.unpricedCount) + " unpriced line(s) not included");
    total("Markup " + quantityText(q.markupPercent) + " %", q.markup);
    total("Tax " + quantityText(q.taxPercent) + " %", q.tax);
    total("TOTAL" + (q.currency.empty() ? std::string{} : " (" + q.currency + ")"), q.total);
    return t;
}

std::string quoteText(const Quote& q) {
    const Table t = quoteTable(q);
    std::vector<std::size_t> width(t.header.size(), 0);
    auto measure = [&](const std::vector<std::string>& row) {
        for (std::size_t i = 0; i < row.size() && i < width.size(); ++i) width[i] = std::max(width[i], displayWidth(row[i]));
    };
    measure(t.header);
    for (const auto& r : t.rows) measure(r);
    std::ostringstream s;
    auto line = [&](const std::vector<std::string>& row) {
        std::string out;
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i > 0) out += "  ";
            out += row[i];
            if (i + 1 < row.size()) out.append(width[i] - displayWidth(row[i]), ' ');
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
        s << out << "\n";
    };
    s << "Quote" << (q.currency.empty() ? std::string{} : " (currency " + q.currency + ", no conversion)")
      << (q.bySheets ? " - materials by sheets from the cutting plan" : " - materials by area") << "\n\n";
    line(t.header);
    std::size_t rule = 0;
    for (const auto w : width) rule += w;
    s << std::string(rule + 2 * (width.size() - 1), '-') << "\n";
    for (std::size_t i = 0; i < t.rows.size(); ++i) {
        if (i == q.lines.size()) s << std::string(rule + 2 * (width.size() - 1), '-') << "\n";
        line(t.rows[i]);
    }
    if (!q.complete)
        s << "\nINCOMPLETE QUOTE: " << q.unpricedCount
          << " line(s) have no price in the libraries and are NOT included in the total.\n";
    return s.str();
}

} // namespace fos::production
