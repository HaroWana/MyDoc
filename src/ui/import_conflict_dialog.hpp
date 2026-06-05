#pragma once
#include <QDialog>
#include <QString>

namespace mondoc::ui {

enum class ConflictChoice { Overwrite, Copy, Cancel };

class ImportConflictDialog : public QDialog {
    Q_OBJECT
public:
    explicit ImportConflictDialog(const QString& conflictingName, QWidget* parent = nullptr);

    ConflictChoice choice() const { return choice_; }

private:
    ConflictChoice choice_ = ConflictChoice::Cancel;
};

}  // namespace mondoc::ui
