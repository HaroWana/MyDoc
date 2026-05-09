#pragma once

#include <QString>
#include <QWidget>
#include <filesystem>
#include <vector>

#include "domain/i_template_repository.hpp"
#include "fill_session_service.hpp"
#include "mondoc/id.hpp"

class QLabel;
class QPushButton;
class QSplitter;
class QUndoStack;

namespace mondoc::ui {

class SourceDocPane;
class FieldFormPane;

class FillSessionView : public QWidget {
    Q_OBJECT
public:
    FillSessionView(mondoc::services::FillSessionService& service,
                    mondoc::domain::ITemplateRepository& templateRepo,
                    QWidget* parent = nullptr);

    bool openSession(const mondoc::FillSessionId& id,
                     const std::vector<std::filesystem::path>& sourcePaths,
                     QString* errorOut);
    void clearSession();

signals:
    void backRequested();
    void draftSaved();
    void sessionExported(QString fileName);
    void exportFailed(QString message);

private slots:
    void onBackClicked();
    void onSaveDraftClicked();
    void onExportClicked();

private:
    void buildToolbar(QWidget* host);
    void buildSplitter(QWidget* host);

    mondoc::services::FillSessionService& service_;
    mondoc::domain::ITemplateRepository& templateRepo_;
    mondoc::FillSessionId currentSessionId_;
    mondoc::TemplateId currentTemplateId_;

    QPushButton* backBtn_;
    QLabel* templateNameLabel_;
    QPushButton* saveDraftBtn_;
    QPushButton* exportBtn_;

    QSplitter* splitter_;
    SourceDocPane* sourcePane_;
    FieldFormPane* fieldPane_;
    QUndoStack* undoStack_;
};

}  // namespace mondoc::ui
