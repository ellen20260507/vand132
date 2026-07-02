#ifndef DBMANAGER_H
#define DBMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QMutex>
#include <QDateTime>
#include <QPair>
#include <QList>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QRunnable>
#include <QThreadPool>

class DBManager : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(DBManager)

public:
    static DBManager* instance();
    ~DBManager();

    // 初始化MySQL连接
    bool initDB(const QString& host = "127.0.0.1", int port = 3307,
                const QString& user = "root", const QString& pwd = "",
                const QString& dbName = "sensor_db");

    // 设备操作
    bool addDevice(const QString& devId, const QString& devType);

    // W/T/E数据插入（仅标识+解析后数据）
    bool insertData(const QString& devId, double parsedValue);

    // W/T/E数据插入（完整数据：标识+时间+值+状态+状态描述）
    bool insertWteData(const QString& deviceType, const QString& deviceId,
                       const QDateTime& recordTime, uint32_t value,
                       const QString& status, const QString& statusDesc);

    // 尘埃数据插入（仅标识+指标+解析后数据）
    bool insertDustData(const QString& devId, const QString& indexId, double parsedValue);

    // 尘埃数据插入（完整数据）
    bool insertDustData(const QString& deviceId, const QDateTime& recordTime,
                       const QString& temp, const QString& humidity,
                       const QString& dust03, const QString& dust05,
                       const QString& dust10, const QString& dust25,
                       const QString& dust50, const QString& dust10Total);

    // 离子风机数据插入（status1+status2）
    bool insertIonFanData(const QString& devId, const QString& status1, const QString& status2);

    // 插入合格率数据
    bool insertQualifiedRateData(const QDateTime& time, double wRate, double tRate, double eRate,
                               double avgTemp, double avgHumidity, double avgCleanliness);

    // 插入报警处理记录
    bool insertAlarmHandling(const QDateTime& handleTime, const QString& handler, const QString& action);

    // 历史数据查询方法
    QList<QPair<QDateTime, double>> getDustHistoryData(const QString& indexId, const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);

    // 新增：从分离的W/T/E表中获取历史数据
    QList<QPair<QDateTime, double>> getWHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);
    QList<QPair<QDateTime, double>> getTHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);
    QList<QPair<QDateTime, double>> getEHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);

    // 新增：获取合格率数据（一轮设备点合格/一轮总点）
    QList<QPair<QDateTime, double>> getQualifiedRateData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);
    QList<QPair<QDateTime, double>> getWQualifiedRateData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);
    QList<QPair<QDateTime, double>> getTQualifiedRateData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);
    QList<QPair<QDateTime, double>> getEQualifiedRateData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);

    // 新增：获取环境数据历史数据（温度、湿度、洁净度）
    QList<QPair<QDateTime, double>> getTempHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);
    QList<QPair<QDateTime, double>> getHumidityHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);
    QList<QPair<QDateTime, double>> getCleanlinessHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);

    // 新增：获取最近一天的合格率和环境数据，用于历史折线图
    QMap<QDateTime, QMap<QString, double>> getHistoryChartData();

    // 新增：根据时间范围获取历史图表数据
    QJsonObject getHistoryChartData(const QString& timeRange);

    // 新增：获取指定时间范围内的平均数据
    QVector<double> getAverageDataFromTimeRange(const QDateTime& startTime, int minutesRange);

    // 获取最近一次轮询的统计数据（用于计算合格率）
    QMap<QString, QPair<int, int>> getPollingStatistics(const QDateTime& time, int minutesRange);

    // 获取最近一次轮询的平均环境数据（温度、湿度、洁净度）
    QVector<double> getPollingEnvData(const QDateTime& time, int minutesRange);

    // 获取最近的合格率记录
    QList<QMap<QString, QVariant>> getLatestQualifiedRateRecords(int limit = 10);

    // 获取指定时间范围内的报警处理记录
    QList<QMap<QString, QVariant>> getAlarmHandlingRecords(const QDateTime& startTime, const QDateTime& endTime);

signals:
    void logGenerated(const QString& workerName, const QString& message);
    // 异步查询完成信号
    void tempHistoryDataReady(const QList<QPair<QDateTime, double>>& data);
    void humidityHistoryDataReady(const QList<QPair<QDateTime, double>>& data);
    void cleanlinessHistoryDataReady(const QList<QPair<QDateTime, double>>& data);
    void averageDataReady(const QVector<double>& data);

public slots:
    // 接收SerialWorker发送的数据并处理
    void handleWteData(const QString& deviceType, const QString& deviceId,
                      const QDateTime& recordTime, uint32_t value,
                      const QString& status, const QString& statusDesc);
    void handleDustData(const QString& deviceId, const QDateTime& recordTime,
                       const QString& temp, const QString& humidity,
                       const QString& dust03, const QString& dust05,
                       const QString& dust10, const QString& dust25,
                       const QString& dust50, const QString& dust10Total);
    void handleQualifiedRateData(const QDateTime& time, double wRate, double tRate, double eRate,
                               double avgTemp, double avgHumidity, double avgCleanliness);

private:
    DBManager(QObject *parent = nullptr);
    bool deviceExists(const QString& devId); // 内部判断设备是否存在
    void writeLogToFile(const QString& workerName, const QString& message);
    bool checkAndReconnect(); // 检查并重新连接数据库

    QSqlDatabase m_db;
    QMutex m_mutex;
    bool m_isConnected;
    QThreadPool* m_threadPool;

    // 数据库连接参数，用于重连
    QString m_host;
    int m_port;
    QString m_user;
    QString m_password;
    QString m_dbName;

    // 内部异步查询类
    class AsyncQueryTask : public QRunnable {
    public:
        enum QueryType {
            TempHistory,
            HumidityHistory,
            CleanlinessHistory,
            AverageData
        };

        AsyncQueryTask(DBManager* manager, QueryType type, const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes);
        AsyncQueryTask(DBManager* manager, const QDateTime& startTime, int minutesRange);

        void run() override;

    private:
        DBManager* m_manager;
        QueryType m_type;
        QDateTime m_startTime;
        QDateTime m_endTime;
        int m_intervalMinutes;
        int m_minutesRange;
    };
};

#endif // DBMANAGER_H
