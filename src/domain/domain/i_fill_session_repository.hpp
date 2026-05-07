#pragma once
#include <string>
#include <vector>
#include "mondoc/expected.hpp"
#include "mondoc/error.hpp"
#include "mondoc/id.hpp"
#include "fill_session.hpp"

namespace mondoc::domain {

class IFillSessionRepository {
public:
    virtual ~IFillSessionRepository() = default;

    virtual mondoc::expected<void, mondoc::Error>
        save(const FillSession& session) = 0;

    virtual mondoc::expected<FillSession, mondoc::Error>
        findById(const mondoc::FillSessionId& id) = 0;

    virtual mondoc::expected<std::vector<FillSession>, mondoc::Error>
        listDrafts() = 0;

    virtual mondoc::expected<void, mondoc::Error>
        remove(const mondoc::FillSessionId& id) = 0;

    virtual mondoc::expected<void, mondoc::Error>
        upsertValue(const mondoc::FillSessionId& sessionId,
                    const mondoc::FieldId& fieldId,
                    const std::string& value) = 0;
};

}  // namespace mondoc::domain
