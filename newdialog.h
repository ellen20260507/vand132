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
    QString getFilePath() const {
            return qApp->applicationDirPath() + "/poll_config.txt";
        }
private slots:

    void addRowBtn_clicked();
    void deleteRowBtn_clicked();
    void saveBtn_clicked();
};

#endif // NEWDIALOG_H
