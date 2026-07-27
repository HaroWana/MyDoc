#pragma once
#include <string>
#include <vector>
#include "mondoc/expected.hpp"
#include "mondoc/error.hpp"
#include "mondoc/id.hpp"
#include "confidence.hpp"
#include "fill_session.hpp"
#include "source_ref.hpp"

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

    // Atomically writes `value` unless the field's CURRENT stored state is
    // Manual with a non-empty value, in which case the write is skipped.
    // Returns true if the write happened, false if it was protected.
    virtual mondoc::expected<bool, mondoc::Error>
        upsertValueIfNotManual(const mondoc::FillSessionId& sessionId,
                               const mondoc::FieldId& fieldId,
                               const std::string& value) = 0;

    virtual mondoc::expected<void, mondoc::Error>
        upsertConfidence(const mondoc::FillSessionId& sessionId,
                         const mondoc::FieldId& fieldId,
                         Confidence confidence) = 0;

    virtual mondoc::expected<void, mondoc::Error>
        replaceSourceRefs(const mondoc::FillSessionId& sessionId,
                          const mondoc::FieldId& fieldId,
                          const std::vector<SourceRef>& refs) = 0;
};

}  // namespace mondoc::domain
