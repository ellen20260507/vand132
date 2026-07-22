#include "serialworker.h"
#include "dbmanager.h"
#include "pollconfig.h"
#include <QDateTime>
#include <QDebug>
#include <QMessageBox>
#include <QCoreApplication>
#include <QPluginLoader>
#include <QFileInfo>

SerialWorker::SerialWorker(const QString& workerName, TaskType taskType, QObject *parent)
    : QObject(parent), m_workerName(workerName), m_taskType(taskType), m_connectionType(ConnectionType::SERIAL)
{
    serial = new QSerialPort(this);
    tcpServer = nullptr;
    tcpSocket = nullptr;
    sendTimer = new QTimer(this);
    overtime = new QTimer(this);
    isPollingActive = false;
    isSingleTest = false;
    maxResendCount = 3;
    sendnum = 0;
    delayMs = 50;
    m_tcpServerPort = 0;
    isIonizerDevice = false;

    // 修正信号连接：显式指定 SerialWorker::onTimeout
    connect(serial, &QSerialPort::errorOccurred, this, &SerialWorker::onSerialError);
    connect(serial, &QSerialPort::readyRead, this, &SerialWorker::recv);
    connect(sendTimer, &QTimer::timeout, this, &SerialWorker::timerTriggerSend);
    connect(overtime, &QTimer::timeout, this, &SerialWorker::onTimeout);  // 修正此处！
    
    // 初始化统计数据
    resetPollingStats();
}

SerialWorker::~SerialWorker()
{
    closeSerial();

    delete serial;
    if (tcpServer) {
        delete tcpServer;
    }
    if (tcpSocket) {
        delete tcpSocket;
    }
    delete sendTimer;
    delete overtime;
}



void SerialWorker::openSerial(const QString& portName, int baudRate, int sendIntervalMs, int overtimeIntervalMs, int maxResendCount, int delayMs)
{
    // 1. 关闭已打开的串口
    if (serial->isOpen()) {
        serial->close();
        emit logGenerated(m_workerName, getLogWithTime("已关闭原有串口连接"));
    }

    // 2. 先设置串口参数（必须在open之前）
    serial->setPortName(portName); // 设置要打开的COM口（比如COM1）
    serial->setBaudRate(baudRate);
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);
    serial->setFlowControl(QSerialPort::NoFlowControl);

    // 3. 再尝试打开串口
    if (serial->open(QIODevice::ReadWrite)) {
        // 打开成功后，初始化其他参数
        this->maxResendCount = maxResendCount;
        this->delayMs = delayMs;
        sendTimer->setInterval(sendIntervalMs);
        overtime->setInterval(overtimeIntervalMs);
        m_currentPortName = portName;
        m_connectionType = ConnectionType::SERIAL;
        emit logGenerated(m_workerName, getLogWithTime(QString("串口打开成功：%1 %2").arg(portName).arg(baudRate)));
    } else {
        // 打开失败
        QString errMsg = QString("串口打开失败：%1").arg(serial->errorString());
        emit logGenerated(m_workerName, getLogWithTime(errMsg));
        emit dataReceived(m_workerName, "错误：" + errMsg);
        emit serialOpenFailed(m_workerName, errMsg);
    }
}

void SerialWorker::openTcpServer(const QString& ipAddress, int port, int sendIntervalMs, int overtimeIntervalMs, int maxResendCount, int delayMs)
{
    // 1. 关闭已打开的连接
    closeSerial();

    // 2. 创建TCP Server
    tcpServer = new QTcpServer(this);
    m_tcpServerIp = ipAddress;
    m_tcpServerPort = port;

    // 3. 连接信号槽
    connect(tcpServer, &QTcpServer::newConnection, this, &SerialWorker::onNewConnection);
    connect(tcpServer, &QTcpServer::acceptError, this, &SerialWorker::onTcpServerError);

    // 4. 尝试监听
    if (tcpServer->listen(QHostAddress(ipAddress), port)) {
        // 监听成功后，初始化其他参数
        this->maxResendCount = maxResendCount;
        this->delayMs = delayMs;
        sendTimer->setInterval(sendIntervalMs);
        overtime->setInterval(overtimeIntervalMs);
        m_connectionType = ConnectionType::TCP_SERVER;
        emit logGenerated(m_workerName, getLogWithTime(QString("TCP Server启动成功：%1:%2").arg(ipAddress).arg(port)));
    } else {
        // 监听失败
        QString errMsg = QString("TCP Server启动失败：%1").arg(tcpServer->errorString());
        emit logGenerated(m_workerName, getLogWithTime(errMsg));
        emit dataReceived(m_workerName, "错误：" + errMsg);
        emit serialOpenFailed(m_workerName, errMsg);
        delete tcpServer;
        tcpServer = nullptr;
    }
}

// 以下是其他原有函数（recv、sendNextData、onTimeout 等），保持不变，仅将静态成员访问改为非静态（去掉 SerialWorker::）
void SerialWorker::startPolling()
{
    if (m_connectionType == ConnectionType::SERIAL) {
        if (!serial->isOpen()) {
            emit logGenerated(m_workerName, getLogWithTime("轮询启动失败：串口未打开"));
            return;
        }
    } else if (m_connectionType == ConnectionType::TCP_SERVER) {
        if (!tcpServer || !tcpServer->isListening()) {
            emit logGenerated(m_workerName, getLogWithTime("轮询启动失败：TCP Server未启动"));
            return;
        }
    }

    isPollingActive = true;
    sendTimer->start();
    emit logGenerated(m_workerName, getLogWithTime("轮询启动成功"));

    QMutexLocker locker(&queueMutex);
    sendQueue.clear();
    sendQueueDeviceIds.clear();
    sendnum = 0;
    currentIonizerId.clear();
    locker.unlock();
}

void SerialWorker::stopPolling()
{
    isPollingActive = false;
    sendTimer->stop();
    overtime->stop();
    QMutexLocker locker(&queueMutex);
    sendQueue.clear();
    sendQueueDeviceIds.clear();
    sendnum = 0;
    currentIonizerId.clear();
    locker.unlock();
    emit logGenerated(m_workerName, getLogWithTime("轮询停止成功"));
}

void SerialWorker::setConfigData(const QVector<QStringList>& configData)
{
    newarrange = configData;
    processModbusData(configData);
}

void SerialWorker::closeSerial()
{
    stopPolling();

    if (m_connectionType == ConnectionType::SERIAL) {
        if (serial->isOpen()) {
            serial->close();
            emit logGenerated(m_workerName, getLogWithTime("串口已关闭"));
        }
    } else if (m_connectionType == ConnectionType::TCP_SERVER) {
        if (tcpServer) {
            if (tcpServer->isListening()) {
                tcpServer->close();
                emit logGenerated(m_workerName, getLogWithTime(QString("TCP Server已关闭：%1:%2").arg(m_tcpServerIp).arg(m_tcpServerPort)));
            }
            delete tcpServer;
            tcpServer = nullptr;
        }
        if (tcpSocket) {
            if (tcpSocket->isOpen()) {
                tcpSocket->close();
                emit logGenerated(m_workerName, getLogWithTime("TCP Socket已关闭"));
            }
            delete tcpSocket;
            tcpSocket = nullptr;
        }
    }

    m_connectionType = ConnectionType::SERIAL;
    m_currentPortName.clear();
    m_tcpServerIp.clear();
    m_tcpServerPort = 0;
}

void SerialWorker::recv()
{
    QByteArray newData = serial->readAll();
    if (newData.isEmpty()) return;
    recvBuffer.append(newData);

    if (recvBuffer.size() > 256) {
        recvBuffer = recvBuffer.mid(recvBuffer.size() - 256);
        emit logGenerated(m_workerName, getLogWithTime("警告：缓冲区超256字节，保留最新256字节"));
        return;
    }

    if (m_toolMode) {
        processToolReceivedData();
        return;
    }

    // 离子风机处理：只要是离子风机设备且有返回数据，就认为当前发送的离子风机在线
    if (isIonizerDevice && !currentIonizerId.isEmpty() && !recvBuffer.isEmpty()) {

        // 显示接收数据
        emit dataReceived(m_workerName, QString("接收数据：%1").arg(QString::fromLatin1(recvBuffer.toHex().toUpper())));

        // 使用当前发送的离子风机设备ID
        QString devId = currentIonizerId;

        emit dataReceived(m_workerName, QString("✅ 离子风机 %1 在线").arg(devId));
        emit ionizerStatusChanged(devId, true);
        emit channelReadingReady(devId, QDateTime::currentDateTime(), 0.0,
                                 QString::fromLatin1(recvBuffer.toHex().toUpper()),
                                 QStringLiteral("ONLINE"), QStringLiteral("在线"));

        // 将离子风机数据添加到addressFuncData（使用和其他设备相同的格式）
        QString dataHex = recvBuffer.toHex().toUpper();
        addressFuncData["IONIZER" + devId] = QStringList() << dataHex;

        // 立即发射信号，让数据传递给MainWindow
        emit parsedDataReady(addressFuncData);
        qDebug() << "[SerialWorker-" << m_workerName << "] 发送解析数据给MainWindow：" << addressFuncData.keys();
        addressFuncData.clear();

        // 清空缓冲区，准备下一次发送
        recvBuffer.clear();
        currentExpectedAddrFunc.clear();
        currentIonizerId.clear();
        isIonizerDevice = false;
        sendnum = 0;

        // 停止超时定时器
        if (overtime->isActive()) {
            overtime->stop();
        }

        QTimer::singleShot(delayMs, [this]() {
            sendNextData();
        });
        return;
    }

    if (currentExpectedAddrFunc.isEmpty()) {
        // 添加调试日志，查看接收数据的情况
        emit logGenerated(m_workerName, getLogWithTime(QString("接收数据，但currentExpectedAddrFunc为空，无法解析：%1").arg(QString::fromLatin1(recvBuffer.toHex().toUpper()))));
        // 清空缓冲区，准备下一次接收
        recvBuffer.clear();
        return;
    }

    int expectedStart = -1;
    for (int i = 0; i <= recvBuffer.size() - 4; i++) {
        QString addr = QString(recvBuffer.mid(i,2).toHex().toUpper()).rightJustified(4,'0');
        QString func = QString(recvBuffer.mid(i+2,2).toHex().toUpper()).rightJustified(4,'0');
        if (addr + func == currentExpectedAddrFunc) {
            expectedStart = i;
            break;
        }
    }

    if (expectedStart == -1) {
        return;
    }

    QByteArray frameHeader = recvBuffer.mid(expectedStart, 4);
    QString funcCode = QString(frameHeader.mid(2, 2).toHex().toUpper());

    int expectedFrameLen = -1;
    if (recvBuffer.size() >= expectedStart + 6) {
        QByteArray regCountBytes = recvBuffer.mid(expectedStart + 4, 2);
        uint16_t regCount = (static_cast<unsigned char>(regCountBytes[0]) << 8) | static_cast<unsigned char>(regCountBytes[1]);
        expectedFrameLen = 2 + 2 + 2 + (regCount * 2) + 2;
    }

    if (expectedFrameLen == -1) {
        return;
    }

    int currentFrameAvailableLen = recvBuffer.size() - expectedStart;
    if (currentFrameAvailableLen < expectedFrameLen) {
        return;
    }

    QByteArray validFrame = recvBuffer.mid(expectedStart, expectedFrameLen);
    QByteArray dataToCrc = validFrame.left(expectedFrameLen - 2);
    uint16_t calcCrc = calcrc(dataToCrc);
    uint16_t recvCrc = (static_cast<unsigned char>(validFrame[expectedFrameLen-1]) << 8) | static_cast<unsigned char>(validFrame[expectedFrameLen-2]);

    if (calcCrc != recvCrc) {
        QString errInfo = QString("CRC校验失败：计算值=0x%1 | 接收值=0x%2")
                          .arg(QString::number(calcCrc,16).toUpper(), 4, '0')
                          .arg(QString::number(recvCrc,16).toUpper(), 4, '0');
        emit logGenerated(m_workerName, getLogWithTime("错误：" + errInfo));
        emit dataReceived(m_workerName, errInfo);
        recvBuffer = recvBuffer.mid(expectedStart + 1);
        return;
    }

    QString addr = QString(validFrame.mid(0, 2).toHex().toUpper()).rightJustified(4, '0');
    QString frameHex = validFrame.toHex().toUpper();
    QString crcHex = QString::number(recvCrc, 16).toUpper().rightJustified(4, '0');

    // 显示接收数据
    emit dataReceived(m_workerName, QString("接收数据：%1").arg(frameHex));

    parsingdata(validFrame);
    recvBuffer = recvBuffer.mid(expectedStart + expectedFrameLen);
    currentExpectedAddrFunc.clear();
    currentIonizerId.clear();
    sendnum = 0;

    QTimer::singleShot(delayMs, [this]() {
        recvBuffer.clear();
        sendNextData();
    });
}

void SerialWorker::sendNextData()
{
    QMutexLocker sendLocker(&sendMutex); // 新增：防止并发调用该函数

    bool isConnectionOpen = false;
    if (m_connectionType == ConnectionType::SERIAL) {
        isConnectionOpen = serial->isOpen();
    } else if (m_connectionType == ConnectionType::TCP_SERVER) {
        isConnectionOpen = tcpSocket && tcpSocket->isOpen();
    }

    if (!isConnectionOpen || !isPollingActive) {
        overtime->stop();
        if (!isConnectionOpen) {
            if (m_connectionType == ConnectionType::SERIAL) {
                emit logGenerated(m_workerName, getLogWithTime("错误：串口已关闭，停止发送"));
                emit dataReceived(m_workerName, "警告：串口已断开连接，发送已停止！");
                QMutexLocker locker(&queueMutex);
                sendQueue.clear();
                sendQueueDeviceIds.clear();
                sendnum = 0;
                lastSentData.clear();
                currentIonizerId.clear();
            } else if (!isPollingActive) {
                emit logGenerated(m_workerName, getLogWithTime("发送暂停：轮询未激活"));
            }
            // TCP 等待客户端重连时不清理队列，下一轮 timerTriggerSend 会重建
        } else {
            emit logGenerated(m_workerName, getLogWithTime("发送暂停：轮询未激活"));
        }
        return;
    }

    QByteArray currentData;
    QString currentDevId;
    {
        QMutexLocker locker(&queueMutex);
        if (sendQueue.isEmpty()) {
            locker.unlock();
            lastSentData.clear();
            currentIonizerId.clear();
            return;
        }
        currentData = sendQueue.head();
        if (!sendQueueDeviceIds.isEmpty()) {
            currentDevId = sendQueueDeviceIds.head();
        }
    }

    if (currentData.size() >= 4) {
        QString addr = QString(currentData.mid(0,2).toHex().toUpper()).rightJustified(4,'0');
        QString func = QString(currentData.mid(2,2).toHex().toUpper()).rightJustified(4,'0');
        currentExpectedAddrFunc = addr + func;

        // 检查是否是离子风机数据（FF FF 信道 AA）
        isIonizerDevice = (currentData.size() >= 4 &&
                         static_cast<uint8_t>(currentData[0]) == 0xFF &&
                         static_cast<uint8_t>(currentData[1]) == 0xFF &&
                         static_cast<uint8_t>(currentData[3]) == 0xAA);

        // 如果是离子风机，记录当前设备ID
        if (isIonizerDevice) {
            currentIonizerId = currentDevId;
        } else {
            currentIonizerId.clear();
        }
    } else {
        emit logGenerated(m_workerName, getLogWithTime("警告：发送数据长度不足4字节，跳过"));
        QMutexLocker locker(&queueMutex);
        sendQueue.dequeue();
        if (!sendQueueDeviceIds.isEmpty()) {
            sendQueueDeviceIds.dequeue();
        }
        locker.unlock();
        sendnum = 0;
        lastSentData.clear();
        // 使用QTimer::singleShot避免递归调用，防止死锁
        QTimer::singleShot(0, this, &SerialWorker::sendNextData);
        return;
    }

    bool isNewData = (lastSentData != currentData);
    if (isNewData) {
        lastSentData = currentData;
    }

    // 执行发送
    qint64 bytesWritten = -1;

    if (m_connectionType == ConnectionType::SERIAL) {
        bytesWritten = serial->write(currentData);
        if (bytesWritten != -1) {
            serial->flush();
        }
    } else if (m_connectionType == ConnectionType::TCP_SERVER && tcpSocket) {
        bytesWritten = tcpSocket->write(currentData);
        if (bytesWritten != -1) {
            tcpSocket->flush();
        }
    }

    if (bytesWritten == -1) {
        QString errorMsg = "";
        if (m_connectionType == ConnectionType::SERIAL) {
            errorMsg = serial->errorString();
        } else if (m_connectionType == ConnectionType::TCP_SERVER && tcpSocket) {
            errorMsg = tcpSocket->errorString();
        } else {
            errorMsg = "未知错误";
        }

        emit logGenerated(m_workerName, getLogWithTime(
            QString("错误：发送失败 | 原因：%1").arg(errorMsg)));
        emit dataReceived(m_workerName, "失败：数据发送失败！");
        QMutexLocker locker(&queueMutex);
        if (!sendQueue.isEmpty()) {
            sendQueue.dequeue();
        }
        if (!sendQueueDeviceIds.isEmpty()) {
            sendQueueDeviceIds.dequeue();
        }
        locker.unlock();
        sendnum = 0;
        lastSentData.clear();
        // 使用QTimer::singleShot避免递归调用，防止死锁
        QTimer::singleShot(0, this, &SerialWorker::sendNextData);
        return;
    }

    // 显示发送数据
    emit dataReceived(m_workerName, QString("发送数据：%1").arg(QString::fromLatin1(currentData.toHex().toUpper())));

    {
        QMutexLocker locker(&queueMutex);
        if (!sendQueue.isEmpty()) {
            sendQueue.dequeue();
        }
        if (!sendQueueDeviceIds.isEmpty()) {
            sendQueueDeviceIds.dequeue();
        }
        sendnum++;
    }

    overtime->start();

    bool hasMoreData = false;
    {
        QMutexLocker locker(&queueMutex);
        hasMoreData = !sendQueue.isEmpty();
    }

    if (!hasMoreData) {
        emit logGenerated(m_workerName, getLogWithTime("提示：队列已空，当前轮询发送完成"));
        sendnum = 0;
        lastSentData.clear();
        // 异步计算并插入合格率数据，避免阻塞UI线程
        QTimer::singleShot(0, this, &SerialWorker::calculateAndInsertQualifiedRate);
        emit pollingCycleFinished();  // 发射一轮轮询完成信号
    }
}

void SerialWorker::onTimeout()
{
    bool isConnectionOpen = false;
    if (m_connectionType == ConnectionType::SERIAL) {
        isConnectionOpen = serial->isOpen();
    } else if (m_connectionType == ConnectionType::TCP_SERVER) {
        isConnectionOpen = (tcpSocket != nullptr);
    }

    if (!isConnectionOpen) {
        sendnum = 0;
        currentExpectedAddrFunc.clear();
        lastSentData.clear();
        if (m_toolMode) {
            finishToolCommand(false, QByteArray(), QStringLiteral("连接已断开"));
        }
        return;
    }

    if (m_toolMode) {
        if (m_toolSendnum < maxResendCount && !lastSentData.isEmpty()) {
            m_toolSendnum++;
            if (m_connectionType == ConnectionType::SERIAL) {
                serial->write(lastSentData);
            } else if (tcpSocket) {
                tcpSocket->write(lastSentData);
            }
            return;
        }
        finishToolCommand(false, QByteArray(), QStringLiteral("响应超时"));
        return;
    }

    // 离子风机超时处理：标记为待机
    if (isIonizerDevice) {
        QString devId = currentIonizerId;

        if (!devId.isEmpty()) {
            emit dataReceived(m_workerName, QString("⚪ 离子风机 %1 待机（无回复）").arg(devId));
            emit ionizerStatusChanged(devId, false);  // 发射待机状态信号
            emit channelReadingReady(devId, QDateTime::currentDateTime(), 0.0, QString(),
                                     QStringLiteral("OFFLINE"), QStringLiteral("待机（无回复）"));

            // 将离子风机离线状态添加到addressFuncData（使用特殊标记）
            addressFuncData["IONIZER" + devId] = QStringList() << "OFFLINE";

            // 立即发射信号，让数据传递给MainWindow
            emit parsedDataReady(addressFuncData);
            qDebug() << "[SerialWorker-" << m_workerName << "] 发送离子风机离线状态给MainWindow：" << addressFuncData.keys();
            addressFuncData.clear();
        }

        isIonizerDevice = false;
    }

    if (sendnum < maxResendCount) {
        QTimer::singleShot(delayMs, [this]() {
            recvBuffer.clear();
            sendNextData();
        });
    } else {
        QMutexLocker locker(&queueMutex);
        if (!sendQueue.isEmpty()) {
            sendQueue.dequeue();
        }
        if (!sendQueueDeviceIds.isEmpty()) {
            sendQueueDeviceIds.dequeue();
        }
        locker.unlock();

        sendnum = 0;
        currentExpectedAddrFunc.clear();
        lastSentData.clear();
        currentIonizerId.clear();

        if (isSingleTest) {
            bool queueEmpty = false;
            {
                QMutexLocker locker3(&queueMutex);
                queueEmpty = sendQueue.isEmpty();
            }
            if (queueEmpty) {
                isSingleTest = false;
                if (isPollingActive) {
                    sendTimer->start();
                    emit logGenerated(m_workerName, getLogWithTime("恢复轮询"));
                }
                return;
            }
        }

        QTimer::singleShot(delayMs, [this]() {
            recvBuffer.clear();
            sendNextData();
        });
    }
}

void SerialWorker::timerTriggerSend()
{
    if (!isPollingActive) {
        sendTimer->stop();
        return;
    }

    bool isConnectionOpen = false;
    if (m_connectionType == ConnectionType::SERIAL) {
        isConnectionOpen = serial->isOpen();
    } else if (m_connectionType == ConnectionType::TCP_SERVER) {
        isConnectionOpen = tcpSocket && tcpSocket->isOpen();
    }

    if (!isConnectionOpen) {
        if (m_connectionType == ConnectionType::TCP_SERVER) {
            // TCP Server 已监听，等待客户端连接，保持定时器运行
            return;
        }
        sendTimer->stop();
        emit logGenerated(m_workerName, getLogWithTime(QStringLiteral("定时发送暂停：串口未打开")));
        return;
    }

    if (newarrange.isEmpty()) {
        emit logGenerated(m_workerName, getLogWithTime("定时发送：无配置数据，等待下一轮"));
        return;
    }

    // 重置轮询统计数据
    resetPollingStats();

    // 统计各类型设备总数
    int totalW = 0, totalT = 0, totalE = 0;
    for (const QStringList& rowData : newarrange) {
        const QVector<PollRowInfo> entries = parsePollConfigEntries(rowData);
        for (const PollRowInfo& info : entries) {
            if (m_taskType == TaskType::C_TYPE && info.typePrefix != "C") continue;
            if (m_taskType == TaskType::WTE_TYPE && info.typePrefix == "C") continue;

            if (info.typePrefix == "W") totalW += info.range.registerCount;
            else if (info.typePrefix == "T") totalT += info.range.registerCount;
            else if (info.typePrefix == "E") totalE += info.range.registerCount;
        }
    }

    // 更新设备类型总数
    deviceTypeCounts["W"] = totalW;
    deviceTypeCounts["T"] = totalT;
    deviceTypeCounts["E"] = totalE;

    QQueue<QByteArray> newSendQueue;
    QQueue<QString> newSendQueueDeviceIds;
    QSet<QString> newProcessedIds;

    for (const QStringList& rowData : newarrange) {
        const QVector<PollRowInfo> entries = parsePollConfigEntries(rowData);
        for (const PollRowInfo& info : entries) {
            if (m_taskType == TaskType::C_TYPE && info.typePrefix != "C") continue;
            if (m_taskType == TaskType::WTE_TYPE && info.typePrefix == "C") continue;

            const QString hexAddr = QString("%1").arg(info.modbusAddr, 4, 16, QChar('0')).toUpper();

            if (info.typePrefix == "W" || info.typePrefix == "T" || info.typePrefix == "E") {
            const QString funcCode = info.typePrefix == "W" ? "1011"
                                   : info.typePrefix == "T" ? "1014" : "1021";
            const QString hexA = QString("%1").arg(info.range.startRegister, 4, 16, QChar('0')).toUpper();
            const QString hexB = QString("%1").arg(info.range.registerCount, 4, 16, QChar('0')).toUpper();
            const QString modbusStr = hexAddr + funcCode + hexA + hexB;
            QByteArray sendData = encodeModbusHexPayload(modbusStr);
            if (!sendData.isEmpty()) {
                const uint16_t crc = calcrc(sendData);
                sendData.append(static_cast<char>(crc & 0xFF));
                sendData.append(static_cast<char>((crc >> 8) & 0xFF));
                newSendQueue.enqueue(sendData);
                newSendQueueDeviceIds.enqueue(makePollDeviceId(info.typePrefix, info.modbusAddr, info.range.startChannel));
            }
            for (int ch = info.range.startChannel; ch <= info.range.endChannel; ++ch) {
                newProcessedIds.insert(makePollDeviceId(info.typePrefix, info.modbusAddr, ch));
            }
        } else if (info.typePrefix == "C") {
            const QString modbusStr = hexAddr + "1030" + "0002" + "0016";
            QByteArray sendData = encodeModbusHexPayload(modbusStr);
            if (!sendData.isEmpty()) {
                const uint16_t crc = calcrc(sendData);
                sendData.append(static_cast<char>(crc & 0xFF));
                sendData.append(static_cast<char>((crc >> 8) & 0xFF));
                newSendQueue.enqueue(sendData);
                newSendQueueDeviceIds.enqueue(makePollDeviceId("C", info.modbusAddr));
                newProcessedIds.insert(makePollDeviceId("C", info.modbusAddr));
            }
        } else if (info.typePrefix == "I" && (m_taskType == TaskType::WTE_TYPE || m_taskType == TaskType::ALL_TYPE)) {
            const uint8_t channel = static_cast<uint8_t>(info.range.startChannel);
            QByteArray sendData;
            sendData.append(static_cast<char>(0xFF));
            sendData.append(static_cast<char>(0xFF));
            sendData.append(static_cast<char>(channel));
            sendData.append(static_cast<char>(0xAA));
            sendData.append(static_cast<char>(0x0D));
            sendData.append(static_cast<char>(0x97));
            sendData.append(static_cast<char>(0x05));
            sendData.append(static_cast<char>(info.modbusAddr & 0xFF));
            sendData.append(static_cast<char>((info.modbusAddr >> 8) & 0xFF));
            sendData.append(static_cast<char>(0xFF));
            sendData.append(static_cast<char>(0xFF));
            sendData.append(static_cast<char>(0xA2));
            sendData.append(static_cast<char>(0x00));

            uint16_t checksum = 0;
            for (int i = 4; i < sendData.size(); ++i) {
                checksum += static_cast<uint8_t>(sendData[i]);
            }
            checksum = (256 - (checksum % 256)) & 0xFF;
            sendData.append(static_cast<char>(checksum));

            const QString ionizerId = makePollDeviceId("I", info.modbusAddr, info.range.startChannel);
            newSendQueue.enqueue(sendData);
            newSendQueueDeviceIds.enqueue(ionizerId);
            newProcessedIds.insert(ionizerId);
        }
        }
    }

    QMutexLocker locker(&queueMutex);
    sendQueue = newSendQueue;
    sendQueueDeviceIds = newSendQueueDeviceIds;
    processedIds.unite(newProcessedIds);
    sendnum = 0;
    bool startSend = !sendQueue.isEmpty() && !overtime->isActive();

    // 添加调试日志，查看每个线程生成的sendQueue大小和TaskType
    QString taskTypeStr;
    if (m_taskType == TaskType::C_TYPE) taskTypeStr = "C_TYPE";
    else if (m_taskType == TaskType::WTE_TYPE) taskTypeStr = "WTE_TYPE";
    else taskTypeStr = "ALL_TYPE";
    emit logGenerated(m_workerName, getLogWithTime(QString("定时发送：生成新队列，TaskType=%1，队列大小=%2，设备ID列表=%3").arg(taskTypeStr).arg(sendQueue.size()).arg(sendQueueDeviceIds.join(","))));

    locker.unlock();

    if (startSend) {
        emit logGenerated(m_workerName, getLogWithTime("开始发送数据队列..."));
        sendNextData();
    }
}

void SerialWorker::processModbusData(const QVector<QStringList>& tableData)
{
    newarrange = tableData;
    configuredIds.clear();

    for (const QStringList& row : tableData) {
        const QVector<PollRowInfo> entries = parsePollConfigEntries(row);
        for (const PollRowInfo& info : entries) {
            if (info.typePrefix == "C") {
                configuredIds.insert(makePollDeviceId("C", info.modbusAddr));
            } else if (info.typePrefix == "I") {
                configuredIds.insert(makePollDeviceId("I", info.modbusAddr, info.range.startChannel));
            } else {
                for (int ch = info.range.startChannel; ch <= info.range.endChannel; ++ch) {
                    configuredIds.insert(makePollDeviceId(info.typePrefix, info.modbusAddr, ch));
                }
            }
        }
    }
}

void SerialWorker::parsingdata(const QByteArray& frame)
{
    const int UNIT_LEN = 2;
    const int CRC_LEN = 2;

    if (frame.size() < 10) {
        emit logGenerated(m_workerName, getLogWithTime("解析错误：帧长度不足10字节"));
        return;
    }

    QString addressHex = QString(frame.mid(0, UNIT_LEN).toHex().toUpper()).rightJustified(4, '0');
    QString funcCode = QString(frame.mid(UNIT_LEN, UNIT_LEN).toHex().toUpper());
    QByteArray regCountBytes = frame.mid(4, UNIT_LEN);
    uint16_t regCount = (static_cast<unsigned char>(regCountBytes[0]) << 8) | static_cast<unsigned char>(regCountBytes[1]);
    int dataStart = 6;
    int dataLen = regCount * UNIT_LEN;

    if (frame.size() < dataStart + dataLen + CRC_LEN) {
        emit logGenerated(m_workerName, getLogWithTime("解析错误：数据段长度不匹配"));
        return;
    }

    QList<QString> dataBits;
    for (int i = 0; i < regCount; ++i) {
        int pos = dataStart + i * UNIT_LEN;
        dataBits.append(QString(frame.mid(pos, UNIT_LEN).toHex().toUpper()));
    }

    QString typePrefix;
    bool funcValid = true;
    if (funcCode == "1011") {
        typePrefix = "W";
    } else if (funcCode == "1014") {
        typePrefix = "T";
    } else if (funcCode == "1021") {
        typePrefix = "E";
    } else if (funcCode == "1030") {
        typePrefix = "C";
    } else {
        emit logGenerated(m_workerName, getLogWithTime(QString("解析错误：不支持的功能码%1").arg(funcCode)));
        funcValid = false;
    }
    if (!funcValid) return;

    // 根据TaskType过滤数据，确保每个worker只处理它应该处理的数据类型
    if (m_taskType == TaskType::C_TYPE && typePrefix != "C") {
        emit logGenerated(m_workerName, getLogWithTime(QString("跳过解析：当前worker只处理C类型数据，收到%1类型").arg(typePrefix)));
        return;
    }
    if (m_taskType == TaskType::WTE_TYPE && typePrefix == "C") {
        emit logGenerated(m_workerName, getLogWithTime(QString("跳过解析：当前worker只处理WTE类型数据，收到C类型").arg(typePrefix)));
        return;
    }

    PollRowInfo matchedInfo;
    bool configFound = false;
    for (const QStringList& row : newarrange) {
        const QVector<PollRowInfo> entries = parsePollConfigEntries(row);
        for (const PollRowInfo& info : entries) {
            if (info.typePrefix != typePrefix) continue;

            const QString rowHexAddr = QString("%1").arg(info.modbusAddr, 4, 16, QChar('0')).toUpper();
            if (rowHexAddr != addressHex) continue;

            matchedInfo = info;
            configFound = true;
            break;
        }
        if (configFound) {
            break;
        }
    }

    if (typePrefix != "C") {
        int processCount = dataBits.size();
        if (processCount <= 0) {
            emit logGenerated(m_workerName, getLogWithTime("解析错误：无有效数据位"));
            return;
        }

        for (int i = 0; i < processCount; ++i) {
            const int channel = configFound ? (matchedInfo.range.startChannel + i) : (i + 1);
            const QString currentId = configFound
                ? makePollDeviceId(typePrefix, matchedInfo.modbusAddr, channel)
                : ("测试" + typePrefix + QString::number(i + 1));

            QString dataHex = dataBits[i];
            bool valueOk;
            uint32_t dataValue = dataHex.toUInt(&valueOk, 16);
            if (!valueOk) dataValue = 0;
            QString status, statusDesc;
            bool isQualified = false;
            
            if (typePrefix == "W") {
                isQualified = (dataValue >= 75 && dataValue <= 3500);
                status = isQualified ? "1" : "2";
                statusDesc = status == "1" ? "正常" : "异常";
            } else if (typePrefix == "T") {
                isQualified = (dataValue >= 75 && dataValue <= 350);
                status = isQualified ? "1" : "2";
                statusDesc = status == "1" ? "正常" : "异常";
            } else if (typePrefix == "E") {
                isQualified = (dataValue <= 2500);
                status = isQualified ? "1" : "2";
                statusDesc = status == "1" ? "正常" : "异常";
            }
            
            // 统计返回数据的设备数
            returnedDeviceCounts[typePrefix]++;
            
            // 统计合格设备数量
            if (isQualified) {
                qualifiedCounts[typePrefix]++;
            }
            
            addressFuncData[addressHex + funcCode + currentId] = QStringList() << dataHex;
            // 直接发送信号给DBManager处理数据
            emit wteDataReady(typePrefix, currentId, QDateTime::currentDateTime(),
                             dataValue, status, statusDesc);
            qDebug() << QString("[%1] 解析数据 - 类型：%2 | 标识：%3 | 原始值：%4 | 十六进制：%5 | 状态：%6")
                            .arg(m_workerName)
                            .arg(typePrefix)
                            .arg(currentId)
                            .arg(dataValue)
                            .arg(dataHex)
                            .arg(statusDesc);
        }
    } else {
        if (dataBits.size() < 22) {
            emit logGenerated(m_workerName, getLogWithTime("解析错误：尘埃数据不完整（需≥22个寄存器）"));
            return;
        }

        QString currentId = "C1";
        if (configFound) {
            currentId = makePollDeviceId("C", matchedInfo.modbusAddr);
        }

        struct DustData {
            QString name;
            int statusIdx;
            int valueHighIdx;
            int valueLowIdx;
            bool isTempHum;
        };

        QList<DustData> dustDataList = {
            {"温度", 0, 1, -1, true},
            {"湿度", 2, 3, -1, true},
            {"0.3um尘埃", 4, 5, 6, false},
            {"0.5um尘埃", 7, 8, 9, false},
            {"1.0um尘埃", 10, 11, 12, false},
            {"2.5um尘埃", 13, 14, 15, false},
            {"5.0um尘埃", 16, 17, 18, false},
            {"10um尘埃", 19, 20, 21, false}
        };

        QList<QString> dustResult;
        double tempValue = 0.0, humidityValue = 0.0, cleanlinessValue = 0.0;
        bool tempValid = false, humidityValid = false, cleanlinessValid = false;
        
        for (const DustData& item : dustDataList) {
            bool statusOk = false;
            uint16_t statusVal = dataBits[item.statusIdx].toUInt(&statusOk, 16);
            if (!statusOk) {
                dustResult.append("状态解析失败");
                continue;
            }

            bool isEnabled = (statusVal & 0x1000) != 0;
            bool isNormal = (statusVal & 0x0100) == 0;
            QString statusDesc = isEnabled ? (isNormal ? "启用-正常" : "启用-异常") : "未启用";

            QString valueDesc = "无";
            if (isEnabled) {
                if (item.isTempHum) {
                    bool valOk = false;
                    uint16_t val = dataBits[item.valueHighIdx].toUInt(&valOk, 16);
                    if (valOk) {
                        double actualVal = val * 0.1;
                        valueDesc = item.name == "温度" ? QString("%1℃").arg(actualVal, 0, 'f', 1)
                                                       : QString("%1%RH").arg(actualVal, 0, 'f', 1);
                        
                        // 收集环境数据
                        if (item.name == "温度") {
                            tempValue = actualVal;
                            tempValid = true;
                        } else if (item.name == "湿度") {
                            humidityValue = actualVal;
                            humidityValid = true;
                        }
                    } else {
                        valueDesc = "解析失败";
                    }
                } else {
                    if (item.valueLowIdx >= dataBits.size()) {
                        valueDesc = "数据不完整";
                        dustResult.append(valueDesc);
                        continue;
                    }
                    bool highOk = false, lowOk = false;
                    uint16_t highVal = dataBits[item.valueHighIdx].toUInt(&highOk, 16);
                    uint16_t lowVal = dataBits[item.valueLowIdx].toUInt(&lowOk, 16);
                    if (highOk && lowOk) {
                        uint32_t actualVal = (static_cast<uint32_t>(highVal) << 16) | lowVal;
                        valueDesc = QString("%1 个").arg(actualVal);
                        
                        // 使用0.5um尘埃作为洁净度指标
                        if (item.name == "0.5um尘埃") {
                            cleanlinessValue = actualVal;
                            cleanlinessValid = true;
                        }
                    } else {
                        valueDesc = "解析失败";
                    }
                }
            }

            dustResult.append(valueDesc);
        }

        // 将有效的环境数据添加到列表中
        if (tempValid) {
            temperatureValues.append(tempValue);
        }
        if (humidityValid) {
            humidityValues.append(humidityValue);
        }
        if (cleanlinessValid) {
            cleanlinessValues.append(cleanlinessValue);
        }
        
        QString temp = dustResult.size() > 0 ? dustResult[0] : "解析失败";
        QString humidity = dustResult.size() > 1 ? dustResult[1] : "解析失败";
        QString dust03 = dustResult.size() > 2 ? dustResult[2] : "解析失败";
        QString dust05 = dustResult.size() > 3 ? dustResult[3] : "解析失败";
        QString dust10 = dustResult.size() > 4 ? dustResult[4] : "解析失败";
        QString dust25 = dustResult.size() > 5 ? dustResult[5] : "解析失败";
        QString dust50 = dustResult.size() > 6 ? dustResult[6] : "解析失败";
        QString dust10Total = dustResult.size() > 7 ? dustResult[7] : "解析失败";

        // 直接发送信号给DBManager处理数据
        emit dustDataReady(currentId, QDateTime::currentDateTime(),
                           temp, humidity, dust03, dust05, dust10, dust25, dust50, dust10Total);
        addressFuncData[addressHex + funcCode + currentId] = dustResult;
    }

    if (!addressFuncData.isEmpty()) {
        emit parsedDataReady(addressFuncData);
        qDebug() << "[SerialWorker-" << m_workerName << "] 发送解析数据给MainWindow：" << addressFuncData.keys();
        addressFuncData.clear();
    }

    if (overtime->isActive()) {
        overtime->stop();
        sendnum = 0;
    }
}

uint16_t SerialWorker::calcrc(const QByteArray &data)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < data.size(); ++i) {
        crc ^= static_cast<uint8_t>(data[i]);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

QString SerialWorker::getLogWithTime(const QString& content)
{
    QString portName = getCurrentPortName();
    if (portName.isEmpty()) {
        portName = "未连接";
    }
    return QString("[%1] %2:%3").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")).arg(portName).arg(content);
}

void SerialWorker::singleTest(const QString& hexAddr, const QString& hexReg, const QString& type)
{
    bool isConnectionOpen = false;
    QString connectionTypeStr = "";

    if (m_connectionType == ConnectionType::SERIAL) {
        isConnectionOpen = serial->isOpen();
        connectionTypeStr = "串口";
    } else if (m_connectionType == ConnectionType::TCP_SERVER) {
        isConnectionOpen = (tcpSocket != nullptr);
        connectionTypeStr = "TCP连接";
    }

    if (!isConnectionOpen) {
        emit dataReceived(m_workerName, QString("警告：请先打开%1再执行单次测试！").arg(connectionTypeStr));
        return;
    }

    QString funcCode;
    QString regCount = "0001";
    if (type == "腕带") funcCode = "1011";
    else if (type == "设备") funcCode = "1021";
    else if (type == "台垫") funcCode = "1014";
    else if (type == "尘埃") {
        funcCode = "1030";
        regCount = "0016";
    } else {
        emit dataReceived(m_workerName, "错误：请选择有效设备类型！");
        return;
    }

    bool pollingActive = isPollingActive;
    if (pollingActive) {
        stopPolling();
    }

    QMutexLocker locker(&queueMutex);
    sendQueue.clear();
    sendQueueDeviceIds.clear();
    sendnum = 0;
    currentExpectedAddrFunc.clear();
    currentIonizerId.clear();
    isSingleTest = true;
    locker.unlock();

    QString modbusStr = hexAddr + funcCode + hexReg + regCount;
    QByteArray sendData;
    bool dataOk = true;
    for (int i = 0; i < modbusStr.length() && dataOk; i += 2) {
        QString byteStr = modbusStr.mid(i, 2);
        uint8_t byte = byteStr.toUInt(&dataOk, 16);
        if (!dataOk) {
            emit dataReceived(m_workerName, "错误：数据构造失败！无效的十六进制字符");
            isSingleTest = false;
            if (pollingActive) startPolling();
            return;
        }
        sendData.append(static_cast<char>(byte));
    }
    uint16_t crc = calcrc(sendData);
    sendData.append(static_cast<char>(crc & 0xFF));
    sendData.append(static_cast<char>((crc >> 8) & 0xFF));

    locker.relock();
    sendQueue.enqueue(sendData);
    locker.unlock();

    if (!overtime->isActive()) {
        sendnum = 0;
        sendNextData();
    }
}

void SerialWorker::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) return;

    QString errMsg = QString("串口错误：%1（错误码：%2）").arg(serial->errorString()).arg(static_cast<int>(error));
    emit logGenerated(m_workerName, getLogWithTime("错误：" + errMsg));
    emit dataReceived(m_workerName, "错误：" + errMsg);

    if (error == QSerialPort::ResourceError) {
        closeSerial();
    }
}

void SerialWorker::onNewConnection()
{
    // 接受新连接
    tcpSocket = tcpServer->nextPendingConnection();

    // 连接信号槽
    connect(tcpSocket, &QTcpSocket::readyRead, this, &SerialWorker::onTcpSocketReadyRead);
    connect(tcpSocket, &QTcpSocket::disconnected, this, &SerialWorker::onTcpSocketDisconnected);
    connect(tcpSocket, static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error), this, &SerialWorker::onTcpServerError);

    emit logGenerated(m_workerName, getLogWithTime(QString("新的TCP连接已建立：%1:%2").arg(tcpSocket->peerAddress().toString()).arg(tcpSocket->peerPort())));

    if (!isPollingActive) {
        startPolling();
        emit logGenerated(m_workerName, getLogWithTime("客户端连接成功，自动启动轮询"));
    } else if (!sendTimer->isActive()) {
        sendTimer->start();
        emit logGenerated(m_workerName, getLogWithTime("客户端已连接，恢复轮询定时器"));
    }
}

void SerialWorker::onTcpSocketReadyRead()
{
    if (!tcpSocket) {
        return;
    }

    QByteArray newData = tcpSocket->readAll();
    if (newData.isEmpty()) {
        return;
    }

    emit dataReceived(m_workerName, "接收数据：" + newData.toHex().toUpper());

    // 处理接收到的数据，与串口接收的数据处理逻辑相同
    recvBuffer.append(newData);

    // 解析数据
    if (!currentExpectedAddrFunc.isEmpty()) {
        int expectedStart = -1;
        for (int i = 0; i <= recvBuffer.size() - 4; i++) {
            QString addr = QString(recvBuffer.mid(i, 2).toHex().toUpper()).rightJustified(4, '0');
            QString func = QString(recvBuffer.mid(i + 2, 2).toHex().toUpper()).rightJustified(4, '0');
            if (addr + func == currentExpectedAddrFunc) {
                expectedStart = i;
                break;
            }
        }

        if (expectedStart != -1) {
            // 解析帧长度
            int expectedFrameLen = -1;
            if (recvBuffer.size() >= expectedStart + 6) {
                QByteArray regCountBytes = recvBuffer.mid(expectedStart + 4, 2);
                uint16_t regCount = (static_cast<unsigned char>(regCountBytes[0]) << 8) | static_cast<unsigned char>(regCountBytes[1]);
                expectedFrameLen = 2 + 2 + 2 + (regCount * 2) + 2;
            }

            if (expectedFrameLen != -1 && recvBuffer.size() >= expectedStart + expectedFrameLen) {
                QByteArray validFrame = recvBuffer.mid(expectedStart, expectedFrameLen);
                parsingdata(validFrame);
                recvBuffer = recvBuffer.mid(expectedStart + expectedFrameLen);
                currentExpectedAddrFunc.clear();

                // 立即发射信号，让数据传递给MainWindow
                emit parsedDataReady(addressFuncData);
                qDebug() << "[SerialWorker-" << m_workerName << "] 发送解析数据给MainWindow：" << addressFuncData.keys();
                addressFuncData.clear();

                // 接收成功后重置发送次数，避免影响下一条
                sendnum = 0;

                // 发送下一条数据
                QTimer::singleShot(delayMs, this, &SerialWorker::sendNextData);
            }
        }
    }
}

void SerialWorker::onTcpSocketDisconnected()
{
    if (!tcpSocket) {
        return;
    }

    emit logGenerated(m_workerName, getLogWithTime(QString("TCP连接已断开：%1:%2").arg(tcpSocket->peerAddress().toString()).arg(tcpSocket->peerPort())));

    tcpSocket->deleteLater();
    tcpSocket = nullptr;
}

void SerialWorker::onTcpServerError(QAbstractSocket::SocketError error)
{
    // 在较旧版本的 Qt 中，QAbstractSocket::SocketError 枚举中可能没有 NoError 值
    // 直接检查错误字符串是否为空

    QString errorString;
    if (tcpSocket) {
        errorString = tcpSocket->errorString();
    } else if (tcpServer) {
        errorString = tcpServer->errorString();
    } else {
        errorString = "未知TCP错误";
    }

    // 如果错误字符串为空，可能是NoError，直接返回
    if (errorString.isEmpty()) {
        return;
    }

    emit logGenerated(m_workerName, getLogWithTime(QString("TCP错误：%1").arg(errorString)));
}



QString SerialWorker::getCurrentPortName() const
{
    if (serial && serial->isOpen()) {
        return serial->portName();
    }
    return QString();
}

QVector<QStringList> SerialWorker::getConfigData() const
{
    return newarrange;
}

void SerialWorker::appendConfigData(const QVector<QStringList>& additionalConfig)
{
    QMutexLocker locker(&queueMutex);
    for (const QStringList& row : additionalConfig) {
        newarrange.append(row);
    }
    processModbusData(newarrange);
    emit logGenerated(m_workerName, getLogWithTime(QString("已追加 %1 条设备配置").arg(additionalConfig.size())));
}

void SerialWorker::resetPollingStats()
{
    deviceTypeCounts.clear();
    qualifiedCounts.clear();
    returnedDeviceCounts.clear();
    temperatureValues.clear();
    humidityValues.clear();
    cleanlinessValues.clear();
}

void SerialWorker::calculateAndInsertQualifiedRate()
{
    // 计算合格率（使用返回数据的设备数作为分母）
    double wRate = returnedDeviceCounts["W"] > 0 ? static_cast<double>(qualifiedCounts["W"]) / returnedDeviceCounts["W"] * 100 : 0.0;
    double tRate = returnedDeviceCounts["T"] > 0 ? static_cast<double>(qualifiedCounts["T"]) / returnedDeviceCounts["T"] * 100 : 0.0;
    double eRate = returnedDeviceCounts["E"] > 0 ? static_cast<double>(qualifiedCounts["E"]) / returnedDeviceCounts["E"] * 100 : 0.0;
    
    // 计算平均温度（所有返回数据的平均值）
    double avgTemp = 0.0;
    if (!temperatureValues.isEmpty()) {
        double sum = 0.0;
        for (double temp : temperatureValues) {
            sum += temp;
        }
        avgTemp = sum / temperatureValues.size();
    }
    
    // 计算平均湿度（所有返回数据的平均值）
    double avgHumidity = 0.0;
    if (!humidityValues.isEmpty()) {
        double sum = 0.0;
        for (double humidity : humidityValues) {
            sum += humidity;
        }
        avgHumidity = sum / humidityValues.size();
    }
    
    // 计算平均洁净度（所有返回数据的平均值）
    double avgCleanliness = 0.0;
    if (!cleanlinessValues.isEmpty()) {
        double sum = 0.0;
        for (double cleanliness : cleanlinessValues) {
            sum += cleanliness;
        }
        avgCleanliness = sum / cleanlinessValues.size();
    }
    
    // 发送日志信息
    emit logGenerated(m_workerName, getLogWithTime(QString("合格率计算完成：腕带=%1%，台垫=%2%，设备=%3%，平均温度=%4℃，平均湿度=%5%RH，平均洁净度=%6")
        .arg(QString::number(wRate, 'f', 2))
        .arg(QString::number(tRate, 'f', 2))
        .arg(QString::number(eRate, 'f', 2))
        .arg(QString::number(avgTemp, 'f', 1))
        .arg(QString::number(avgHumidity, 'f', 1))
        .arg(QString::number(avgCleanliness, 'f', 0))));
    
    // 发出信号给MainWindow，让MainWindow统一处理和插入数据
    emit qualifiedRateDataReady(QDateTime::currentDateTime(), wRate, tRate, eRate, avgTemp, avgHumidity, avgCleanliness);
}

bool SerialWorker::isConnectionOpen() const
{
    if (m_connectionType == ConnectionType::SERIAL) {
        return serial && serial->isOpen();
    }
    if (m_connectionType == ConnectionType::TCP_SERVER) {
        return tcpSocket != nullptr;
    }
    return false;
}

void SerialWorker::sendRawBytes(const QByteArray& data)
{
    if (!isConnectionOpen() || data.isEmpty()) {
        return;
    }
    if (m_connectionType == ConnectionType::SERIAL && serial) {
        serial->write(data);
    } else if (tcpSocket) {
        tcpSocket->write(data);
    }
}

void SerialWorker::sendToolCommand(const QByteArray& data, const QString& expectedFuncCode)
{
    if (!isConnectionOpen()) {
        emit toolCommandFailed(QStringLiteral("请先在连接设置中连接串口"));
        return;
    }
    if (m_toolMode) {
        emit toolCommandFailed(QStringLiteral("上一条指令尚未完成"));
        return;
    }
    if (data.isEmpty() || expectedFuncCode.isEmpty()) {
        emit toolCommandFailed(QStringLiteral("指令参数无效"));
        return;
    }

    m_resumePollingAfterTool = isPollingActive;
    if (isPollingActive) {
        stopPolling();
    }

    m_toolMode = true;
    m_toolExpectedFunc = expectedFuncCode;
    m_toolSendnum = 0;
    lastSentData = data;
    recvBuffer.clear();

    qint64 written = 0;
    if (m_connectionType == ConnectionType::SERIAL) {
        written = serial->write(data);
    } else if (tcpSocket) {
        written = tcpSocket->write(data);
    }

    if (written != data.size()) {
        finishToolCommand(false, QByteArray(), QStringLiteral("发送失败"));
        return;
    }

    if (!overtime->isActive()) {
        overtime->start();
    }
}

void SerialWorker::finishToolCommand(bool success, const QByteArray& frame, const QString& reason)
{
    if (!m_toolMode) {
        return;
    }

    const QString expectedFunc = m_toolExpectedFunc;
    m_toolMode = false;
    m_toolExpectedFunc.clear();
    m_toolSendnum = 0;
    recvBuffer.clear();

    if (overtime->isActive()) {
        overtime->stop();
    }

    const bool resumePolling = m_resumePollingAfterTool;
    m_resumePollingAfterTool = false;
    if (resumePolling) {
        startPolling();
    }

    if (success) {
        emit toolCommandCompleted(frame, expectedFunc);
    } else {
        emit toolCommandFailed(reason.isEmpty() ? QStringLiteral("指令失败") : reason);
    }
}

bool SerialWorker::processToolReceivedData()
{
    if (m_toolExpectedFunc.isEmpty()) {
        recvBuffer.clear();
        return false;
    }

    int expectedStart = -1;
    for (int i = 0; i <= recvBuffer.size() - 4; ++i) {
        const QString func = QString(recvBuffer.mid(i + 2, 2).toHex().toUpper()).rightJustified(4, QChar('0'));
        if (func == m_toolExpectedFunc) {
            expectedStart = i;
            break;
        }
    }

    if (expectedStart == -1) {
        return false;
    }

    int expectedFrameLen = -1;
    if (m_toolExpectedFunc == QLatin1String("0001")
        || m_toolExpectedFunc == QLatin1String("0110")
        || m_toolExpectedFunc == QLatin1String("0113")
        || m_toolExpectedFunc == QLatin1String("0120")
        || m_toolExpectedFunc == QLatin1String("0010")
        || m_toolExpectedFunc == QLatin1String("0011")
        || m_toolExpectedFunc == QLatin1String("0012")
        || m_toolExpectedFunc == QLatin1String("0013")
        || m_toolExpectedFunc == QLatin1String("0014")
        || m_toolExpectedFunc == QLatin1String("0015")
        || m_toolExpectedFunc == QLatin1String("0020")
        || m_toolExpectedFunc == QLatin1String("0021")
        || m_toolExpectedFunc == QLatin1String("0022")
        || m_toolExpectedFunc == QLatin1String("0040")) {
        expectedFrameLen = 10;
    } else if (recvBuffer.size() >= expectedStart + 6) {
        const QByteArray regCountBytes = recvBuffer.mid(expectedStart + 4, 2);
        const uint16_t regCount = (static_cast<unsigned char>(regCountBytes[0]) << 8)
            | static_cast<unsigned char>(regCountBytes[1]);
        expectedFrameLen = 2 + 2 + 2 + (regCount * 2) + 2;
    }

    if (expectedFrameLen == -1) {
        return false;
    }

    const int currentFrameAvailableLen = recvBuffer.size() - expectedStart;
    if (currentFrameAvailableLen < expectedFrameLen) {
        return false;
    }

    const QByteArray validFrame = recvBuffer.mid(expectedStart, expectedFrameLen);
    const QByteArray dataToCrc = validFrame.left(expectedFrameLen - 2);
    const quint16 calcCrc = calcrc(dataToCrc);
    const quint16 recvCrc = (static_cast<unsigned char>(validFrame[expectedFrameLen - 1]) << 8)
        | static_cast<unsigned char>(validFrame[expectedFrameLen - 2]);

    if (calcCrc != recvCrc) {
        recvBuffer = recvBuffer.mid(expectedStart + 1);
        finishToolCommand(false, QByteArray(), QStringLiteral("CRC校验失败"));
        return true;
    }

    finishToolCommand(true, validFrame, QString());
    return true;
}
