#pragma once

#include <QScrollArea>
#include <filesystem>
#include <optional>
#include <vector>

#include "domain/field.hpp"
#include "mondoc/id.hpp"

class QLabel;
class QVBoxLayout;
class QPdfDocument;
class QWheelEvent;

namespace mondoc::ui {

class CanvasPageWidget;  // internal, defined in document_canvas.cpp

class DocumentCanvas : public QScrollArea {
    Q_OBJECT
public:
    explicit DocumentCanvas(QWidget* parent = nullptr);

    bool loadDocument(const std::filesystem::path& previewPdf);  // false on load failure
    void clearDocument();
    void setFrames(const std::vector<mondoc::domain::Field>& fields);
    void setSelectedField(const mondoc::FieldId& id);
    void setStaleWarning(bool visible);

signals:
    void frameDrawn(mondoc::domain::PdfLocation rect);
    void frameSelected(mondoc::FieldId id);
    void frameChanged(mondoc::FieldId id, mondoc::domain::PdfLocation rect);

protected:
    void wheelEvent(QWheelEvent* event) override;

private:
    void rebuildPages();
    void rerenderPages();
    void applyFrames();

    static constexpr int kPageWidthPx = 900;

    QWidget* container_;
    QVBoxLayout* layout_;
    QLabel* warning_label_;
    QPdfDocument* document_ = nullptr;
    std::vector<CanvasPageWidget*> pages_;
    std::vector<mondoc::domain::Field> fields_;
    std::optional<mondoc::FieldId> selected_id_;
    double zoom_ = 1.0;
};

}  // namespace mondoc::ui
