#include "newdialog.h"
#include "ui_newdialog.h"
#include "pollconfig.h"
#include "uistyle.h"
#include "dbmanager.h"
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QHeaderView>
#include <QFileDialog>

newdialog::newdialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::newdialog)
{
    ui->setupUi(this);
    this->setWindowTitle(QStringLiteral("轮询设置"));
    setStyleSheet(buildAdminPanelStyleSheet());
    applyAdminPanelFont(this);
    applyAdminPanelControlSizes(this, 180, 44);
    applyAdminPanelLayoutSpacing(this, 14, 16);
    ui->tableWidget->horizontalHeader()->setStretchLastSection(false);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(kDataColumnCount, QHeaderView::Fixed);
    ui->tableWidget->setColumnWidth(kDataColumnCount, 90);

    connect(ui->addRowBtn, &QPushButton::clicked, this, &newdialog::addRowBtn_clicked);
    connect(ui->deleteRowBtn, &QPushButton::clicked, this, &newdialog::deleteRowBtn_clicked);
    connect(ui->saveBtn, &QPushButton::clicked, this, &newdialog::saveBtn_clicked);
    connect(DBManager::instance(), &DBManager::exportDeviceDataFinished,
            this, &newdialog::onExportFinished);

    ui->tableWidget->setColumnCount(kDataColumnCount + 1);
    ui->tableWidget->setHorizontalHeaderLabels({
        QStringLiteral("地址"), QStringLiteral("腕带"), QStringLiteral("台垫"),
        QStringLiteral("设备"), QStringLiteral("尘埃"), QStringLiteral("离子风机"),
        QStringLiteral("下载")
    });

    QFile file(getFilePath());
    if (!file.exists()) {
        return;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("文件错误"),
                             QStringLiteral("加载失败！无法打开文件: ") + file.errorString());
        return;
    }
    QTextStream in(&file);
    ui->tableWidget->setRowCount(0);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        QStringList parts = line.split(",");
        const int row = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(row);
        for (int col = 0; col < kDataColumnCount; ++col) {
            const QString text = (col < parts.size()) ? parts[col] : QString();
            ui->tableWidget->setItem(row, col, new QTableWidgetItem(text));
        }
        setupDownloadButton(row);
    }
    file.close();
}

newdialog::~newdialog()
{
    delete ui;
}

void newdialog::setupDownloadButton(int row)
{
    auto* btn = new QPushButton(QStringLiteral("下载"));
    btn->setProperty("row", row);
    connect(btn, &QPushButton::clicked, this, &newdialog::onDownloadClicked);
    ui->tableWidget->setCellWidget(row, kDataColumnCount, btn);
}

void newdialog::refreshAllDownloadButtons()
{
    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        setupDownloadButton(row);
    }
}

void newdialog::addRowBtn_clicked()
{
    const int currentRow = ui->tableWidget->rowCount();
    ui->tableWidget->insertRow(currentRow);
    for (int col = 0; col < kDataColumnCount; ++col) {
        ui->tableWidget->setItem(currentRow, col, new QTableWidgetItem(QString()));
    }
    setupDownloadButton(currentRow);
}

void newdialog::deleteRowBtn_clicked()
{
    const int selectedRow = ui->tableWidget->currentRow();
    if (selectedRow >= 0) {
        ui->tableWidget->removeRow(selectedRow);
        refreshAllDownloadButtons();
    } else {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先选中要删除的行！"));
    }
}

static bool validatePollConfigFormat(const QString& config, const QString& typePrefix,
                                     const QString& columnLabel, int rowIndex, QWidget* parent)
{
    if (typePrefix == "C") {
        return true;
    }

    const PollChannelRange range = parsePollChannelRange(config);
    if (!range.valid) {
        QMessageBox::warning(parent, QStringLiteral("错误"),
            QStringLiteral("第%1行【%2】格式应为X-Y（如1-2、2-6），表示从第X通道轮询到第Y通道")
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

        const QString addr = addrItem ? addrItem->text() : QString();
        const QString wristConfig = wristItem ? wristItem->text() : QString();
        const QString matConfig = matItem ? matItem->text() : QString();
        const QString devConfig = devItem ? devItem->text() : QString();
        const QString dustConfig = dustItem ? dustItem->text() : QString();
        const QString ionFanConfig = ionFanItem ? ionFanItem->text() : QString();

        if (addr.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("第%1行地址不能为空").arg(row + 1));
            return;
        }

        bool addrOk = false;
        const int addrValue = addr.toInt(&addrOk);
        if (!addrOk || addrValue <= 0) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("第%1行地址无效").arg(row + 1));
            return;
        }

        struct TypeConfig {
            QString type;
            QString config;
            QString label;
        };
        const TypeConfig configs[] = {
            {"W", wristConfig, QStringLiteral("腕带")},
            {"T", matConfig, QStringLiteral("台垫")},
            {"E", devConfig, QStringLiteral("设备")},
            {"C", dustConfig, QStringLiteral("尘埃")},
            {"I", ionFanConfig, QStringLiteral("离子风机")}
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
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("第%1行请至少填写一种设备配置").arg(row + 1));
            return;
        }
    }

    QFile file(getFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("保存失败: ") + file.errorString());
        return;
    }
    QTextStream out(&file);
    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        QStringList cols;
        for (int col = 0; col < kDataColumnCount; ++col) {
            QTableWidgetItem *item = ui->tableWidget->item(row, col);
            cols.append(item ? item->text() : QString());
        }
        out << cols.join(",") << "\n";
    }
    file.close();
    QMessageBox::information(this, QStringLiteral("成功"), QStringLiteral("配置已保存"));
    accept();
}

QVector<QStringList> newdialog::getTableData()
{
    QVector<QStringList> allRowData;
    const int rowCount = ui->tableWidget->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        QStringList currentRow;
        for (int col = 0; col < kDataColumnCount; ++col) {
            QTableWidgetItem *item = ui->tableWidget->item(row, col);
            currentRow.append(item ? item->text() : QString());
        }
        allRowData.append(currentRow);
    }
    return allRowData;
}

void newdialog::onDownloadClicked()
{
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) {
        return;
    }

    const int row = btn->property("row").toInt();
    QTableWidgetItem* addrItem = ui->tableWidget->item(row, 0);
    if (!addrItem || addrItem->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先填写设备地址"));
        return;
    }

    bool addrOk = false;
    const int modbusAddr = addrItem->text().trimmed().toInt(&addrOk);
    if (!addrOk || modbusAddr <= 0) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("设备地址无效"));
        return;
    }

    const QString defaultName = QStringLiteral("device_%1_all.csv").arg(modbusAddr);
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出设备全部数据"), defaultName, QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }

    btn->setEnabled(false);
    btn->setText(QStringLiteral("导出中..."));
    DBManager::instance()->requestExportDeviceData(modbusAddr, path);
}

void newdialog::onExportFinished(int modbusAddr, bool success, const QString& filePath, const QString& errorMessage)
{
    Q_UNUSED(modbusAddr);

    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        auto* btn = qobject_cast<QPushButton*>(ui->tableWidget->cellWidget(row, kDataColumnCount));
        if (!btn) {
            continue;
        }
        btn->setEnabled(true);
        btn->setText(QStringLiteral("下载"));
    }

    if (success) {
        QMessageBox::information(this, QStringLiteral("导出成功"),
                                 QStringLiteral("设备数据已导出到：\n%1").arg(filePath));
    } else {
        QMessageBox::warning(this, QStringLiteral("导出失败"), errorMessage);
    }
}
