// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Internal: a deliberately small, non-validating XML reader for SVG outline import (not installed).
//
// Safety rules (SVG files are untrusted input):
//  * no entity expansion beyond the five predefined entities and numeric character references;
//    any other "&name;" is a ParseError;
//  * a DOCTYPE with an internal subset ("[...]", where entities are declared) is refused; an
//    external DTD identifier is ignored and NEVER fetched;
//  * caps on element count, nesting depth and attributes per element; no recursion while parsing;
//  * UTF-16 / UTF-32 text (BOM or NUL bytes) is refused.
#include "fos/core/result.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace fos::formats::svg {

struct XmlAttribute {
    std::string name;  ///< Qualified name as written ("xlink:href").
    std::string value; ///< Entity references decoded.
};

struct XmlElement {
    std::string name;  ///< Local name (namespace prefix removed: "svg:path" -> "path").
    std::string qname; ///< Qualified name as written.
    std::vector<XmlAttribute> attributes;
    std::vector<XmlElement> children;
    std::size_t line = 0; ///< 1-based line of the '<' that opens the element.

    /// Value of the attribute with qualified name `name`, or nullptr.
    const std::string* attribute(std::string_view name) const;
};

struct XmlLimits {
    std::size_t maxElements = 100000;
    std::size_t maxDepth = 64;
    std::size_t maxAttributes = 256; ///< Per element.
};

/// Parses `text` into its root element. Errors are ParseError / Unsupported with messages that
/// start "SVG line N: ". `elementCount` receives the number of elements read.
core::Result<XmlElement> parseXml(std::string_view text, const XmlLimits& limits, std::size_t& elementCount);

} // namespace fos::formats::svg
