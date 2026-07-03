#ifndef NEWDIALOG_H
#define NEWDIALOG_H

#include <QDialog>
#include <QTableWidgetItem>
#include <QMessageBox>
#include <QDebug>
#include <QStringList>
#include <QFile>
#include <QTextStream>
#include <QApplication>
#include <QPushButton>

namespace Ui {
class newdialog;
}

class newdialog : public QDialog
{
    Q_OBJECT

public:
    explicit newdialog(QWidget *parent = nullptr);
    ~newdialog();
    QVector<QStringList> getTableData();

private:
    Ui::newdialog *ui;
    static const int kDataColumnCount = 6;

    QString getFilePath() const {
        return qApp->applicationDirPath() + "/poll_config.txt";
    }

    void setupTableColumns();
    void ensureRowItems(int row);
    void loadConfigFromFile();
    void setupDownloadButton(int row);
    void refreshAllDownloadButtons();

private slots:
    void addRowBtn_clicked();
    void deleteRowBtn_clicked();
    void saveBtn_clicked();
    void onDownloadClicked();
    void onExportFinished(int modbusAddr, bool success, const QString& filePath, const QString& errorMessage);
};

#endif // NEWDIALOG_H
