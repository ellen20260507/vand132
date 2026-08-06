#include <QHeaderView>
#include "uistyle.h"

#include <QComboBox>
#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QSize>
#include <QSizePolicy>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

void tuneMessageBox(QMessageBox& box)
{
    const QString text = box.text();
    const QFontMetrics metrics(box.font());
    const int textWidth = metrics.horizontalAdvance(text) + 48;
    box.setMinimumWidth(qMax(460, textWidth + 120));

    if (QLabel* label = box.findChild<QLabel*>(QStringLiteral("qt_msgbox_label"))) {
        label->setWordWrap(true);
        label->setMinimumWidth(qMax(360, textWidth));
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    }
    if (QLabel* iconLabel = box.findChild<QLabel*>(QStringLiteral("qt_msgboxex_icon_label"))) {
        iconLabel->setMinimumWidth(0);
        iconLabel->setMaximumWidth(64);
    }
    for (QPushButton* button : box.findChildren<QPushButton*>()) {
        button->setMinimumWidth(88);
    }
}

class MessageBoxTuner : public QObject
{
public:
    explicit MessageBoxTuner(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Show) {
            if (QMessageBox* box = qobject_cast<QMessageBox*>(watched)) {
                tuneMessageBox(*box);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

void installMessageBoxTuner()
{
    static MessageBoxTuner* tuner = nullptr;
    if (!tuner) {
        tuner = new MessageBoxTuner(qApp);
        qApp->installEventFilter(tuner);
    }
}

int showAppWarning(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Ok, parent);
    tuneMessageBox(box);
    return box.exec();
}

int showAppInformation(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(QMessageBox::Information, title, text, QMessageBox::Ok, parent);
    tuneMessageBox(box);
    return box.exec();
}

int showAppCritical(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(QMessageBox::Critical, title, text, QMessageBox::Ok, parent);
    tuneMessageBox(box);
    return box.exec();
}

int showAppQuestion(QWidget* parent, const QString& title, const QString& text,
                    QMessageBox::StandardButtons buttons)
{
    QMessageBox box(QMessageBox::Question, title, text, buttons, parent);
    tuneMessageBox(box);
    return box.exec();
}

QString buildAdminPanelStyleSheet(int fontPx, int labelMinWidth, int controlHeight)
{
    return QString(
        "QWidget#page, QWidget#page_2, QWidget#page_3, QWidget#page_4, "
        "QWidget#page_5, QWidget#page_6, QDialog {"
        "  background-color: #f5f7fb;"
        "}"
        "QLabel {"
        "  color: #1d2129;"
        "  font-size: %1px;"
        "  min-width: %2px;"
        "}"
        "QLabel#labelImage {"
        "  background: transparent;"
        "  border: none;"
        "  min-width: 0px;"
        "}"
        "QComboBox, QLineEdit, QSpinBox {"
        "  background: #ffffff;"
        "  border: 1px solid #c9cdd4;"
        "  border-radius: 6px;"
        "  color: #1d2129;"
        "  font-size: %1px;"
        "  min-height: %3px;"
        "  padding: 4px 12px;"
        "}"
        "QComboBox:focus, QLineEdit:focus, QSpinBox:focus {"
        "  border: 1px solid #165dff;"
        "}"
        "QComboBox::drop-down {"
        "  subcontrol-origin: padding;"
        "  subcontrol-position: center right;"
        "  width: 34px;"
        "  border-left: 1px solid #e5e6eb;"
        "}"
        "QComboBox QAbstractItemView {"
        "  font-size: %1px;"
        "  min-height: %3px;"
        "  selection-background-color: #e8f3ff;"
        "  selection-color: #1d2129;"
        "}"
        "QPushButton {"
        "  background-color: #165dff;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 6px;"
        "  font-size: %1px;"
        "  min-height: %3px;"
        "  min-width: 120px;"
        "  padding: 6px 16px;"
        "}"
        "QPushButton:hover { background-color: #4080ff; }"
        "QPushButton:pressed { background-color: #0e42d2; }"
        "QTextBrowser, QPlainTextEdit {"
        "  background: #ffffff;"
        "  border: 1px solid #e5e6eb;"
        "  border-radius: 8px;"
        "  font-size: 14px;"
        "  padding: 10px;"
        "  color: #1d2129;"
        "}"
        "QTableWidget {"
        "  background: #ffffff;"
        "  border: 1px solid #e5e6eb;"
        "  border-radius: 8px;"
        "  gridline-color: #e5e6eb;"
        "  font-size: %1px;"
        "  selection-background-color: #e8f3ff;"
        "  selection-color: #1d2129;"
        "}"
        "QHeaderView::section {"
        "  background-color: #f2f3f5;"
        "  color: #1d2129;"
        "  font-size: %1px;"
        "  padding: 10px 8px;"
        "  border: none;"
        "  border-bottom: 1px solid #e5e6eb;"
        "  min-height: %3px;"
        "}"
        "QMenuBar {"
        "  font-size: %1px;"
        "  min-height: 38px;"
        "  padding: 4px 8px;"
        "  background: #ffffff;"
        "  border-bottom: 1px solid #e5e6eb;"
        "}"
        "QMenuBar::item { padding: 8px 16px; }"
        "QMenu { font-size: %1px; background: #ffffff; border: 1px solid #e5e6eb; }"
        "QMenu::item { padding: 10px 32px; min-height: 30px; }"
        "QMenu::item:selected { background: #e8f3ff; }"
        "QStatusBar {"
        "  font-size: 14px;"
        "  background: #ffffff;"
        "  border-top: 1px solid #e5e6eb;"
        "  color: #4e5969;"
        "}"
        "QMessageBox {"
        "  background-color: #ffffff;"
        "  min-width: 420px;"
        "}"
        "QMessageBox QLabel {"
        "  min-width: 0px;"
        "  color: #1d2129;"
        "  font-size: %1px;"
        "}"
        "QMessageBox QPushButton {"
        "  min-width: 88px;"
        "  min-height: %3px;"
        "  padding: 6px 16px;"
        "}"
    ).arg(fontPx).arg(labelMinWidth).arg(controlHeight);
}

QString buildMapPageStyleSheet()
{
    return QString(
        "QWidget#page_4 QPushButton {"
        "  font-size: 13px;"
        "  min-height: 28px;"
        "  max-height: 32px;"
        "  min-width: 68px;"
        "  padding: 2px 10px;"
        "}"
        "QWidget#page_4 QComboBox, QWidget#page_4 QLineEdit {"
        "  font-size: 13px;"
        "  min-height: 28px;"
        "  max-height: 32px;"
        "  padding: 2px 8px;"
        "}"
        "QWidget#page_4 QLabel {"
        "  font-size: 13px;"
        "  min-width: 0px;"
        "}"
    );
}

void applyAdminPanelFont(QWidget* root, int fontPx)
{
    if (!root) {
        return;
    }
    QFont font(QStringLiteral("Microsoft YaHei UI"), fontPx);
    root->setFont(font);
    for (QWidget* child : root->findChildren<QWidget*>()) {
        child->setFont(font);
    }
}

void applyAdminPanelControlSizes(QWidget* root, int inputMinWidth, int controlHeight)
{
    if (!root) {
        return;
    }

    const QSize expandSize(16777215, 16777215);
    for (QLineEdit* lineEdit : root->findChildren<QLineEdit*>()) {
        lineEdit->setMaximumSize(expandSize);
        lineEdit->setMinimumHeight(controlHeight);
        lineEdit->setMinimumWidth(qMax(lineEdit->minimumWidth(), inputMinWidth));
        lineEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    for (QComboBox* comboBox : root->findChildren<QComboBox*>()) {
        comboBox->setMinimumHeight(controlHeight);
        comboBox->setMinimumWidth(qMax(comboBox->minimumWidth(), inputMinWidth));
        comboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    for (QPushButton* button : root->findChildren<QPushButton*>()) {
        button->setMinimumHeight(controlHeight);
    }

    for (QTableWidget* table : root->findChildren<QTableWidget*>()) {
        table->verticalHeader()->setDefaultSectionSize(controlHeight + 8);
        table->horizontalHeader()->setMinimumHeight(controlHeight);
    }
}

void applyAdminPanelLayoutSpacing(QWidget* root, int spacing, int margin)
{
    if (!root) {
        return;
    }

    for (QVBoxLayout* layout : root->findChildren<QVBoxLayout*>()) {
        layout->setSpacing(spacing);
        const QMargins margins = layout->contentsMargins();
        if (margins.left() == 0 && margins.top() == 0
            && margins.right() == 0 && margins.bottom() == 0) {
            layout->setContentsMargins(margin, margin, margin, margin);
        }
    }

    for (QHBoxLayout* layout : root->findChildren<QHBoxLayout*>()) {
        layout->setSpacing(spacing);
    }
}

void applyFormRowLayout(QWidget* root, int hSpacing, int labelWidth)
{
    if (!root) {
        return;
    }

    for (QHBoxLayout* layout : root->findChildren<QHBoxLayout*>()) {
        layout->setSpacing(hSpacing);
    }

    for (QLabel* label : root->findChildren<QLabel*>()) {
        if (label->objectName() == QLatin1String("labelImage")) {
            continue;
        }
        label->setMinimumWidth(labelWidth);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    }
}

void applyMapPageCompactStyle(QWidget* page)
{
    if (!page) {
        return;
    }

    page->setStyleSheet(buildMapPageStyleSheet());

    for (QPushButton* button : page->findChildren<QPushButton*>()) {
        button->setMinimumHeight(28);
        button->setMaximumHeight(32);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    }

    for (QComboBox* comboBox : page->findChildren<QComboBox*>()) {
        comboBox->setMinimumHeight(28);
        comboBox->setMaximumHeight(32);
        comboBox->setMinimumWidth(120);
        comboBox->setMaximumWidth(220);
        comboBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    for (QLineEdit* lineEdit : page->findChildren<QLineEdit*>()) {
        lineEdit->setMinimumHeight(28);
        lineEdit->setMaximumHeight(32);
        lineEdit->setMinimumWidth(80);
        lineEdit->setMaximumWidth(160);
        lineEdit->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    for (QLabel* label : page->findChildren<QLabel*>()) {
        if (label->objectName() == QLatin1String("labelImage")) {
            continue;
        }
        label->setMinimumWidth(0);
        label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    }
}
