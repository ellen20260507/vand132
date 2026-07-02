#include "newdialog.h"
#include "ui_newdialog.h"
#include "pollconfig.h"
#include "uistyle.h"
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QHeaderView>

newdialog::newdialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::newdialog)
{
    ui->setupUi(this);
    this->setWindowTitle("轮询设置");
    setStyleSheet(buildAdminPanelStyleSheet());
    applyAdminPanelFont(this);
    applyAdminPanelControlSizes(this, 180, 44);
    applyAdminPanelLayoutSpacing(this, 14, 16);
    ui->tableWidget->horizontalHeader()->setStretchLastSection(true);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    connect(ui->addRowBtn, &QPushButton::clicked, this, &newdialog::addRowBtn_clicked);
    connect(ui->deleteRowBtn, &QPushButton::clicked, this, &newdialog::deleteRowBtn_clicked);
    connect(ui->saveBtn, &QPushButton::clicked, this, &newdialog::saveBtn_clicked);

    ui->tableWidget->setColumnCount(6);
    ui->tableWidget->setHorizontalHeaderLabels({"地址", "腕带", "台垫", "设备", "尘埃", "离子风机"});

    QFile file(getFilePath());
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "文件错误", "加载失败！无法打开文件: " + file.errorString());
        return;
    }
    QTextStream in(&file);
    ui->tableWidget->setRowCount(0);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        QStringList parts = line.split(",");
        int row = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(row);
        for (int col = 0; col < 6; ++col) {
            QString text = (col < parts.size()) ? parts[col] : "";
            ui->tableWidget->setItem(row, col, new QTableWidgetItem(text));
        }
    }
    file.close();
}

newdialog::~newdialog()
{
    delete ui;
}

void newdialog::addRowBtn_clicked()
{
    int currentRow = ui->tableWidget->rowCount();
    ui->tableWidget->insertRow(currentRow);
    for (int col = 0; col < 6; ++col) {
        ui->tableWidget->setItem(currentRow, col, new QTableWidgetItem(""));
    }
}

void newdialog::deleteRowBtn_clicked()
{
    int selectedRow = ui->tableWidget->currentRow();
    if (selectedRow >= 0) {
        ui->tableWidget->removeRow(selectedRow);
    } else {
        QMessageBox::warning(this, "提示", "请先选中要删除的行！");
    }
}

static bool validatePollConfigFormat(const QString& config, const QString& typePrefix,
                                     const QString& columnLabel, int rowIndex, QWidget* parent)
{
    if (typePrefix == "C") {
        return true;
    }

    PollChannelRange range = parsePollChannelRange(config);
    if (!range.valid) {
        QMessageBox::warning(parent, "错误",
            QString("第%1行【%2】格式应为X-Y（如1-2、2-6），表示从第X通道轮询到第Y通道")
                .arg(rowIndex + 1).arg(columnLabel));
        return false;
    }
    return true;
}

void newdialog::saveBtn_clicked()
{
    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        QTableWidgetItem *addrItem = ui->tableWidget->item(row, 0);
        QTableWidgetItem *wristItem = ui->tableWidget->item(row, 1);
        QTableWidgetItem *matItem = ui->tableWidget->item(row, 2);
        QTableWidgetItem *devItem = ui->tableWidget->item(row, 3);
        QTableWidgetItem *dustItem = ui->tableWidget->item(row, 4);
        QTableWidgetItem *ionFanItem = ui->tableWidget->item(row, 5);

        QString addr = addrItem ? addrItem->text() : "";
        QString wristConfig = wristItem ? wristItem->text() : "";
        QString matConfig = matItem ? matItem->text() : "";
        QString devConfig = devItem ? devItem->text() : "";
        QString dustConfig = dustItem ? dustItem->text() : "";
        QString ionFanConfig = ionFanItem ? ionFanItem->text() : "";

        if (addr.isEmpty()) {
            QMessageBox::warning(this, "错误", QString("第%1行地址不能为空").arg(row + 1));
            return;
        }

        bool addrOk = false;
        const int addrValue = addr.toInt(&addrOk);
        if (!addrOk || addrValue <= 0) {
            QMessageBox::warning(this, "错误", QString("第%1行地址无效").arg(row + 1));
            return;
        }

        struct TypeConfig {
            QString type;
            QString config;
            QString label;
        };
        const TypeConfig configs[] = {
            {"W", wristConfig, "腕带"},
            {"T", matConfig, "台垫"},
            {"E", devConfig, "设备"},
            {"C", dustConfig, "尘埃"},
            {"I", ionFanConfig, "离子风机"}
        };

        bool hasConfig = false;
        for (const TypeConfig& item : configs) {
            if (item.config.isEmpty()) {
                continue;
            }
            hasConfig = true;
            if (!validatePollConfigFormat(item.config, item.type, item.label, row, this)) {
                return;
            }
        }

        if (!hasConfig) {
            QMessageBox::warning(this, "错误", QString("第%1行请至少填写一种设备配置").arg(row + 1));
            return;
        }
    }

    QFile file(getFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::critical(this, "错误", "保存失败: " + file.errorString());
        return;
    }
    QTextStream out(&file);
    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        QStringList cols;
        for (int col = 0; col < 6; ++col) {
            QTableWidgetItem *item = ui->tableWidget->item(row, col);
            cols.append(item ? item->text() : "");
        }
        out << cols.join(",") << "\n";
    }
    file.close();
    QMessageBox::information(this, "成功", "配置已保存");
    accept();
}

QVector<QStringList> newdialog::getTableData()
{
    QVector<QStringList> allRowData;
    int rowCount = ui->tableWidget->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        QStringList currentRow;
        for (int col = 0; col < 6; ++col) {
            QTableWidgetItem *item = ui->tableWidget->item(row, col);
            currentRow.append(item ? item->text() : "");
        }
        allRowData.append(currentRow);
    }
    return allRowData;
}
