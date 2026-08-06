#include "esdeditpanel.h"
#include "./ui_esdeditpanel.h"
#include "uistyle.h"
#include <QMessageBox>
#include <QLabel>
#include <QGridLayout>
#include <QDateTime>

EsdEditPanel::EsdEditPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::EsdEditPanel)
    , pendingLimitChannel(-1)
    , pendingLimitRangeStart(1)
    , pendingLimitRangeCount(1)
    , pendingLimitReadTarget(LimitReadUpper)
    , pendingGroundParamRead(false)
    , pendingTempParamRead(false)
    , m_connectionChecker(nullptr)
{
    for (int i = 0; i < 8; i++) {
        channelUpperLimitEdits[i] = nullptr;
    }
    ui->setupUi(this);

    if (QGridLayout* channelGrid = ui->deviceEditGrid) {
        channelGrid->setColumnStretch(1, 1);
        channelGrid->setColumnStretch(3, 1);
        for (int row = 0; row <= 4; ++row) {
            channelGrid->setRowMinimumHeight(row, 44);
        }
    }
    if (ui->leftPanelLayout) {
        ui->leftPanelLayout->setStretch(2, 1);
    }
    if (ui->tab1Layout) {
        ui->tab1Layout->setStretch(0, 0);
        ui->tab1Layout->setStretch(1, 1);
    }
    if (ui->channelSettingsPanel) {
        ui->channelSettingsPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    }

    ui->typeComboBox->addItem("腕带");
    ui->typeComboBox->addItem("台垫");
    ui->typeComboBox->addItem("设备");
    
    updateTriggerComboBox();
    updateDeviceInternalResistanceVisibility();
    initReferenceInfo();
    initChannelUpperLimitDisplays();
    initTimeModifyFields();

    m_timeModifyTimer = new QTimer(this);
    m_timeModifyTimer->setSingleShot(true);
    connect(m_timeModifyTimer, &QTimer::timeout, this, [this]() {
        if (!pendingTimeModifyText.isEmpty()) {
            notifyTimeModifyResult(false);
            currentExpectedAddrFunc.clear();
            pendingSuccessMessage.clear();
        }
    });

    applyEsdEditPanelStyles();

    ui->delayTypeComboBox->addItem("修改主机延迟");
    ui->delayTypeComboBox->addItem("修改从机延迟");

    ui->groundTypeComboBox->addItem("本机接地");
    ui->groundTypeComboBox->addItem("静电接地");

    ui->tempTypeComboBox->addItem("主机温度上限");
    ui->tempTypeComboBox->addItem("从机温度上限");
    
    connect(ui->typeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EsdEditPanel::on_typeComboBox_currentIndexChanged);

    connect(ui->displayAddressLineEdit, &QLineEdit::textChanged,
            this, &EsdEditPanel::updateApplyButtonState);
    updateApplyButtonState();
}

EsdEditPanel::~EsdEditPanel()
{
    delete ui;
}

void EsdEditPanel::setConnectionChecker(ConnectionChecker checker)
{
    m_connectionChecker = checker;
}

bool EsdEditPanel::checkConnection() const
{
    if (m_connectionChecker && m_connectionChecker()) {
        return true;
    }
    QMessageBox::warning(const_cast<EsdEditPanel*>(this), QStringLiteral("警告"),
                         QStringLiteral("请先在连接设置中连接串口"));
    return false;
}

void EsdEditPanel::sendFrame(const QByteArray &frame, const QString &expectedFunc)
{
    currentExpectedAddrFunc = expectedFunc;
    emit frameSendRequested(frame, expectedFunc);
}

void EsdEditPanel::handleToolFailed(const QString &reason)
{
    if (!pendingTimeModifyText.isEmpty()) {
        if (m_timeModifyTimer) {
            m_timeModifyTimer->stop();
        }
        Q_UNUSED(reason);
        notifyTimeModifyResult(false);
        currentExpectedAddrFunc.clear();
        pendingSuccessMessage.clear();
        pendingGroundParamRead = false;
        pendingTempParamRead = false;
        return;
    }

    currentExpectedAddrFunc.clear();
    pendingSuccessMessage.clear();
    pendingGroundParamRead = false;
    pendingTempParamRead = false;
    QMessageBox::warning(this, QStringLiteral("提示"), reason);
}

void EsdEditPanel::handleToolResponse(const QByteArray &frame, const QString &expectedFunc)
{
    processValidFrame(frame, expectedFunc);
    currentExpectedAddrFunc.clear();
}

void EsdEditPanel::on_applyButton_clicked()
{
    if (!checkConnection()) {
        return;
    }

    bool addrOk;
    int displayAddr = ui->displayAddressLineEdit->text().trimmed().toInt(&addrOk);
    if (!addrOk || displayAddr <= 0) {
        return;
    }

    QString hexAddr = QString("%1").arg(displayAddr, 4, 16, QChar('0')).toUpper();

    bool inputOk;
    int inputValue = ui->inputAddressLineEdit->text().toInt(&inputOk);
    if (!inputOk || inputValue < 0) {
        inputValue = 0;
    }

    QString hexValue = QString("%1").arg(inputValue, 4, 16, QChar('0')).toUpper();

    QString modbusStr = hexAddr + "0001" + "0000" + "0001" + hexValue;
    QByteArray sendData = hexStringToByteArray(modbusStr);
    quint16 crc = crc16(sendData);

    QByteArray crcBytes;
    crcBytes.append(crc & 0xFF);
    crcBytes.append((crc >> 8) & 0xFF);

    QByteArray finalData = sendData + crcBytes;
    QString finalHex = byteArrayToHexString(finalData);

    QString formattedHex;
    for (int i = 0; i < finalHex.length(); i += 4) {
        if (i > 0) formattedHex += " ";
        formattedHex += finalHex.mid(i, qMin(4, finalHex.length() - i));
    }
    pendingSuccessMessage = "地址写入成功！";
    sendFrame(finalData, QStringLiteral("0001"));
}

void EsdEditPanel::on_queryButton_clicked()
{
    if (!checkConnection()) {
        ui->displayAddressLineEdit->setText("请先连接串口");
        return;
    }
    
    QString address = "0001";
    
    QByteArray data = hexStringToByteArray("FFFF01010000" + address);
    quint16 crc = crc16(data);
    
    QByteArray crcBytes;
    crcBytes.append(crc & 0xFF);
    crcBytes.append((crc >> 8) & 0xFF);
    
    QByteArray sendData = data + crcBytes;

    QString sendHex = byteArrayToHexString(sendData);
    QString formattedSend = "FFFF 0101 0000 " + address + " " + sendHex.right(4);
    Q_UNUSED(formattedSend);
    sendFrame(sendData, QStringLiteral("0101"));
}

void EsdEditPanel::processValidFrame(const QByteArray &validFrame, const QString &expectedFunc)
{
    const QString currentExpectedAddrFunc = expectedFunc;
    if (currentExpectedAddrFunc.isEmpty()) {
        return;
    }

    QString hexStr = byteArrayToHexString(validFrame);
    QString formattedHex;
    for (int i = 0; i < hexStr.length(); i += 4) {
        if (i > 0) formattedHex += " ";
        formattedHex += hexStr.mid(i, qMin(4, hexStr.length() - i));
    }
    Q_UNUSED(formattedHex);

    if (currentExpectedAddrFunc == "0101" && pendingGroundParamRead && hexStr.length() >= 16) {
        ui->groundValueLineEdit->setText(formatLimitValue(hexStr.mid(12, 4)));
        pendingGroundParamRead = false;
    } else if (currentExpectedAddrFunc == "0101" && pendingTempParamRead && hexStr.length() >= 16) {
        ui->tempValueLineEdit->setText(formatLimitValue(hexStr.mid(12, 4)));
        pendingTempParamRead = false;
    } else if (currentExpectedAddrFunc != "0110" && currentExpectedAddrFunc != "0111"
        && currentExpectedAddrFunc != "0112" && currentExpectedAddrFunc != "0113"
        && currentExpectedAddrFunc != "0114" && currentExpectedAddrFunc != "0115"
        && currentExpectedAddrFunc != "0120" && currentExpectedAddrFunc != "0121"
        && currentExpectedAddrFunc != "0122") {
        QString addressHex = hexStr.left(4);
        bool ok;
        int addressDec = addressHex.toInt(&ok, 16);
        if (ok) {
            ui->displayAddressLineEdit->setText(QString::number(addressDec));
        }
    }

    QString readUpperFunc, readLowerFunc, writeUpperFunc, writeLowerFunc;
    getLimitFuncCodes(ui->typeComboBox->currentText(), readUpperFunc, readLowerFunc,
                      writeUpperFunc, writeLowerFunc);

    if (currentExpectedAddrFunc == readUpperFunc) {
        parseLimitReadValues(validFrame);
    }

    if (currentExpectedAddrFunc == readLowerFunc) {
        parseLimitReadValues(validFrame);
    }

    if (currentExpectedAddrFunc == "0110" && hexStr.length() >= 16) {
        QString dataHex = hexStr.mid(12, 4);
        QString triggerType;
        if (dataHex == "3000") {
            triggerType = "同时有效";
        } else if (dataHex == "2000") {
            triggerType = "光电开关";
        } else if (dataHex == "1000") {
            triggerType = "腕带开关";
        } else if (dataHex == "0000") {
            triggerType = "停止使用";
        } else {
            triggerType = dataHex;
        }
        ui->triggerDisplayLineEdit->setText(triggerType);
    }

    if (currentExpectedAddrFunc == "0113" && hexStr.length() >= 16) {
        QString dataHex = hexStr.mid(12, 4);
        bool ok;
        uint16_t data = dataHex.toUInt(&ok, 16);
        QString status;
        if (ok && (data & 0x1000)) {
            status = "开启";
        } else {
            status = "关闭";
        }
        ui->triggerDisplayLineEdit->setText(status);
    }

    if (currentExpectedAddrFunc == "0120" && hexStr.length() >= 16) {
        QString dataHex = hexStr.mid(12, 4);
        bool ok;
        uint16_t data = dataHex.toUInt(&ok, 16);
        QString status;
        if (ok && (data & 0x1000)) {
            status = "开启";
        } else {
            status = "关闭";
        }
        ui->triggerDisplayLineEdit->setText(status);
    }

    if (!pendingSuccessMessage.isEmpty()) {
        notifyModifySuccess(pendingSuccessMessage);
    } else if (expectedFunc == QStringLiteral("0040") && !pendingTimeModifyText.isEmpty()) {
        notifyTimeModifyResult(true);
    }
}

void EsdEditPanel::on_setChannelButton_clicked()
{
    if (!checkConnection()) {
        return;
    }

    QString type = ui->typeComboBox->currentText();

    bool addrOk = false;
    int address = getSharedDeviceAddress(addrOk);
    if (!addrOk || address <= 0) {
        address = 1;
    }

    int channel;
    QString channelField = buildHexChannelField(ui->valueInputLineEdit->text(), &channel);
    QString hexAddr = QString("%1").arg(address, 4, 16, QChar('0')).toUpper();

    QString modbusStr;
    if (type == "腕带") {
        modbusStr = buildChannelModbusCommand(type, "0110", hexAddr, channelField, "0001");
        currentExpectedAddrFunc = "0110";
    } else if (type == "台垫") {
        modbusStr = buildChannelModbusCommand(type, "0113", hexAddr, channelField);
        currentExpectedAddrFunc = "0113";
    } else if (type == "设备") {
        modbusStr = buildChannelModbusCommand(type, "0120", hexAddr, channelField);
        currentExpectedAddrFunc = "0120";
    } else {
        modbusStr = "FFFF0001" + hexAddr + QString("%1").arg(channel, 4, 16, QChar('0')).toUpper();
    }

    QByteArray sendData = hexStringToByteArray(modbusStr);
    quint16 crc = crc16(sendData);

    QByteArray crcBytes;
    crcBytes.append(crc & 0xFF);
    crcBytes.append((crc >> 8) & 0xFF);

    QByteArray finalData = sendData + crcBytes;
    QString finalHex = byteArrayToHexString(finalData);

    QString formattedHex;
    for (int i = 0; i < finalHex.length(); i += 4) {
        if (i > 0) formattedHex += " ";
        formattedHex += finalHex.mid(i, qMin(4, finalHex.length() - i));
    }
    Q_UNUSED(formattedHex);

    sendFrame(finalData, currentExpectedAddrFunc);
}

quint16 EsdEditPanel::crc16(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    const quint16 polynomial = 0xA001;
    
    for (int i = 0; i < data.size(); i++) {
        crc ^= static_cast<quint8>(data[i]);
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= polynomial;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

QByteArray EsdEditPanel::hexStringToByteArray(const QString &hex)
{
    QByteArray result;
    QString cleanHex = hex;
    cleanHex.remove(QRegExp("[\\s-]"));
    
    for (int i = 0; i < cleanHex.length(); i += 2) {
        QString byte = cleanHex.mid(i, 2);
        result.append(static_cast<char>(byte.toInt(nullptr, 16)));
    }
    return result;
}

QString EsdEditPanel::byteArrayToHexString(const QByteArray &data)
{
    QString hexStr;
    for (int i = 0; i < data.size(); i++) {
        hexStr += QString("%1").arg(static_cast<quint8>(data[i]), 2, 16, QChar('0')).toUpper();
    }
    return hexStr;
}

void EsdEditPanel::notifyModifySuccess(const QString &message)
{
    showAppInformation(this, QStringLiteral("操作成功"), message);
    pendingSuccessMessage.clear();
}

void EsdEditPanel::updateTriggerComboBox()
{
    ui->triggerComboBox->clear();
    QString type = ui->typeComboBox->currentText();
    if (ui->wristbandLabel) {
        ui->wristbandLabel->setText(type.isEmpty() ? QStringLiteral("腕带") : type);
    }
    if (type == "腕带") {
        ui->triggerComboBox->addItem("腕带开关");
        ui->triggerComboBox->addItem("光电开关");
        ui->triggerComboBox->addItem("同时有效");
        ui->triggerComboBox->addItem("停止使用");
    } else {
        ui->triggerComboBox->addItem("开启");
        ui->triggerComboBox->addItem("关闭");
    }
}

void EsdEditPanel::updateDeviceInternalResistanceVisibility()
{
    const bool isDevice = ui->typeComboBox->currentText() == "设备";
    ui->internalResistanceLabel->setVisible(isDevice);
    ui->internalResistanceLineEdit->setVisible(isDevice);
    ui->readInternalResistanceButton->setVisible(isDevice);
    ui->applyInternalResistanceButton->setVisible(isDevice);
}

void EsdEditPanel::initReferenceInfo()
{
    const QString referenceText =
        "默认参数参考\n"
        "────────────────\n"
        "【合格范围】\n"
        "  腕带：0.75-35 MΩ\n"
        "  台垫：0.75-3.5 MΩ\n"
        "  设备0：<25 Ω\n"
        "  温度：5-30 ℃\n"
        "  湿度：10-85 %\n"
        "  等电位：<3.5V\n"
        "\n"
        "【测试范围】\n"
        "  腕带：0-500 MΩ\n"
        "  台垫：0-500 MΩ\n"
        "  设备0：0-200 Ω\n"
        "  温度：-30-45 ℃\n"
        "  湿度：10-99 %\n"
        "  等电位：0-300V\n"
        "  0.5um粒径：0-30W\n"
        "\n"
        "【延迟参数】\n"
        "  延迟报警：3S\n"
        "  延迟输出控制：3S";

    ui->referenceInfoTextEdit->setPlainText(referenceText);
}

void EsdEditPanel::initChannelUpperLimitDisplays()
{
    QWidget *host = ui->channelGridHost;
    if (!host) {
        return;
    }

    QGridLayout *grid = new QGridLayout(host);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    grid->setContentsMargins(0, 0, 0, 0);

    for (int i = 0; i < 8; i++) {
        QLabel *label = new QLabel(QStringLiteral("通道%1").arg(i + 1), host);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QStringLiteral("color: #722ed1; font-weight: bold; background: transparent;"));

        channelUpperLimitEdits[i] = new QLineEdit(host);
        channelUpperLimitEdits[i]->setReadOnly(true);
        channelUpperLimitEdits[i]->setAlignment(Qt::AlignCenter);
        channelUpperLimitEdits[i]->setMinimumWidth(72);

        grid->addWidget(label, 0, i);
        grid->addWidget(channelUpperLimitEdits[i], 1, i);
        grid->setColumnStretch(i, 1);
    }
}

void EsdEditPanel::updateApplyButtonState()
{
    bool addrOk;
    int displayAddr = ui->displayAddressLineEdit->text().trimmed().toInt(&addrOk);
    ui->applyButton->setEnabled(addrOk && displayAddr > 0);
}

void EsdEditPanel::on_typeComboBox_currentIndexChanged(int index)
{
    Q_UNUSED(index);
    updateTriggerComboBox();
    updateDeviceInternalResistanceVisibility();
}

void EsdEditPanel::on_applyChannelButton_clicked()
{
    if (!checkConnection()) {
        return;
    }

    QString type = ui->typeComboBox->currentText();
    QString triggerValue = ui->triggerComboBox->currentText();

    bool addrOk = false;
    int address = getSharedDeviceAddress(addrOk);
    if (!addrOk || address <= 0) {
        address = 1;
    }

    int channel;
    QString channelField = buildHexChannelField(ui->valueInputLineEdit->text(), &channel);
    QString hexAddr = QString("%1").arg(address, 4, 16, QChar('0')).toUpper();

    QString funcCode;
    if (type == "腕带") {
        funcCode = "0010";
    } else if (type == "台垫") {
        funcCode = "0013";
    } else {
        funcCode = "0020";
    }

    QString dataValue;
    if (triggerValue == "腕带开关" || triggerValue == "开启") {
        dataValue = "1000";
    } else if (triggerValue == "光电开关") {
        dataValue = "2000";
    } else if (triggerValue == "同时有效") {
        dataValue = "3000";
    } else {
        dataValue = "0000";
    }

    QString modbusStr = buildChannelModbusCommand(type, funcCode, hexAddr, channelField, dataValue);

    QByteArray sendData = hexStringToByteArray(modbusStr);
    quint16 crc = crc16(sendData);

    QByteArray crcBytes;
    crcBytes.append(crc & 0xFF);
    crcBytes.append((crc >> 8) & 0xFF);

    QByteArray finalData = sendData + crcBytes;
    QString finalHex = byteArrayToHexString(finalData);

    QString formattedHex;
    for (int i = 0; i < finalHex.length(); i += 4) {
        if (i > 0) formattedHex += " ";
        formattedHex += finalHex.mid(i, qMin(4, finalHex.length() - i));
    }
    Q_UNUSED(formattedHex);

    pendingSuccessMessage = QStringLiteral("通道参数修改成功！");
    sendFrame(finalData, funcCode);
}

void EsdEditPanel::applyEsdEditPanelStyles()
{
    setStyleSheet(QStringLiteral(
        "EsdEditPanel { background-color: #f5f7fb; }"
        "EsdEditPanel QTabWidget::pane {"
        "  border: 1px solid #e5e6eb;"
        "  border-radius: 8px;"
        "  background: #ffffff;"
        "  top: -1px;"
        "}"
        "EsdEditPanel QTabBar::tab {"
        "  background: #f2f3f5;"
        "  color: #4e5969;"
        "  border: 1px solid #e5e6eb;"
        "  border-bottom: none;"
        "  border-top-left-radius: 8px;"
        "  border-top-right-radius: 8px;"
        "  padding: 8px 18px;"
        "  margin-right: 4px;"
        "  min-width: 120px;"
        "}"
        "EsdEditPanel QTabBar::tab:selected {"
        "  background: #ffffff;"
        "  color: #165dff;"
        "  font-weight: bold;"
        "}"
        "EsdEditPanel QTabBar::tab:hover { background: #e8f3ff; }"
        "EsdEditPanel QLineEdit, EsdEditPanel QComboBox {"
        "  background: #ffffff;"
        "  border: 1px solid #c9cdd4;"
        "  border-radius: 6px;"
        "  padding: 4px 10px;"
        "  min-height: 32px;"
        "}"
        "EsdEditPanel QLineEdit:focus, EsdEditPanel QComboBox:focus {"
        "  border: 1px solid #165dff;"
        "}"
        "EsdEditPanel QLineEdit:read-only {"
        "  background: #f7f8fa;"
        "  color: #1d2129;"
        "}"
        "EsdEditPanel QPushButton {"
        "  background-color: #165dff;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 6px 14px;"
        "  min-height: 32px;"
        "  min-width: 72px;"
        "}"
        "EsdEditPanel QPushButton:hover { background-color: #4080ff; }"
        "EsdEditPanel QPushButton:pressed { background-color: #0e42d2; }"
        "EsdEditPanel QTextEdit {"
        "  background: #ffffff;"
        "  border: 1px solid #e5e6eb;"
        "  border-radius: 6px;"
        "  padding: 8px;"
        "  color: #1d2129;"
        "}"
    ));

    ui->queryAddrPanel->setStyleSheet(QStringLiteral(
        "QWidget#queryAddrPanel {"
        "  background-color: #e8f3ff;"
        "  border: 1px solid #bedaff;"
        "  border-radius: 8px;"
        "}"
        "QWidget#queryAddrPanel QLabel {"
        "  color: #165dff;"
        "  font-weight: bold;"
        "  background: transparent;"
        "  border: none;"
        "  min-width: 0px;"
        "}"));

    ui->modifyAddrPanel->setStyleSheet(QStringLiteral(
        "QWidget#modifyAddrPanel {"
        "  background-color: #fff7e8;"
        "  border: 1px solid #ffd591;"
        "  border-radius: 8px;"
        "}"
        "QWidget#modifyAddrPanel QLabel {"
        "  color: #d46b08;"
        "  font-weight: bold;"
        "  background: transparent;"
        "  border: none;"
        "  min-width: 0px;"
        "}"));

    ui->channelSettingsPanel->setStyleSheet(QStringLiteral(
        "QWidget#channelSettingsPanel {"
        "  background-color: #f6ffed;"
        "  border: 1px solid #b7eb8f;"
        "  border-radius: 8px;"
        "}"
        "QWidget#channelSettingsPanel QLabel {"
        "  color: #389e0d;"
        "  font-weight: bold;"
        "  background: transparent;"
        "  border: none;"
        "  min-width: 0px;"
        "}"));

    ui->channelGridContainer->setStyleSheet(QStringLiteral(
        "QWidget#channelGridContainer {"
        "  background-color: #f9f0ff;"
        "  border: 1px solid #d3adf7;"
        "  border-radius: 8px;"
        "}"
        "QWidget#channelGridContainer QLabel {"
        "  color: #722ed1;"
        "  font-weight: bold;"
        "  background: transparent;"
        "  border: none;"
        "  min-width: 0px;"
        "}"));

    ui->delayParamPanel->setStyleSheet(QStringLiteral(
        "QWidget#delayParamPanel {"
        "  background-color: #e6fffb;"
        "  border: 1px solid #87e8de;"
        "  border-radius: 8px;"
        "}"
        "QWidget#delayParamPanel QLabel {"
        "  color: #08979c;"
        "  font-weight: bold;"
        "  background: transparent;"
        "  border: none;"
        "  min-width: 0px;"
        "}"));

    ui->groundParamPanel->setStyleSheet(QStringLiteral(
        "QWidget#groundParamPanel {"
        "  background-color: #e8f4ff;"
        "  border: 1px solid #91caff;"
        "  border-radius: 8px;"
        "}"
        "QWidget#groundParamPanel QLabel {"
        "  color: #0958d9;"
        "  font-weight: bold;"
        "  background: transparent;"
        "  border: none;"
        "  min-width: 0px;"
        "}"));

    ui->tempParamPanel->setStyleSheet(QStringLiteral(
        "QWidget#tempParamPanel {"
        "  background-color: #fff1f0;"
        "  border: 1px solid #ffccc7;"
        "  border-radius: 8px;"
        "}"
        "QWidget#tempParamPanel QLabel {"
        "  color: #cf1322;"
        "  font-weight: bold;"
        "  background: transparent;"
        "  border: none;"
        "  min-width: 0px;"
        "}"));

    const QString logPanelStyle = QStringLiteral(
        "QWidget#timeModifyPanel {"
        "  background-color: #fffbe6;"
        "  border: 1px solid #ffe58f;"
        "  border-radius: 8px;"
        "}"
        "QWidget#timeModifyPanel QLabel#timeModifyTitleLabel {"
        "  color: #ad6800;"
        "  font-weight: bold;"
        "  background: transparent;"
        "  border: none;"
        "  min-width: 0px;"
        "}"
        "QWidget#timeModifyPanel QLabel#timeYearLabel,"
        "QWidget#timeModifyPanel QLabel#timeMonthLabel,"
        "QWidget#timeModifyPanel QLabel#timeDayLabel,"
        "QWidget#timeModifyPanel QLabel#timeWeekdayLabel,"
        "QWidget#timeModifyPanel QLabel#timeHourLabel,"
        "QWidget#timeModifyPanel QLabel#timeMinuteLabel,"
        "QWidget#timeModifyPanel QLabel#timeSecondLabel {"
        "  color: #ad6800;"
        "  font-weight: normal;"
        "  background: transparent;"
        "  border: none;"
        "  min-width: 0px;"
        "  padding-right: 8px;"
        "}"
        "QWidget#timeModifyPanel QPushButton#syncPcTimeButton {"
        "  min-width: 156px;"
        "}");
    ui->timeModifyPanel->setStyleSheet(logPanelStyle);

    ui->referencePanel->setStyleSheet(QStringLiteral(
        "QWidget#referencePanel {"
        "  background-color: #f7f9fc;"
        "  border: 1px solid #c5d4e8;"
        "  border-radius: 8px;"
        "}"
        "QWidget#referencePanel QLabel {"
        "  color: #1d4f91;"
        "  font-weight: bold;"
        "  background: transparent;"
        "  border: none;"
        "  min-width: 0px;"
        "}"
        "QWidget#referencePanel QTextEdit {"
        "  background: #ffffff;"
        "  border: 1px solid #e5e6eb;"
        "  border-radius: 6px;"
        "  padding: 8px;"
        "  font-size: 13px;"
        "  color: #1d2129;"
        "}"));

    for (int i = 0; i < 8; i++) {
        if (!channelUpperLimitEdits[i]) {
            continue;
        }
        channelUpperLimitEdits[i]->setStyleSheet(QStringLiteral(
            "QLineEdit {"
            "  background: #ffffff;"
            "  border: 1px solid #d3adf7;"
            "  border-radius: 4px;"
            "  padding: 2px 4px;"
            "  min-height: 28px;"
            "}"));
    }
}

void EsdEditPanel::on_applyDelayButton_clicked()
{
    if (!checkConnection()) {
        return;
    }

    bool addrOk = false;
    int address = getSharedDeviceAddress(addrOk);
    if (!addrOk || address <= 0) {
        QMessageBox::warning(this, "警告", "请先在查询地址中填写有效地址");
        return;
    }

    bool delayOk;
    int delayTime = ui->delayTimeLineEdit->text().toInt(&delayOk);
    if (!delayOk || delayTime < 0) {
        QMessageBox::warning(this, "警告", "请输入有效的延迟时间");
        return;
    }

    QString hexAddr = QString("%1").arg(address, 4, 16, QChar('0')).toUpper();
    QString hexDelay = QString("%1").arg(delayTime, 4, 16, QChar('0')).toUpper();

    QString registerAddr;
    QString delayType = ui->delayTypeComboBox->currentText();
    if (delayType == "修改主机延迟") {
        registerAddr = "0003";
        pendingSuccessMessage = "主机延迟修改成功！";
    } else {
        registerAddr = "0004";
        pendingSuccessMessage = "从机延迟修改成功！";
    }

    QString modbusStr = hexAddr + "0001" + registerAddr + "0001" + hexDelay;
    QByteArray sendData = hexStringToByteArray(modbusStr);
    quint16 crc = crc16(sendData);

    QByteArray crcBytes;
    crcBytes.append(crc & 0xFF);
    crcBytes.append((crc >> 8) & 0xFF);

    QByteArray finalData = sendData + crcBytes;
    QString finalHex = byteArrayToHexString(finalData);

    QString formattedHex;
    for (int i = 0; i < finalHex.length(); i += 4) {
        if (i > 0) formattedHex += " ";
        formattedHex += finalHex.mid(i, qMin(4, finalHex.length() - i));
    }
    Q_UNUSED(formattedHex);

    sendFrame(finalData, QStringLiteral("0001"));
}

bool EsdEditPanel::getSysParamAddress(int &address)
{
    bool addrOk = false;
    address = getSharedDeviceAddress(addrOk);
    if (!addrOk || address <= 0) {
        QMessageBox::warning(this, "警告", "请先在查询地址中填写有效地址");
        return false;
    }
    return true;
}

QString EsdEditPanel::getGroundRegisterAddr() const
{
    if (ui->groundTypeComboBox->currentText() == "本机接地") {
        return "0001";
    }
    return "0002";
}

QString EsdEditPanel::getTempRegisterAddr() const
{
    if (ui->tempTypeComboBox->currentText() == "主机温度上限") {
        return "0006";
    }
    return "0007";
}

void EsdEditPanel::sendSysParamCommand(int address, const QString &funcCode,
                                     const QString &registerAddr, const QString &dataSuffix,
                                     const QString &logTag)
{
    QString hexAddr = QString("%1").arg(address, 4, 16, QChar('0')).toUpper();
    QString modbusStr = hexAddr + funcCode + registerAddr + "0001" + dataSuffix;
    QByteArray sendData = hexStringToByteArray(modbusStr);
    quint16 crc = crc16(sendData);

    QByteArray crcBytes;
    crcBytes.append(crc & 0xFF);
    crcBytes.append((crc >> 8) & 0xFF);

    QByteArray finalData = sendData + crcBytes;
    QString finalHex = byteArrayToHexString(finalData);

    QString formattedHex;
    for (int i = 0; i < finalHex.length(); i += 4) {
        if (i > 0) formattedHex += " ";
        formattedHex += finalHex.mid(i, qMin(4, finalHex.length() - i));
    }
    Q_UNUSED(formattedHex);
    Q_UNUSED(logTag);

    sendFrame(finalData, funcCode);
}

void EsdEditPanel::on_readGroundButton_clicked()
{
    if (!checkConnection()) {
        return;
    }

    int address;
    if (!getSysParamAddress(address)) {
        return;
    }

    pendingGroundParamRead = true;
    sendSysParamCommand(address, "0101", getGroundRegisterAddr(), QString(), "读取接地");
}

void EsdEditPanel::on_applyGroundButton_clicked()
{
    if (!checkConnection()) {
        return;
    }

    int address;
    if (!getSysParamAddress(address)) {
        return;
    }

    bool ok;
    double value = ui->groundValueLineEdit->text().toDouble(&ok);
    if (!ok) {
        QMessageBox::warning(this, "警告", "请输入有效的接地电压上限");
        return;
    }

    int rawValue = static_cast<int>(value * 100);
    if (rawValue < 0) {
        rawValue = 0;
    }
    QString hexValue = QString("%1").arg(rawValue, 4, 16, QChar('0')).toUpper();

    QString groundType = ui->groundTypeComboBox->currentText();
    if (groundType == "本机接地") {
        pendingSuccessMessage = "本机接地修改成功！";
    } else {
        pendingSuccessMessage = "静电接地修改成功！";
    }
    pendingGroundParamRead = false;

    sendSysParamCommand(address, "0001", getGroundRegisterAddr(), hexValue, "应用接地");
}

void EsdEditPanel::on_readTempButton_clicked()
{
    if (!checkConnection()) {
        return;
    }

    int address;
    if (!getSysParamAddress(address)) {
        return;
    }

    pendingTempParamRead = true;
    sendSysParamCommand(address, "0101", getTempRegisterAddr(), QString(), "读取温度上限");
}

void EsdEditPanel::on_applyTempButton_clicked()
{
    if (!checkConnection()) {
        return;
    }

    int address;
    if (!getSysParamAddress(address)) {
        return;
    }

    bool ok;
    double value = ui->tempValueLineEdit->text().toDouble(&ok);
    if (!ok) {
        QMessageBox::warning(this, "警告", "请输入有效的温度上限");
        return;
    }

    int rawValue = static_cast<int>(value * 100);
    if (rawValue < 0) {
        rawValue = 0;
    }
    QString hexValue = QString("%1").arg(rawValue, 4, 16, QChar('0')).toUpper();

    QString tempType = ui->tempTypeComboBox->currentText();
    if (tempType == "主机温度上限") {
        pendingSuccessMessage = "主机温度上限修改成功！";
    } else {
        pendingSuccessMessage = "从机温度上限修改成功！";
    }
    pendingTempParamRead = false;

    sendSysParamCommand(address, "0001", getTempRegisterAddr(), hexValue, "应用温度上限");
}

bool EsdEditPanel::getChannelParams(int &address, int &channel)
{
    bool addrOk = false;
    address = getSharedDeviceAddress(addrOk);
    if (!addrOk || address <= 0) {
        address = 1;
    }

    buildHexChannelField(ui->valueInputLineEdit->text(), &channel);
    return true;
}

int EsdEditPanel::getSharedDeviceAddress(bool &ok) const
{
    return ui->displayAddressLineEdit->text().trimmed().toInt(&ok);
}

void EsdEditPanel::clearAllChannelValues()
{
    for (int i = 0; i < 8; i++) {
        if (channelUpperLimitEdits[i]) {
            channelUpperLimitEdits[i]->clear();
        }
    }
}

QString EsdEditPanel::buildHexChannelField(const QString &channelText, int *outChannel)
{
    QString text = channelText.trimmed();
    if (text.contains('-')) {
        const QStringList parts = text.split('-', QString::SkipEmptyParts);
        if (parts.size() == 2) {
            bool ok1 = false;
            bool ok2 = false;
            int start = parts[0].trimmed().toInt(&ok1);
            int end = parts[1].trimmed().toInt(&ok2);
            if (ok1 && ok2 && start > 0 && end >= start) {
                int hexStart = start - 1;
                int hexEnd = end - hexStart;
                if (outChannel) {
                    *outChannel = start;
                }
                return QString("%1%2")
                    .arg(hexStart, 4, 16, QChar('0'))
                    .arg(hexEnd, 4, 16, QChar('0'))
                    .toUpper();
            }
        }
    }

    bool ok = false;
    int channel = text.toInt(&ok);
    if (!ok || channel <= 0) {
        channel = 1;
    }
    if (outChannel) {
        *outChannel = channel;
    }
    return QString("%1%2")
        .arg(channel - 1, 4, 16, QChar('0'))
        .arg(1, 4, 16, QChar('0'))
        .toUpper();
}

QString EsdEditPanel::buildChannelModbusCommand(const QString &type, const QString &funcCode,
                                              const QString &hexAddr, const QString &channelField,
                                              const QString &extraSuffix)
{
    if (type == "腕带") {
        if (extraSuffix.isEmpty()) {
            return hexAddr + funcCode + channelField + "0001";
        }
        return hexAddr + funcCode + channelField + extraSuffix;
    }

    if (extraSuffix.isEmpty()) {
        return hexAddr + funcCode + channelField;
    }
    return hexAddr + funcCode + channelField + extraSuffix;
}

void EsdEditPanel::getLimitFuncCodes(const QString &type, QString &readUpper, QString &readLower,
                                   QString &writeUpper, QString &writeLower)
{
    if (type == "腕带") {
        readUpper = "0111";
        readLower = "0112";
        writeUpper = "0011";
        writeLower = "0012";
    } else if (type == "台垫") {
        readUpper = "0114";
        readLower = "0115";
        writeUpper = "0014";
        writeLower = "0015";
    } else {
        readUpper = "0121";
        readLower = "0122";
        writeUpper = "0021";
        writeLower = "0022";
    }
}

QString EsdEditPanel::buildLimitReadCommand(const QString &type, const QString &funcCode, int address, int channel)
{
    Q_UNUSED(channel);
    QString hexAddr = QString("%1").arg(address, 4, 16, QChar('0')).toUpper();
    QString channelField = buildHexChannelField(ui->valueInputLineEdit->text());
    if (type == "腕带") {
        return buildChannelModbusCommand(type, funcCode, hexAddr, channelField, "0001");
    }
    return buildChannelModbusCommand(type, funcCode, hexAddr, channelField);
}

void EsdEditPanel::sendLimitReadCommand(const QString &funcCode, const QString &logTag)
{
    int address, channel;
    getChannelParams(address, channel);
    pendingLimitChannel = channel;
    parseChannelRange(pendingLimitRangeStart, pendingLimitRangeCount);
    if (pendingLimitRangeCount > 1) {
        clearAllChannelValues();
    }

    QString modbusStr = buildLimitReadCommand(ui->typeComboBox->currentText(), funcCode, address, channel);
    QByteArray sendData = hexStringToByteArray(modbusStr);
    quint16 crc = crc16(sendData);

    QByteArray crcBytes;
    crcBytes.append(crc & 0xFF);
    crcBytes.append((crc >> 8) & 0xFF);

    QByteArray finalData = sendData + crcBytes;
    QString finalHex = byteArrayToHexString(finalData);

    QString formattedHex;
    for (int i = 0; i < finalHex.length(); i += 4) {
        if (i > 0) formattedHex += " ";
        formattedHex += finalHex.mid(i, qMin(4, finalHex.length() - i));
    }
    Q_UNUSED(formattedHex);
    Q_UNUSED(logTag);

    sendFrame(finalData, funcCode);
}

void EsdEditPanel::sendLimitWriteCommand(const QString &funcCode, double limitValue,
                                       const QString &logTag, const QString &successMessage)
{
    int address, channel;
    getChannelParams(address, channel);

    QString hexAddr = QString("%1").arg(address, 4, 16, QChar('0')).toUpper();
    QString channelField = buildHexChannelField(ui->valueInputLineEdit->text());
    int rawValue = static_cast<int>(limitValue * 100);
    if (rawValue < 0) {
        rawValue = 0;
    }
    QString dataValue = QString("%1").arg(rawValue, 4, 16, QChar('0')).toUpper();

    QString modbusStr = buildChannelModbusCommand(ui->typeComboBox->currentText(), funcCode,
                                                  hexAddr, channelField, dataValue);
    QByteArray sendData = hexStringToByteArray(modbusStr);
    quint16 crc = crc16(sendData);

    QByteArray crcBytes;
    crcBytes.append(crc & 0xFF);
    crcBytes.append((crc >> 8) & 0xFF);

    QByteArray finalData = sendData + crcBytes;
    QString finalHex = byteArrayToHexString(finalData);

    QString formattedHex;
    for (int i = 0; i < finalHex.length(); i += 4) {
        if (i > 0) formattedHex += " ";
        formattedHex += finalHex.mid(i, qMin(4, finalHex.length() - i));
    }
    Q_UNUSED(formattedHex);
    Q_UNUSED(logTag);

    pendingSuccessMessage = successMessage;
    sendFrame(finalData, funcCode);
}

QString EsdEditPanel::formatLimitValue(const QString &dataHex)
{
    bool ok;
    uint16_t rawValue = dataHex.toUInt(&ok, 16);
    if (!ok) {
        return dataHex;
    }
    double value = rawValue / 100.0;
    if (qAbs(value - qRound(value)) < 0.0001) {
        return QString::number(static_cast<qlonglong>(qRound(value)));
    }
    return QString::number(value, 'f', 2);
}

void EsdEditPanel::on_readUpperLimitButton_clicked()
{
    if (!checkConnection()) {
        return;
    }

    QString readUpperFunc, readLowerFunc, writeUpperFunc, writeLowerFunc;
    getLimitFuncCodes(ui->typeComboBox->currentText(), readUpperFunc, readLowerFunc,
                      writeUpperFunc, writeLowerFunc);
    pendingLimitReadTarget = LimitReadUpper;
    sendLimitReadCommand(readUpperFunc, "读取上限");
}

void EsdEditPanel::on_readLowerLimitButton_clicked()
{
    if (!checkConnection()) {
        return;
    }

    QString readUpperFunc, readLowerFunc, writeUpperFunc, writeLowerFunc;
    getLimitFuncCodes(ui->typeComboBox->currentText(), readUpperFunc, readLowerFunc,
                      writeUpperFunc, writeLowerFunc);
    pendingLimitReadTarget = LimitReadLower;
    sendLimitReadCommand(readLowerFunc, "读取下限");
}

void EsdEditPanel::on_applyUpperLimitButton_clicked()
{
    if (!checkConnection()) {
        return;
    }

    bool ok;
    double limitValue = ui->upperLimitLineEdit->text().toDouble(&ok);
    if (!ok) {
        QMessageBox::warning(this, "警告", "请输入有效的上限值");
        return;
    }

    QString readUpperFunc, readLowerFunc, writeUpperFunc, writeLowerFunc;
    getLimitFuncCodes(ui->typeComboBox->currentText(), readUpperFunc, readLowerFunc,
                      writeUpperFunc, writeLowerFunc);
    sendLimitWriteCommand(writeUpperFunc, limitValue, "应用上限", "上限修改成功！");
}

void EsdEditPanel::on_applyLowerLimitButton_clicked()
{
    if (!checkConnection()) {
        return;
    }

    bool ok;
    double limitValue = ui->lowerLimitLineEdit->text().toDouble(&ok);
    if (!ok) {
        QMessageBox::warning(this, "警告", "请输入有效的下限值");
        return;
    }

    QString readUpperFunc, readLowerFunc, writeUpperFunc, writeLowerFunc;
    getLimitFuncCodes(ui->typeComboBox->currentText(), readUpperFunc, readLowerFunc,
                      writeUpperFunc, writeLowerFunc);
    sendLimitWriteCommand(writeLowerFunc, limitValue, "应用下限", "下限修改成功！");
}

void EsdEditPanel::on_readInternalResistanceButton_clicked()
{
    if (!checkConnection()) {
        return;
    }

    if (ui->typeComboBox->currentText() != "设备") {
        return;
    }

    pendingLimitReadTarget = LimitReadInternalResistance;
    sendLimitReadCommand("0122", "读取内阻");
}

void EsdEditPanel::on_applyInternalResistanceButton_clicked()
{
    if (!checkConnection()) {
        return;
    }

    if (ui->typeComboBox->currentText() != "设备") {
        return;
    }

    bool ok;
    double resistanceValue = ui->internalResistanceLineEdit->text().toDouble(&ok);
    if (!ok) {
        QMessageBox::warning(this, "警告", "请输入有效的内阻值");
        return;
    }

    sendLimitWriteCommand("0022", resistanceValue, "应用内阻", "内阻修改成功！");
}

bool EsdEditPanel::isLimitReadFunc(const QString &func) const
{
    return func == "0111" || func == "0112" || func == "0114"
        || func == "0115" || func == "0121" || func == "0122";
}

void EsdEditPanel::parseChannelRange(int &startChannel, int &channelCount)
{
    QString text = ui->valueInputLineEdit->text().trimmed();
    if (text.contains('-')) {
        const QStringList parts = text.split('-', QString::SkipEmptyParts);
        if (parts.size() == 2) {
            bool ok1 = false;
            bool ok2 = false;
            int start = parts[0].trimmed().toInt(&ok1);
            int end = parts[1].trimmed().toInt(&ok2);
            if (ok1 && ok2 && start > 0 && end >= start) {
                startChannel = start;
                channelCount = end - start + 1;
                return;
            }
        }
    }

    bool ok = false;
    int channel = text.toInt(&ok);
    if (!ok || channel <= 0) {
        channel = 1;
    }
    startChannel = channel;
    channelCount = 1;
}

void EsdEditPanel::parseLimitReadValues(const QByteArray &validFrame)
{
    if (validFrame.size() < 8) {
        return;
    }

    uint16_t regCount = (static_cast<unsigned char>(validFrame[4]) << 8)
                        | static_cast<unsigned char>(validFrame[5]);
    const int dataStart = 6;
    const int availableRegs = (validFrame.size() - 2 - dataStart) / 2;
    const int count = qMin(static_cast<int>(regCount), availableRegs);

    QString firstValue;
    for (int i = 0; i < count; i++) {
        const int byteIndex = dataStart + i * 2;
        QString dataHex = QString("%1%2")
            .arg(static_cast<quint8>(validFrame[byteIndex]), 2, 16, QChar('0'))
            .arg(static_cast<quint8>(validFrame[byteIndex + 1]), 2, 16, QChar('0'))
            .toUpper();
        QString value = formatLimitValue(dataHex);
        if (i == 0) {
            firstValue = value;
        }

        const int channelNo = pendingLimitRangeStart + i;
        if (channelNo >= 1 && channelNo <= 8 && channelUpperLimitEdits[channelNo - 1]) {
            channelUpperLimitEdits[channelNo - 1]->setText(value);
        }
    }

    if (!firstValue.isEmpty()) {
        if (pendingLimitReadTarget == LimitReadUpper) {
            ui->upperLimitLineEdit->setText(firstValue);
        } else if (pendingLimitReadTarget == LimitReadLower) {
            ui->lowerLimitLineEdit->setText(firstValue);
        } else if (pendingLimitReadTarget == LimitReadInternalResistance) {
            ui->internalResistanceLineEdit->setText(firstValue);
        }
    }
}

void EsdEditPanel::initTimeModifyFields()
{
    const QDateTime now = QDateTime::currentDateTime();
    fillTimeModifyFields(now.date().year(),
                         now.date().month(),
                         now.date().day(),
                         now.date().dayOfWeek(),
                         now.time().hour(),
                         now.time().minute(),
                         now.time().second());
}

void EsdEditPanel::fillTimeModifyFields(int year, int month, int day, int weekday,
                                         int hour, int minute, int second)
{
    ui->timeYearLineEdit->setText(QString::number(year));
    ui->timeMonthLineEdit->setText(QString::number(month));
    ui->timeDayLineEdit->setText(QString::number(day));
    ui->timeWeekdayLineEdit->setText(QString::number(weekday));
    ui->timeHourLineEdit->setText(QString::number(hour));
    ui->timeMinuteLineEdit->setText(QString::number(minute));
    ui->timeSecondLineEdit->setText(QString::number(second));
}

QString EsdEditPanel::encodeTimeRegisterValue(int value) const
{
    if (value < 0 || value > 0xFFFF) {
        return QStringLiteral("0000");
    }
    return QString("%1").arg(value, 4, 16, QChar('0')).toUpper();
}

QString EsdEditPanel::formatTimeModifyText(int year, int month, int day, int weekday,
                                          int hour, int minute, int second) const
{
    return QStringLiteral("%1年%2月%3日 星期%4 %5:%6:%7")
        .arg(year)
        .arg(month)
        .arg(day)
        .arg(weekday)
        .arg(hour, 2, 10, QChar('0'))
        .arg(minute, 2, 10, QChar('0'))
        .arg(second, 2, 10, QChar('0'));
}

bool EsdEditPanel::parseTimeModifyInputs(int &year, int &month, int &day, int &weekday,
                                          int &hour, int &minute, int &second)
{
    bool yearOk = false;
    bool monthOk = false;
    bool dayOk = false;
    bool weekdayOk = false;
    bool hourOk = false;
    bool minuteOk = false;
    bool secondOk = false;

    year = ui->timeYearLineEdit->text().trimmed().toInt(&yearOk);
    month = ui->timeMonthLineEdit->text().trimmed().toInt(&monthOk);
    day = ui->timeDayLineEdit->text().trimmed().toInt(&dayOk);
    weekday = ui->timeWeekdayLineEdit->text().trimmed().toInt(&weekdayOk);
    hour = ui->timeHourLineEdit->text().trimmed().toInt(&hourOk);
    minute = ui->timeMinuteLineEdit->text().trimmed().toInt(&minuteOk);
    second = ui->timeSecondLineEdit->text().trimmed().toInt(&secondOk);

    if (!yearOk || !monthOk || !dayOk || !weekdayOk || !hourOk || !minuteOk || !secondOk) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请完整填写年、月、日、星期、时、分、秒"));
        return false;
    }
    if (year < 2000 || year > 2099) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("年份应在 2000-2099 之间"));
        return false;
    }
    if (month < 1 || month > 12) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("月份应在 1-12 之间"));
        return false;
    }
    if (day < 1 || day > 31) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("日期应在 1-31 之间"));
        return false;
    }
    if (weekday < 1 || weekday > 7) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("星期应在 1-7 之间"));
        return false;
    }
    if (hour < 0 || hour > 23) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("小时应在 0-23 之间"));
        return false;
    }
    if (minute < 0 || minute > 59) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("分钟应在 0-59 之间"));
        return false;
    }
    if (second < 0 || second > 59) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("秒应在 0-59 之间"));
        return false;
    }
    return true;
}

void EsdEditPanel::notifyTimeModifyResult(bool success)
{
    if (m_timeModifyTimer) {
        m_timeModifyTimer->stop();
    }

    const QString timeText = pendingTimeModifyText;
    pendingTimeModifyText.clear();

    if (timeText.isEmpty()) {
        return;
    }

    if (success) {
        showAppInformation(this,
                           QStringLiteral("操作成功"),
                           QStringLiteral("已修改时间为：") + timeText);
    } else {
        showAppWarning(this,
                       QStringLiteral("操作失败"),
                       QStringLiteral("未修改成功：") + timeText);
    }
}

void EsdEditPanel::on_applyTimeButton_clicked()
{
    int year = 0;
    int month = 0;
    int day = 0;
    int weekday = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (!parseTimeModifyInputs(year, month, day, weekday, hour, minute, second)) {
        return;
    }

    sendTimeModifyCommand(year, month, day, weekday, hour, minute, second);
}

void EsdEditPanel::on_syncPcTimeButton_clicked()
{
    const QDateTime now = QDateTime::currentDateTime();
    const int year = now.date().year();
    const int month = now.date().month();
    const int day = now.date().day();
    const int weekday = now.date().dayOfWeek();
    const int hour = now.time().hour();
    const int minute = now.time().minute();
    const int second = now.time().second();

    fillTimeModifyFields(year, month, day, weekday, hour, minute, second);
    sendTimeModifyCommand(year, month, day, weekday, hour, minute, second);
}

void EsdEditPanel::sendTimeModifyCommand(int year, int month, int day, int weekday,
                                          int hour, int minute, int second)
{
    if (!checkConnection()) {
        return;
    }

    bool addrOk = false;
    const int address = getSharedDeviceAddress(addrOk);
    if (!addrOk || address <= 0) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请先在查询地址中填写有效地址"));
        return;
    }

    pendingTimeModifyText = formatTimeModifyText(year, month, day, weekday, hour, minute, second);
    pendingSuccessMessage.clear();

    const QString hexAddr = QString("%1").arg(address, 4, 16, QChar('0')).toUpper();
    const QString modbusStr = hexAddr
        + QStringLiteral("004000000007")
        + encodeTimeRegisterValue(year)
        + encodeTimeRegisterValue(month)
        + encodeTimeRegisterValue(day)
        + encodeTimeRegisterValue(weekday)
        + encodeTimeRegisterValue(hour)
        + encodeTimeRegisterValue(minute)
        + encodeTimeRegisterValue(second);

    QByteArray sendData = hexStringToByteArray(modbusStr);
    const quint16 crc = crc16(sendData);

    QByteArray crcBytes;
    crcBytes.append(crc & 0xFF);
    crcBytes.append((crc >> 8) & 0xFF);

    const QByteArray finalData = sendData + crcBytes;
    if (m_timeModifyTimer) {
        m_timeModifyTimer->start(2000);
    }
    sendFrame(finalData, QStringLiteral("0040"));
}