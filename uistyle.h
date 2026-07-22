#ifndef UISTYLE_H
#define UISTYLE_H

#include <QMessageBox>
#include <QWidget>

QString buildAdminPanelStyleSheet(int fontPx = 16, int labelMinWidth = 120, int controlHeight = 44);
QString buildMapPageStyleSheet();
void installMessageBoxTuner();
int showAppWarning(QWidget* parent, const QString& title, const QString& text);
int showAppInformation(QWidget* parent, const QString& title, const QString& text);
int showAppCritical(QWidget* parent, const QString& title, const QString& text);
int showAppQuestion(QWidget* parent, const QString& title, const QString& text,
                    QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No);
void applyAdminPanelFont(QWidget* root, int fontPx = 16);
void applyAdminPanelControlSizes(QWidget* root, int inputMinWidth = 240, int controlHeight = 44);
void applyAdminPanelLayoutSpacing(QWidget* root, int spacing = 14, int margin = 16);
void applyFormRowLayout(QWidget* root, int hSpacing = 18, int labelWidth = 96);
void applyMapPageCompactStyle(QWidget* page);

#endif
