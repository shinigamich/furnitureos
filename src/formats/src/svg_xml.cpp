// SPDX-License-Identifier: AGPL-3.0-only
// Minimal, safe XML reader for SVG outline import (see svg_xml.hpp for the safety rules).
#include "svg_xml.hpp"

#include <cstdint>
#include <utility>

namespace fos::formats::svg {
namespace {

core::Error parseError(std::size_t line, const std::string& message) {
    return core::makeError(core::ErrorCode::ParseError, "SVG line " + std::to_string(line) + ": " + message);
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool isNameStart(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':' ||
           static_cast<unsigned char>(c) >= 0x80;
}
bool isNameChar(char c) { return isNameStart(c) || (c >= '0' && c <= '9') || c == '-' || c == '.'; }

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

/// Decodes the predefined entities and numeric character references of `raw`; any other
/// entity reference is refused (no entity expansion at all).
core::Result<std::string> decode(std::string_view raw, std::size_t line) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '&') {
            out += raw[i];
            continue;
        }
        const auto semi = raw.find(';', i);
        if (semi == std::string_view::npos || semi - i > 12)
            return parseError(line, "'&' that does not start a valid character or entity reference.");
        const std::string_view ref = raw.substr(i + 1, semi - i - 1);
        if (ref == "amp") out += '&';
        else if (ref == "lt") out += '<';
        else if (ref == "gt") out += '>';
        else if (ref == "quot") out += '"';
        else if (ref == "apos") out += '\'';
        else if (ref.size() >= 2 && ref[0] == '#') {
            const bool hex = ref[1] == 'x';
            const std::string_view digits = ref.substr(hex ? 2 : 1);
            if (digits.empty()) return parseError(line, "empty character reference '&" + std::string(ref) + ";'.");
            std::uint32_t cp = 0;
            for (char c : digits) {
                int d = -1;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
                if (d < 0) return parseError(line, "invalid character reference '&" + std::string(ref) + ";'.");
                cp = cp * (hex ? 16u : 10u) + static_cast<std::uint32_t>(d);
                if (cp > 0x10FFFF) break;
            }
            if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
                return parseError(line, "character reference '&" + std::string(ref) + ";' is not a valid character.");
            appendUtf8(out, cp);
        } else {
            return parseError(line, "entity reference '&" + std::string(ref) +
                                        ";' is refused: only &amp; &lt; &gt; &quot; &apos; and numeric references are "
                                        "accepted (entities are never expanded).");
        }
        i = semi;
    }
    return out;
}

class Reader {
public:
    Reader(std::string_view text, const XmlLimits& limits) : t_(text), limits_(limits) {}

    core::Result<XmlElement> run(std::size_t& count) {
        if (t_.size() >= 2 && ((t_[0] == '\xFE' && t_[1] == '\xFF') || (t_[0] == '\xFF' && t_[1] == '\xFE')))
            return core::makeError(core::ErrorCode::Unsupported, "SVG line 1: UTF-16 text is not supported; save the file as UTF-8.");
        if (t_.find('\0') != std::string_view::npos)
            return core::makeError(core::ErrorCode::Unsupported,
                                   "SVG line 1: the file contains NUL bytes (UTF-16/32 or binary); save it as UTF-8 text.");
        if (t_.substr(0, 3) == "\xEF\xBB\xBF") i_ = 3;
        std::vector<XmlElement> stack;
        bool haveRoot = false;
        XmlElement root;
        while (i_ < t_.size()) {
            if (t_[i_] != '<') {
                const auto next = t_.find('<', i_);
                const std::string_view text = t_.substr(i_, next == std::string_view::npos ? std::string_view::npos : next - i_);
                if (auto d = decode(text, line()); !d) return d.error();
                if (stack.empty())
                    for (char c : text)
                        if (!isSpace(c)) return parseError(line(), "text outside the root element.");
                i_ = next == std::string_view::npos ? t_.size() : next;
                continue;
            }
            const std::size_t at = line();
            if (starts("<?")) {
                if (!skipPast("?>")) return parseError(at, "processing instruction is not closed with '?>'.");
            } else if (starts("<!--")) {
                if (!skipPast("-->")) return parseError(at, "comment is not closed with '-->'.");
            } else if (starts("<![CDATA[")) {
                if (stack.empty()) return parseError(at, "CDATA section outside the root element.");
                if (!skipPast("]]>")) return parseError(at, "CDATA section is not closed with ']]>'.");
            } else if (starts("<!DOCTYPE")) {
                if (haveRoot || !stack.empty()) return parseError(at, "DOCTYPE after the root element.");
                char quote = 0;
                for (++i_; i_ < t_.size(); ++i_) {
                    const char c = t_[i_];
                    if (quote) {
                        if (c == quote) quote = 0;
                    } else if (c == '"' || c == '\'') {
                        quote = c;
                    } else if (c == '[') {
                        return core::makeError(core::ErrorCode::Unsupported,
                                               "SVG line " + std::to_string(at) +
                                                   ": a DOCTYPE with an internal subset (entity declarations) is "
                                                   "refused for safety; remove the DOCTYPE.");
                    } else if (c == '>') {
                        break;
                    }
                }
                if (i_ >= t_.size()) return parseError(at, "DOCTYPE is not closed with '>' (truncated file).");
                ++i_;
            } else if (starts("<!")) {
                return parseError(at, "markup declarations ('<!') other than comments, CDATA and DOCTYPE are not accepted.");
            } else if (starts("</")) {
                i_ += 2;
                const std::string name = readName();
                while (i_ < t_.size() && isSpace(t_[i_])) advance();
                if (i_ >= t_.size() || t_[i_] != '>') return parseError(at, "malformed end tag '</" + name + "'.");
                ++i_;
                if (stack.empty() || stack.back().qname != name)
                    return parseError(at, "end tag '</" + name + ">' does not match " +
                                              (stack.empty() ? std::string("any open element")
                                                             : "'<" + stack.back().qname + ">' opened at line " +
                                                                   std::to_string(stack.back().line)) +
                                              ".");
                XmlElement done = std::move(stack.back());
                stack.pop_back();
                if (stack.empty()) {
                    root = std::move(done);
                    haveRoot = true;
                } else {
                    stack.back().children.push_back(std::move(done));
                }
            } else {
                if (haveRoot) return parseError(at, "a second root element; an XML file has exactly one.");
                ++i_;
                XmlElement el;
                el.line = at;
                el.qname = readName();
                if (el.qname.empty()) return parseError(at, "'<' that does not start a tag.");
                const auto colon = el.qname.rfind(':');
                el.name = colon == std::string::npos ? el.qname : el.qname.substr(colon + 1);
                if (++count > limits_.maxElements)
                    return core::makeError(core::ErrorCode::InvalidArgument,
                                           "SVG line " + std::to_string(at) + ": more than " +
                                               std::to_string(limits_.maxElements) + " elements.");
                if (stack.size() + 1 > limits_.maxDepth)
                    return core::makeError(core::ErrorCode::InvalidArgument,
                                           "SVG line " + std::to_string(at) + ": elements nested deeper than " +
                                               std::to_string(limits_.maxDepth) + " levels.");
                bool selfClosing = false;
                if (auto r = readAttributes(el, selfClosing); !r) return r.error();
                if (selfClosing) {
                    if (stack.empty()) {
                        root = std::move(el);
                        haveRoot = true;
                    } else {
                        stack.back().children.push_back(std::move(el));
                    }
                } else {
                    stack.push_back(std::move(el));
                }
            }
        }
        if (!stack.empty())
            return parseError(line(), "the file ends inside '<" + stack.back().qname + ">' opened at line " +
                                          std::to_string(stack.back().line) + " (truncated file).");
        if (!haveRoot) return parseError(line(), "no root element.");
        return root;
    }

private:
    bool starts(std::string_view s) const { return t_.substr(i_, s.size()) == s; }
    void advance() { ++i_; }

    bool skipPast(std::string_view end) {
        const auto p = t_.find(end, i_);
        if (p == std::string_view::npos) return false;
        i_ = p + end.size();
        return true;
    }

    /// 1-based line of position i_ (incremental; positions only move forward).
    std::size_t line() {
        for (; lineScan_ < i_ && lineScan_ < t_.size(); ++lineScan_)
            if (t_[lineScan_] == '\n') ++line_;
        return line_;
    }

    std::string readName() {
        const std::size_t s = i_;
        if (i_ < t_.size() && isNameStart(t_[i_]))
            while (i_ < t_.size() && isNameChar(t_[i_])) ++i_;
        return std::string(t_.substr(s, i_ - s));
    }

    core::Result<void> readAttributes(XmlElement& el, bool& selfClosing) {
        while (true) {
            const bool hadSpace = i_ < t_.size() && isSpace(t_[i_]);
            while (i_ < t_.size() && isSpace(t_[i_])) ++i_;
            if (i_ >= t_.size()) return parseError(el.line, "tag '<" + el.qname + "' is not closed (truncated file).");
            if (t_[i_] == '>') {
                ++i_;
                return {};
            }
            if (starts("/>")) {
                i_ += 2;
                selfClosing = true;
                return {};
            }
            const std::size_t at = line();
            if (!hadSpace) return parseError(at, "attributes of '<" + el.qname + ">' must be separated by white space.");
            XmlAttribute a;
            a.name = readName();
            if (a.name.empty()) return parseError(at, "malformed attribute in '<" + el.qname + ">'.");
            while (i_ < t_.size() && isSpace(t_[i_])) ++i_;
            if (i_ >= t_.size() || t_[i_] != '=')
                return parseError(at, "attribute '" + a.name + "' of '<" + el.qname + ">' has no '=\"value\"'.");
            ++i_;
            while (i_ < t_.size() && isSpace(t_[i_])) ++i_;
            if (i_ >= t_.size() || (t_[i_] != '"' && t_[i_] != '\''))
                return parseError(at, "value of attribute '" + a.name + "' is not quoted.");
            const char q = t_[i_++];
            const auto end = t_.find(q, i_);
            if (end == std::string_view::npos)
                return parseError(at, "value of attribute '" + a.name + "' is not closed (truncated file).");
            const std::string_view raw = t_.substr(i_, end - i_);
            if (raw.find('<') != std::string_view::npos)
                return parseError(at, "value of attribute '" + a.name + "' contains '<'.");
            auto v = decode(raw, at);
            if (!v) return v.error();
            a.value = std::move(v.value());
            i_ = end + 1;
            for (const auto& o : el.attributes)
                if (o.name == a.name) return parseError(at, "attribute '" + a.name + "' given twice in '<" + el.qname + ">'.");
            if (el.attributes.size() >= limits_.maxAttributes)
                return core::makeError(core::ErrorCode::InvalidArgument,
                                       "SVG line " + std::to_string(at) + ": '<" + el.qname + ">' has more than " +
                                           std::to_string(limits_.maxAttributes) + " attributes.");
            el.attributes.push_back(std::move(a));
        }
    }

    std::string_view t_;
    const XmlLimits& limits_;
    std::size_t i_ = 0;
    std::size_t lineScan_ = 0;
    std::size_t line_ = 1;
};

} // namespace

const std::string* XmlElement::attribute(std::string_view n) const {
    for (const auto& a : attributes)
        if (a.name == n) return &a.value;
    return nullptr;
}

core::Result<XmlElement> parseXml(std::string_view text, const XmlLimits& limits, std::size_t& elementCount) {
    elementCount = 0;
    Reader r(text, limits);
    return r.run(elementCount);
}

} // namespace fos::formats::svg
