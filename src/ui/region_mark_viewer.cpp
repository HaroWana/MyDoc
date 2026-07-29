#include "region_mark_viewer.hpp"

#include "mondoc/util.hpp"
#include "plain_text_extractor.hpp"

#include <QScrollArea>
#include <QTextBrowser>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QMessageBox>
#include <fstream>
#include <sstream>

#include "ui_style.hpp"

namespace mondoc::ui {

RegionMarkViewer::RegionMarkViewer(const std::filesystem::path& sourcePath,
                                   const QString& templateName,
                                   QWidget* parent)
    : QDialog(parent),
      source_path_(sourcePath),
      text_browser_(new QTextBrowser(this)),
      instruction_label_(new QLabel(
          tr("Select text in the document to mark where this field appears."), this)),
      name_prompt_label_(new QLabel(tr("Name this field:"), this)),
      name_edit_(new QLineEdit(this)),
      type_prompt_label_(new QLabel(tr("Field type:"), this)),
      type_combo_(new QComboBox(this)),
      confirm_region_btn_(new QPushButton(tr("Mark Location"), this)),
      save_field_btn_(new QPushButton(tr("Save Field"), this)),
      close_btn_(new QPushButton(tr("Close without Saving"), this))
{
    setWindowTitle(tr("Mark Field Region — %1").arg(templateName));
    setModal(true);
    setMinimumSize(700, 500);

    type_combo_->addItem(tr("Text"),      static_cast<int>(mondoc::domain::FieldType::Text));
    type_combo_->addItem(tr("Paragraph"), static_cast<int>(mondoc::domain::FieldType::Paragraph));
    type_combo_->addItem(tr("Number"),    static_cast<int>(mondoc::domain::FieldType::Number));
    type_combo_->addItem(tr("Date"),      static_cast<int>(mondoc::domain::FieldType::Date));
    type_combo_->addItem(tr("Checkbox"),  static_cast<int>(mondoc::domain::FieldType::Checkbox));
    type_combo_->addItem(tr("Dropdown"),  static_cast<int>(mondoc::domain::FieldType::Dropdown));

    name_edit_->setAccessibleName(tr("Field name"));
    type_combo_->setAccessibleName(tr("Field type"));
    confirm_region_btn_->setAccessibleName(tr("Mark location"));
    save_field_btn_->setAccessibleName(tr("Save field"));
    close_btn_->setAccessibleName(tr("Close without saving"));

    confirm_region_btn_->setStyleSheet(accentButtonStyle());
    save_field_btn_->setStyleSheet(accentButtonStyle());

    confirm_region_btn_->setEnabled(false);
    save_field_btn_->setEnabled(false);

    name_prompt_label_->setVisible(false);
    name_edit_->setVisible(false);
    type_prompt_label_->setVisible(false);
    type_combo_->setVisible(false);
    save_field_btn_->setVisible(false);

    text_browser_->setReadOnly(true);
    scroll_area_ = new QScrollArea(this);
    scroll_area_->setWidgetResizable(false);
    scroll_area_->setContentsMargins(0, 0, 0, 0);
    scroll_area_->setWidget(text_browser_);

    const bool loaded = loadTextDocument(source_path_);
    confirm_region_btn_->setEnabled(loaded);
    if (!loaded) {
        instruction_label_->setText(
            tr("The document could not be read, so a location cannot be marked."));
        confirm_region_btn_->setToolTip(
            tr("The document could not be read, so a location cannot be marked."));
    }

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    btnRow->addWidget(close_btn_);
    btnRow->addWidget(confirm_region_btn_);
    btnRow->addWidget(save_field_btn_);

    auto* subStepRow = new QHBoxLayout;
    subStepRow->addWidget(name_prompt_label_);
    subStepRow->addWidget(name_edit_);
    subStepRow->addSpacing(16);
    subStepRow->addWidget(type_prompt_label_);
    subStepRow->addWidget(type_combo_);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 8, 0, 8);
    root->setSpacing(8);
    root->addWidget(instruction_label_);
    root->addWidget(scroll_area_, 1);
    root->addLayout(subStepRow);
    root->addLayout(btnRow);

    connect(confirm_region_btn_, &QPushButton::clicked, this, &RegionMarkViewer::onConfirmRegion);
    connect(save_field_btn_,     &QPushButton::clicked, this, &RegionMarkViewer::onSaveField);
    connect(close_btn_,         &QPushButton::clicked, this, &RegionMarkViewer::onCloseWithoutSaving);
    connect(name_edit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        save_field_btn_->setEnabled(!text.trimmed().isEmpty());
    });
}

bool RegionMarkViewer::loadTextDocument(const std::filesystem::path& path) {
    std::string content;

    const bool needsExtraction = mondoc::hasExtension(path, ".docx") ||
                                  mondoc::hasExtension(path, ".odt") ||
                                  mondoc::hasExtension(path, ".pdf");

    if (needsExtraction) {
        auto extracted = mondoc::adapters::formats::extractPlainText(path);
        if (!extracted) {
            const QString errMsg = tr("Could not read document: %1")
                .arg(QString::fromStdString(extracted.error().message()));
            text_browser_->setHtml(
                QStringLiteral("<html><body><p style=\"color:#B91C1C;font-weight:600;\">%1</p></body></html>")
                    .arg(errMsg.toHtmlEscaped()));
            return false;
        }
        content = *extracted;
    } else {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        content = ss.str();
    }

    // Plain text, not HTML: toPlainText() must reproduce `content` byte-for-byte
    // so onConfirmRegion's offset math stays aligned with extractPlainText()'s
    // output. HTML rendering (previously used) collapses whitespace runs and
    // loses inter-paragraph spacing as a visual affordance in exchange.
    text_browser_->setPlainText(QString::fromStdString(content));
    return true;
}

void RegionMarkViewer::onConfirmRegion() {
    const QTextCursor cur = text_browser_->textCursor();
    if (cur.hasSelection()) {
        const QString full = text_browser_->toPlainText();
        const QString beforeStart = full.left(cur.selectionStart());
        const QString beforeEnd   = full.left(cur.selectionEnd());
        mondoc::domain::TextLocation tl;
        tl.char_offset     = static_cast<int>(beforeStart.toUtf8().size());
        tl.char_end        = static_cast<int>(beforeEnd.toUtf8().size());
        tl.paragraph_index = static_cast<int>(beforeStart.count(QLatin1Char('\n')));
        QString sel = full.mid(cur.selectionStart(),
                               cur.selectionEnd() - cur.selectionStart());
        if (sel.size() > 120) sel = sel.left(120);
        tl.excerpt = sel.toStdString();
        pending_location_ = mondoc::domain::FieldLocation{std::nullopt, tl};
    } else {
        pending_location_ = std::nullopt;
    }
    instruction_label_->setVisible(false);
    name_prompt_label_->setVisible(true);
    name_edit_->setVisible(true);
    type_prompt_label_->setVisible(true);
    type_combo_->setVisible(true);
    save_field_btn_->setVisible(true);
    confirm_region_btn_->setVisible(false);
    name_edit_->setFocus();
}

void RegionMarkViewer::onSaveField() {
    mondoc::domain::Field f;
    f.name_     = name_edit_->text().trimmed().toStdString();
    f.type_     = static_cast<mondoc::domain::FieldType>(type_combo_->currentData().toInt());
    f.origin_   = mondoc::domain::FieldOrigin::Unknown;
    f.location_ = pending_location_;
    field_ = f;
    accept();
}

void RegionMarkViewer::onCloseWithoutSaving() {
    if (pending_location_) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("Discard Region?"));
        box.setText(tr("Discard this region without saving the field?"));
        auto* discardBtn = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
        auto* keepBtn = box.addButton(tr("Keep Editing"), QMessageBox::RejectRole);
        box.setDefaultButton(keepBtn);
        box.exec();
        if (box.clickedButton() != discardBtn) return;
    }
    reject();
}

mondoc::domain::Field RegionMarkViewer::field() const {
    return field_;
}

}  // namespace mondoc::ui
