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
    void onAiFailed(QString message, int errorKind);
    void onAiCancelled();
    void onChatRefinementRequested(QString prompt,
                                   std::vector<mondoc::domain::AiExtractedFact> lastFacts);
    void onChatRefineFinished(std::vector<mondoc::domain::Fill> fills);
    void onChatRefineFailed(QString message);

private:
    void buildToolbar(QWidget* host);
    void buildSplitter(QWidget* host);
    void buildRightPane(QWidget* host);
    void updateAiControlsVisibility();
    void capturePreFillSnapshot();
    void restorePreFillSnapshot();
    std::vector<mondoc::domain::AiSourceDoc> currentSources() const;
    void showAiErrorDialog(const mondoc::Error& error);
    void shutdownThread(QThread*& t, AiFillWorker*& worker, bool mustJoin);
    void shutdownThread(QThread*& t, AiRefineWorker*& worker, bool mustJoin);

    mondoc::services::FillSessionService& service_;
    mondoc::domain::ITemplateRepository& template_repo_;
    mondoc::FillSessionId current_session_id_;
    mondoc::TemplateId current_template_id_;
    int session_generation_ = 0;
    int ai_fill_generation_  = 0;
    int refine_generation_  = 0;

    QPushButton* back_btn_       = nullptr;
    QLabel* template_name_label_  = nullptr;
    QCheckBox* ai_toggle_        = nullptr;
    QPushButton* fill_with_ai_btn_ = nullptr;
    QLabel* ai_status_label_      = nullptr;
    QPushButton* save_draft_btn_  = nullptr;
    QPushButton* export_btn_     = nullptr;

    QSplitter* splitter_    = nullptr;
    SourceDocPane* source_pane_ = nullptr;
    QWidget* right_pane_     = nullptr;
    FieldFormPane* field_pane_ = nullptr;
    ChatPane* chat_pane_     = nullptr;
    QUndoStack* undo_stack_  = nullptr;

    QThread* ai_thread_      = nullptr;
    AiFillWorker* ai_worker_ = nullptr;
    QThread* refine_thread_  = nullptr;
    AiRefineWorker* refine_worker_ = nullptr;

    QHash<QString, QString> pre_fill_snapshot_;
    std::vector<mondoc::SourceDocId> source_doc_ids_;
    std::vector<std::pair<mondoc::SourceDocId, QString>> source_texts_;
    std::vector<std::pair<mondoc::SourceDocId, QString>> source_titles_;
};

}  // namespace mondoc::ui
