#pragma once

#include <pugixml.hpp>

#include <string>

namespace mondoc::adapters::formats::detail {

// Appends the text of an ODF text node to `out`: every pcdata descendant in
// document order, so text before, inside and after inline elements
// (text:span, text:a, ...) is collected exactly once. ODF whitespace elements
// are mapped to their characters: text:tab -> '\t', text:s -> `text:c` spaces
// (default 1), text:line-break -> '\n'.
void appendOdtText(const pugi::xml_node& node, std::string& out, int depth = 0);

}  // namespace mondoc::adapters::formats::detail
