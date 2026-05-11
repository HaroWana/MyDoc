#pragma once

#include <QWidget>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "domain/source_ref.hpp"
#include "mondoc/id.hpp"

class QLabel;
class QPlainTextEdit;
class QStackedWidget;
class QTabWidget;

namespace mondoc::ui {

class SourceDocPane : public QWidget {
    Q_OBJECT
public:
    explicit SourceDocPane(QWidget* parent = nullptr);

    void setSourceTexts(const std::vector<std::pair<QString, QString>>& titledBodies);

    void setSourceTexts(
        const std::vector<std::tuple<mondoc::SourceDocId, QString, QString>>& sources);

public slots:
    void highlightRef(const mondoc::domain::SourceRef& ref);

private:
    QStackedWidget* stack_;
    QLabel* emptyLabel_;
    QPlainTextEdit* singleView_;
    QTabWidget* tabs_;
    std::unordered_map<std::string, QPlainTextEdit*> viewByDocId_;
};

}  // namespace mondoc::ui
