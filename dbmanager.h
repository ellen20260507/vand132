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
#include <QTimer>
#include <QHash>
#include <QStringList>

class DBManager : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(DBManager)

public:
    static DBManager* instance();
    ~DBManager();

    bool initDB(const QString& host = "127.0.0.1", int port = 3307,
                const QString& user = "root", const QString& pwd = "",
                const QString& dbName = "sensor_db");

    bool syncPollConfigFromRows(const QVector<QStringList>& rows);

    bool insertQualifiedRateData(const QDateTime& time, double wRate, double tRate, double eRate,
                               double avgTemp, double avgHumidity, double avgCleanliness);
    bool insertAlarmHandling(const QDateTime& handleTime, const QString& handler, const QString& action);

    void requestExportDeviceData(int modbusAddr, const QString& filePath);

    QList<QPair<QDateTime, double>> getDustHistoryData(const QString& indexId, const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);
    QList<QPair<QDateTime, double>> getWHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);
    QList<QPair<QDateTime, double>> getTHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);
    QList<QPair<QDateTime, double>> getEHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);

    QList<QPair<QDateTime, double>> getQualifiedRateData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);
    QList<QPair<QDateTime, double>> getWQualifiedRateData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);
    QList<QPair<QDateTime, double>> getTQualifiedRateData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);
    QList<QPair<QDateTime, double>> getEQualifiedRateData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);

    QList<QPair<QDateTime, double>> getTempHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);
    QList<QPair<QDateTime, double>> getHumidityHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);
    QList<QPair<QDateTime, double>> getCleanlinessHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes = 60);

    QMap<QDateTime, QMap<QString, double>> getHistoryChartData();
    QJsonObject getHistoryChartData(const QString& timeRange);
    QVector<double> getAverageDataFromTimeRange(const QDateTime& startTime, int minutesRange);
    QMap<QString, QPair<int, int>> getPollingStatistics(const QDateTime& time, int minutesRange);
    QVector<double> getPollingEnvData(const QDateTime& time, int minutesRange);
    QList<QMap<QString, QVariant>> getLatestQualifiedRateRecords(int limit = 10);
    QList<QMap<QString, QVariant>> getAlarmHandlingRecords(const QDateTime& startTime, const QDateTime& endTime);

public slots:
    void handleWteData(const QString& deviceType, const QString& deviceId,
                      const QDateTime& recordTime, uint32_t value,
                      const QString& status, const QString& statusDesc);
    void handleDustData(const QString& deviceId, const QDateTime& recordTime,
                       const QString& temp, const QString& humidity,
                       const QString& dust03, const QString& dust05,
                       const QString& dust10, const QString& dust25,
                       const QString& dust50, const QString& dust10Total);
    void handleChannelReading(const QString& pointId, const QDateTime& recordTime,
                              double valueNum, const QString& valueRaw,
                              const QString& status, const QString& statusDesc);
    void handleQualifiedRateData(const QDateTime& time, double wRate, double tRate, double eRate,
                               double avgTemp, double avgHumidity, double avgCleanliness);

signals:
    void logGenerated(const QString& workerName, const QString& message);
    void tempHistoryDataReady(const QList<QPair<QDateTime, double>>& data);
    void humidityHistoryDataReady(const QList<QPair<QDateTime, double>>& data);
    void cleanlinessHistoryDataReady(const QList<QPair<QDateTime, double>>& data);
    void averageDataReady(const QVector<double>& data);
    void exportDeviceDataFinished(int modbusAddr, bool success, const QString& filePath, const QString& errorMessage);

private slots:
    void flushPendingReadings();

private:
    DBManager(QObject *parent = nullptr);

    struct PendingReading {
        qint64 channelId = 0;
        qint64 deviceId = 0;
        QDateTime recordTime;
        double valueNum = 0.0;
        bool hasValueNum = false;
        QString valueRaw;
        QString status;
        QString statusDesc;
    };

    bool createSchema();
    bool ensureReadConnection();
    bool checkAndReconnect();
    void writeLogToFile(const QString& workerName, const QString& message);
    void enqueueReading(const QString& pointId, const QDateTime& recordTime,
                        double valueNum, bool hasValueNum, const QString& valueRaw,
                        const QString& status, const QString& statusDesc);
    bool flushPendingReadingsLocked();
    bool exportDeviceDataToCsv(int modbusAddr, const QString& filePath, QString& errorMessage);
    qint64 resolveChannelId(const QString& pointId, qint64& deviceIdOut);

    QSqlDatabase m_db;
    QSqlDatabase m_readDb;
    QMutex m_writeMutex;
    QMutex m_readMutex;
    bool m_isConnected = false;
    bool m_dbEverConnected = false;
    QThreadPool* m_threadPool = nullptr;
    QTimer* m_flushTimer = nullptr;
    QList<PendingReading> m_pendingReadings;
    QHash<QString, qint64> m_pointChannelIds;
    QHash<QString, qint64> m_pointDeviceIds;

    QString m_host;
    int m_port = 3306;
    QString m_user;
    QString m_password;
    QString m_dbName;

    class AsyncQueryTask;
    class ExportDeviceTask;
};

#endif // DBMANAGER_H
