#include "detail/odt_text.hpp"

#include <cstddef>
#include <string_view>

namespace mondoc::adapters::formats::detail {

void appendOdtText(const pugi::xml_node& node, std::string& out) {
    for (pugi::xml_node child : node.children()) {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            out += child.value();
            continue;
        }
        if (child.type() != pugi::node_element) continue;

        const std::string_view name{child.name()};
        if (name == "text:tab") {
            out += '\t';
        } else if (name == "text:line-break") {
            out += '\n';
        } else if (name == "text:s") {
            int count = child.attribute("text:c").as_int(1);
            if (count < 1) count = 1;
            out.append(static_cast<std::size_t>(count), ' ');
        } else {
            appendOdtText(child, out);
        }
    }
}

}  // namespace mondoc::adapters::formats::detail
