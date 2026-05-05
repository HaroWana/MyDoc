#pragma once
#include <vector>
#include "mondoc/expected.hpp"
#include "mondoc/error.hpp"
#include "template.hpp"

namespace mondoc::domain {

class ITemplateRepository {
public:
    virtual ~ITemplateRepository() = default;
    virtual std::expected<void, mondoc::Error> save(const Template& t) = 0;
    virtual std::expected<Template, mondoc::Error> findById(const TemplateId& id) = 0;
    virtual std::expected<std::vector<Template>, mondoc::Error> listAll() = 0;
};

}  // namespace mondoc::domain
