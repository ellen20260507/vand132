#ifndef SERIALWORKER_H
#define SERIALWORKER_H

#include <QObject>
#include <QSerialPort>
#include <QTimer>
#include <QMutex>
#include <QQueue>
#include <QVector>
#include <QSet>
#include <QMap>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QAbstractSocket>
#include <QDateTime>
enum class TaskType {
    C_TYPE,
    WTE_TYPE,
    ALL_TYPE
};

enum class ConnectionType {
    SERIAL,
    TCP_SERVER
};

class SerialWorker : public QObject
{
    Q_OBJECT

public:
    explicit SerialWorker(const QString& workerName, TaskType taskType, QObject *parent = nullptr);
    ~SerialWorker();


    void openSerial(const QString& portName, int baudRate, int sendIntervalMs, int overtimeIntervalMs, int maxResendCount, int delayMs);
    void openTcpServer(const QString& ipAddress, int port, int sendIntervalMs, int overtimeIntervalMs, int maxResendCount, int delayMs);
    void startPolling();
    void stopPolling();
    void setConfigData(const QVector<QStringList>& configData);
    void closeSerial();
    void singleTest(const QString& hexAddr, const QString& hexReg, const QString& type);
    QString getCurrentPortName() const;
    QVector<QStringList> getConfigData() const;
    void appendConfigData(const QVector<QStringList>& additionalConfig);
    bool isPolling() const { return isPollingActive; }
    void setTaskType(TaskType taskType) { m_taskType = taskType; }
    ConnectionType getConnectionType() const { return m_connectionType; }
    void calculateAndInsertQualifiedRate();  // 计算并插入合格率数据

    bool isConnectionOpen() const;
    void sendToolCommand(const QByteArray& data, const QString& expectedFuncCode);

signals:
    void logGenerated(const QString& workerName, const QString& log);
    void dataReceived(const QString& workerName, const QString& data);
    void parsedDataReady(const QMap<QString, QStringList>& data);
    void serialOpenFailed(const QString& workerName, const QString& reason);  // 信号末尾加分号
    void ionizerStatusChanged(const QString& deviceId, bool isOnline);  // 离子风机状态变化信号
    void pollingCycleFinished();  // 一轮轮询完成信号
    void qualifiedRateCalculated(const QString& info);  // 合格率计算结果信号
    // 发送数据给DBManager的信号
    void wteDataReady(const QString& deviceType, const QString& deviceId,
                     const QDateTime& recordTime, uint32_t value,
                     const QString& status, const QString& statusDesc);
    void dustDataReady(const QString& deviceId, const QDateTime& recordTime,
                      const QString& temp, const QString& humidity,
                      const QString& dust03, const QString& dust05,
                      const QString& dust10, const QString& dust25,
                      const QString& dust50, const QString& dust10Total);
    void qualifiedRateDataReady(const QDateTime& time, double wRate, double tRate, double eRate,
                              double avgTemp, double avgHumidity, double avgCleanliness);
    void channelReadingReady(const QString& pointId, const QDateTime& recordTime,
                             double valueNum, const QString& valueRaw,
                             const QString& status, const QString& statusDesc);
    void toolCommandCompleted(const QByteArray& frame, const QString& expectedFuncCode);
    void toolCommandFailed(const QString& reason);

private slots:
    void recv();
    void sendNextData();
    void onTimeout();
    void timerTriggerSend();
    void processModbusData(const QVector<QStringList>& tableData);
    void parsingdata(const QByteArray& frame);
    void onSerialError(QSerialPort::SerialPortError error);
    void onNewConnection();
    void onTcpSocketReadyRead();
    void onTcpSocketDisconnected();
    void onTcpServerError(QAbstractSocket::SocketError error);
    
    // 重置轮询统计数据
    void resetPollingStats();

private:
    bool processToolReceivedData();
    void finishToolCommand(bool success, const QByteArray& frame = QByteArray(), const QString& reason = QString());

    uint16_t calcrc(const QByteArray &data);
    QString getLogWithTime(const QString& content);
    QMutex sendMutex;
    // 非静态成员变量（之前静态成员可能导致线程安全问题，建议改为非静态，仅队列加锁）
    QString m_workerName;
    TaskType m_taskType;
    ConnectionType m_connectionType;
    QSerialPort* serial;
    QTcpServer* tcpServer;
    QTcpSocket* tcpSocket;
    QString m_currentPortName;
    QString m_tcpServerIp;
    int m_tcpServerPort;
    QTimer* sendTimer;
    QTimer* overtime;
    bool isPollingActive;
    bool isSingleTest;
    int maxResendCount;
    int sendnum;
    int delayMs;

    // 改为非静态（静态成员会被所有 SerialWorker 实例共享，导致冲突）
    QMutex queueMutex;
    QMap<QString, QStringList> addressFuncData;
    QVector<QStringList> newarrange;
    QSet<QString> configuredIds;
    QSet<QString> processedIds;
    QQueue<QByteArray> sendQueue;
    QQueue<QString> sendQueueDeviceIds;  // 记录sendQueue中每个数据对应的设备ID
    QByteArray recvBuffer;
    QByteArray lastSentData;
    QString currentExpectedAddrFunc;
    bool isIonizerDevice;  // 标记当前发送的是否是离子风机
    QString currentIonizerId;  // 记录当前发送的离子风机设备ID（I1, I2等）
    
    // 轮询统计数据
    QMap<QString, int> deviceTypeCounts;  // 各类型设备总数
    QMap<QString, int> qualifiedCounts;   // 各类型设备合格数
    QMap<QString, int> returnedDeviceCounts;  // 各类型返回数据的设备数
    QList<double> temperatureValues;      // 温度值列表
    QList<double> humidityValues;         // 湿度值列表
    QList<double> cleanlinessValues;      // 洁净度值列表

    bool m_toolMode = false;
    QString m_toolExpectedFunc;
    bool m_resumePollingAfterTool = false;
    int m_toolSendnum = 0;
};

#endif // SERIALWORKER_H
