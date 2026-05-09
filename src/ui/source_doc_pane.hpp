#pragma once

#include <QWidget>
#include <utility>
#include <vector>

class QLabel;
class QStackedWidget;
class QTabWidget;
class QPlainTextEdit;

namespace mondoc::ui {

class SourceDocPane : public QWidget {
    Q_OBJECT
public:
    explicit SourceDocPane(QWidget* parent = nullptr);

    void setSourceTexts(const std::vector<std::pair<QString, QString>>& titledBodies);

private:
    QStackedWidget* stack_;
    QLabel* emptyLabel_;
    QPlainTextEdit* singleView_;
    QTabWidget* tabs_;
};

}  // namespace mondoc::ui
