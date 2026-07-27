#pragma once

#include <QHash>
#include <QString>
#include <QWidget>
#include <filesystem>
#include <utility>
#include <vector>

#include "domain/i_template_repository.hpp"
#include "fill_session_service.hpp"
#include "mondoc/id.hpp"

class QCheckBox;
class QLabel;
class QPushButton;
class QSplitter;
class QThread;
class QUndoStack;

namespace mondoc::ui {

class AiFillWorker;
class AiRefineWorker;
class ChatPane;
class FieldFormPane;
class SourceDocPane;

class FillSessionView : public QWidget {
    Q_OBJECT
public:
    FillSessionView(mondoc::services::FillSessionService& service,
                    mondoc::domain::ITemplateRepository& templateRepo,
                    QWidget* parent = nullptr);
    ~FillSessionView() override;

    bool openSession(const mondoc::FillSessionId& id,
                     const std::vector<std::filesystem::path>& sourcePaths,
                     QString* errorOut);
    void clearSession();

signals:
    void backRequested();
    void draftSaved();
    void sessionExported(QString fileName);
    void exportFailed(QString message);
    void statusMessageRequested(QString message, int timeoutMs);

private slots:
    void onBackClicked();
    void onSaveDraftClicked();
    void onExportClicked();
    void onAiToggleChanged(int state);
    void onFillWithAiClicked();
    void onAiFinished(std::vector<mondoc::domain::Fill> fills);
    void onAiFailed(QString message);
    void onAiCancelled();
    void onChatRefinementRequested(QString prompt,
                                   std::vector<mondoc::services::AiExtractedFact> lastFacts);
    void onChatRefineFinished(std::vector<mondoc::domain::Fill> fills);
    void onChatRefineFailed(QString message);

private:
    void buildToolbar(QWidget* host);
    void buildSplitter(QWidget* host);
    void buildRightPane(QWidget* host);
    void updateAiControlsVisibility();
    void capturePreFillSnapshot();
    void restorePreFillSnapshot();
    std::vector<mondoc::services::AiFillSourceInput> currentSources() const;
    void showAiErrorDialog(mondoc::services::AiFailureKind kind, const QString& message);
    void shutdownThread(QThread*& t, AiFillWorker*& worker, bool mustJoin);
    void shutdownThread(QThread*& t, AiRefineWorker*& worker, bool mustJoin);

    mondoc::services::FillSessionService& service_;
    mondoc::domain::ITemplateRepository& templateRepo_;
    mondoc::FillSessionId currentSessionId_;
    mondoc::TemplateId currentTemplateId_;

    QPushButton* backBtn_       = nullptr;
    QLabel* templateNameLabel_  = nullptr;
    QCheckBox* aiToggle_        = nullptr;
    QPushButton* fillWithAiBtn_ = nullptr;
    QLabel* aiStatusLabel_      = nullptr;
    QPushButton* saveDraftBtn_  = nullptr;
    QPushButton* exportBtn_     = nullptr;

    QSplitter* splitter_    = nullptr;
    SourceDocPane* sourcePane_ = nullptr;
    QWidget* rightPane_     = nullptr;
    FieldFormPane* fieldPane_ = nullptr;
    ChatPane* chatPane_     = nullptr;
    QUndoStack* undoStack_  = nullptr;

    QThread* aiThread_      = nullptr;
    AiFillWorker* aiWorker_ = nullptr;
    QThread* refineThread_  = nullptr;
    AiRefineWorker* refineWorker_ = nullptr;

    QHash<QString, QString> preFillSnapshot_;
    std::vector<mondoc::SourceDocId> sourceDocIds_;
    std::vector<std::pair<mondoc::SourceDocId, QString>> sourceTexts_;
    std::vector<std::pair<mondoc::SourceDocId, QString>> sourceTitles_;
};

}  // namespace mondoc::ui
