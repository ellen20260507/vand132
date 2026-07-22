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
#include <QAbstractItemView>
#include <QHBoxLayout>

newdialog::newdialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::newdialog)
{
    ui->setupUi(this);
    this->setWindowTitle(QStringLiteral("轮询设置"));

    const QString pollDialogExtraStyle = QStringLiteral(
        " QTableWidget QPushButton#pollDownloadBtn {"
        "  min-width: 0px;"
        "  min-height: 36px;"
        "  padding: 4px 10px;"
        "}"
    );
    setStyleSheet(buildAdminPanelStyleSheet() + pollDialogExtraStyle);

    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        resize(qMin(1280, area.width() - 80), qMin(600, area.height() - 120));
    }
    applyAdminPanelFont(this);
    applyAdminPanelControlSizes(this, 180, 44);
    applyAdminPanelLayoutSpacing(this, 14, 16);
    ui->verticalLayout->setStretch(0, 1);

    setupTableColumns();

    connect(ui->addRowBtn, &QPushButton::clicked, this, &newdialog::addRowBtn_clicked);
    connect(ui->deleteRowBtn, &QPushButton::clicked, this, &newdialog::deleteRowBtn_clicked);
    connect(ui->saveBtn, &QPushButton::clicked, this, &newdialog::saveBtn_clicked);
    connect(DBManager::instance(), &DBManager::exportDeviceDataFinished,
            this, &newdialog::onExportFinished);

    loadConfigFromFile();
}

newdialog::~newdialog()
{
    delete ui;
}

namespace {

QPushButton* downloadButtonForRow(QTableWidget* table, int row, int downloadColumn)
{
    QWidget* cell = table->cellWidget(row, downloadColumn);
    if (!cell) {
        return nullptr;
    }
    return cell->findChild<QPushButton*>();
}

} // namespace

void newdialog::setupTableColumns()
{
    QTableWidget* table = ui->tableWidget;
    QHeaderView* header = table->horizontalHeader();

    table->setColumnCount(kDataColumnCount + 1);
    table->setHorizontalHeaderLabels({
        QStringLiteral("地址"), QStringLiteral("腕带"), QStringLiteral("台垫"),
        QStringLiteral("设备"), QStringLiteral("尘埃"), QStringLiteral("离子风机"),
        QStringLiteral("下载")
    });

    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::DoubleClicked
                           | QAbstractItemView::SelectedClicked
                           | QAbstractItemView::EditKeyPressed);

    header->setStretchLastSection(false);
    header->setMinimumSectionSize(64);
    for (int col = 0; col < kDataColumnCount; ++col) {
        header->setSectionResizeMode(col, QHeaderView::Stretch);
    }
    header->setSectionResizeMode(kDataColumnCount, QHeaderView::Fixed);
    updateDownloadColumnWidth();
}

void newdialog::updateDownloadColumnWidth()
{
    QTableWidget* table = ui->tableWidget;
    const QFontMetrics fm(table->font());
    int width = qMax(kDownloadColumnMinWidth,
                     fm.horizontalAdvance(QStringLiteral("导出中...")) + 28);

    for (int row = 0; row < table->rowCount(); ++row) {
        if (QPushButton* btn = downloadButtonForRow(table, row, kDataColumnCount)) {
            width = qMax(width, btn->sizeHint().width() + 16);
        }
    }

    table->setColumnWidth(kDataColumnCount, width);
}

void newdialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    updateDownloadColumnWidth();
}

void newdialog::ensureRowItems(int row)
{
    for (int col = 0; col < kDataColumnCount; ++col) {
        if (!ui->tableWidget->item(row, col)) {
            auto* item = new QTableWidgetItem(QString());
            item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            ui->tableWidget->setItem(row, col, item);
        }
    }
    setupDownloadButton(row);
}

void newdialog::loadConfigFromFile()
{
    ui->tableWidget->setRowCount(0);

    QFile file(getFilePath());
    if (!file.exists()) {
        ui->tableWidget->insertRow(0);
        ensureRowItems(0);
        return;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("文件错误"),
                             QStringLiteral("加载失败！无法打开文件: ") + file.errorString());
        ui->tableWidget->insertRow(0);
        ensureRowItems(0);
        return;
    }

    QTextStream in(&file);
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
            auto* item = new QTableWidgetItem(text);
            item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            ui->tableWidget->setItem(row, col, item);
        }
        setupDownloadButton(row);
    }
    file.close();

    if (ui->tableWidget->rowCount() == 0) {
        ui->tableWidget->insertRow(0);
        ensureRowItems(0);
    }

    updateDownloadColumnWidth();
}

void newdialog::setupDownloadButton(int row)
{
    if (auto* oldBtn = ui->tableWidget->cellWidget(row, kDataColumnCount)) {
        ui->tableWidget->removeCellWidget(row, kDataColumnCount);
        oldBtn->deleteLater();
    }

    auto* container = new QWidget();
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);

    auto* btn = new QPushButton(QStringLiteral("下载"));
    btn->setObjectName(QStringLiteral("pollDownloadBtn"));
    btn->setProperty("row", row);
    btn->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    connect(btn, &QPushButton::clicked, this, &newdialog::onDownloadClicked);
    layout->addWidget(btn);

    ui->tableWidget->setCellWidget(row, kDataColumnCount, container);
    updateDownloadColumnWidth();
}

void newdialog::refreshAllDownloadButtons()
{
    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        if (auto* btn = downloadButtonForRow(ui->tableWidget, row, kDataColumnCount)) {
            btn->setProperty("row", row);
        } else {
            setupDownloadButton(row);
        }
    }
}

void newdialog::addRowBtn_clicked()
{
    const int currentRow = ui->tableWidget->rowCount();
    ui->tableWidget->insertRow(currentRow);
    ensureRowItems(currentRow);
}

void newdialog::deleteRowBtn_clicked()
{
    const int selectedRow = ui->tableWidget->currentRow();
    if (selectedRow >= 0) {
        ui->tableWidget->removeRow(selectedRow);
        if (ui->tableWidget->rowCount() == 0) {
            ui->tableWidget->insertRow(0);
            ensureRowItems(0);
        } else {
            refreshAllDownloadButtons();
        }
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

        const QString addr = addrItem ? addrItem->text().trimmed() : QString();
        const QString wristConfig = wristItem ? wristItem->text().trimmed() : QString();
        const QString matConfig = matItem ? matItem->text().trimmed() : QString();
        const QString devConfig = devItem ? devItem->text().trimmed() : QString();
        const QString dustConfig = dustItem ? dustItem->text().trimmed() : QString();
        const QString ionFanConfig = ionFanItem ? ionFanItem->text().trimmed() : QString();

        if (addr.isEmpty() && wristConfig.isEmpty() && matConfig.isEmpty()
            && devConfig.isEmpty() && dustConfig.isEmpty() && ionFanConfig.isEmpty()) {
            continue;
        }

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
            cols.append(item ? item->text().trimmed() : QString());
        }
        if (cols[0].isEmpty()) {
            continue;
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
            currentRow.append(item ? item->text().trimmed() : QString());
        }
        if (currentRow[0].isEmpty()) {
            continue;
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
    updateDownloadColumnWidth();
    DBManager::instance()->requestExportDeviceData(modbusAddr, path);
}

void newdialog::onExportFinished(int modbusAddr, bool success, const QString& filePath, const QString& errorMessage)
{
    Q_UNUSED(modbusAddr);

    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        auto* btn = downloadButtonForRow(ui->tableWidget, row, kDataColumnCount);
        if (!btn) {
            continue;
        }
        btn->setEnabled(true);
        btn->setText(QStringLiteral("下载"));
    }

    updateDownloadColumnWidth();

    if (success) {
        QMessageBox::information(this, QStringLiteral("导出成功"),
                                 QStringLiteral("设备数据已导出到：\n%1").arg(filePath));
    } else {
        QMessageBox::warning(this, QStringLiteral("导出失败"), errorMessage);
    }
}
