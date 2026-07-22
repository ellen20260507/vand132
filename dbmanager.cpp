#include "dbmanager.h"
#include "pollconfig.h"

#include <QDebug>
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>
#include <QDir>
#include <QThread>
#include <QMetaObject>

namespace {

double parseDisplayNumber(const QString& text, bool* okOut = nullptr)
{
    QString s = text.trimmed();
    s.remove(QStringLiteral("℃"));
    s.remove(QStringLiteral("%RH"));
    s.remove(QStringLiteral(" 个"));
    if (s.isEmpty() || s == QStringLiteral("解析失败") || s == QStringLiteral("无")
        || s == QStringLiteral("未启用") || s == QStringLiteral("数据不完整")) {
        if (okOut) {
            *okOut = false;
        }
        return 0.0;
    }
    return s.toDouble(okOut);
}

int parseModbusAddrFromCDeviceId(const QString& deviceId)
{
    if (!deviceId.startsWith(QLatin1Char('C')) || deviceId.size() < 2) {
        return -1;
    }
    bool ok = false;
    const int addr = deviceId.mid(1).toInt(&ok);
    return ok ? addr : -1;
}

QString dustPointId(int modbusAddr, const QString& metricKey)
{
    return QStringLiteral("C%1_%2").arg(modbusAddr).arg(metricKey);
}

QList<QPair<QDateTime, double>> queryChannelHistory(QSqlDatabase& db,
                                                    const QString& channelType,
                                                    const QString& metricKey,
                                                    const QDateTime& startTime,
                                                    const QDateTime& endTime)
{
    QList<QPair<QDateTime, double>> result;
    QSqlQuery query(db);
    QString sql = R"(
        SELECT
            DATE_FORMAT(cr.record_time, '%Y-%m-%d %H:%i:00') AS time_slot,
            AVG(cr.value_num) AS avg_value
        FROM channel_reading cr
        INNER JOIN poll_channel pc ON cr.channel_id = pc.id
        WHERE cr.record_time >= :startTime AND cr.record_time <= :endTime
          AND pc.channel_type = :channelType
          AND cr.value_num IS NOT NULL
    )";

    if (!metricKey.isEmpty()) {
        sql += QStringLiteral(" AND pc.metric_key = :metricKey");
    }

    sql += QStringLiteral(" GROUP BY time_slot ORDER BY time_slot");

    query.prepare(sql);
    query.bindValue(QStringLiteral(":startTime"), startTime);
    query.bindValue(QStringLiteral(":endTime"), endTime);
    query.bindValue(QStringLiteral(":channelType"), channelType);
    if (!metricKey.isEmpty()) {
        query.bindValue(QStringLiteral(":metricKey"), metricKey);
    }

    if (query.exec()) {
        while (query.next()) {
            const QDateTime time = QDateTime::fromString(query.value(QStringLiteral("time_slot")).toString(),
                                                         QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            result.append(qMakePair(time, query.value(QStringLiteral("avg_value")).toDouble()));
        }
    }

    return result;
}

} // namespace

DBManager* DBManager::instance()
{
    static DBManager instance;
    return &instance;
}

DBManager::DBManager(QObject* parent)
    : QObject(parent)
{
    m_threadPool = new QThreadPool(this);
    m_threadPool->setMaxThreadCount(4);

    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(500);
    connect(m_flushTimer, &QTimer::timeout, this, &DBManager::flushPendingReadings);
}

DBManager::~DBManager()
{
    if (m_flushTimer) {
        m_flushTimer->stop();
    }

    {
        QMutexLocker locker(&m_writeMutex);
        flushPendingReadingsLocked();
    }

    if (m_threadPool) {
        m_threadPool->clear();
        m_threadPool->waitForDone();
    }

    if (m_db.isOpen()) {
        m_db.close();
    }
    if (m_readDb.isOpen()) {
        m_readDb.close();
    }

    if (QSqlDatabase::contains(QStringLiteral("dbManagerConnection"))) {
        QSqlDatabase::removeDatabase(QStringLiteral("dbManagerConnection"));
    }
    if (QSqlDatabase::contains(QStringLiteral("dbManagerReadConnection"))) {
        QSqlDatabase::removeDatabase(QStringLiteral("dbManagerReadConnection"));
    }
}

bool DBManager::initDB(const QString& host, int port, const QString& user, const QString& pwd, const QString& dbName)
{
    m_host = host;
    m_port = port;
    m_user = user;
    m_password = pwd;
    m_dbName = dbName;

    QSqlDatabase testDb = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), QStringLiteral("testConnection"));
    testDb.setHostName(host);
    testDb.setPort(port);
    testDb.setUserName(user);
    testDb.setPassword(pwd);
    testDb.setDatabaseName(QStringLiteral("mysql"));
    testDb.setConnectOptions(QStringLiteral("MYSQL_OPT_CONNECT_TIMEOUT=5"));

    emit logGenerated(QStringLiteral("DBManager"),
                      QStringLiteral("尝试连接到 MySQL: host=%1, port=%2, user=%3").arg(host).arg(port).arg(user));
    emit logGenerated(QStringLiteral("DBManager"),
                      QStringLiteral("检查 QMYSQL 驱动: %1").arg(testDb.isValid() ? QStringLiteral("有效") : QStringLiteral("无效")));
    emit logGenerated(QStringLiteral("DBManager"),
                      QStringLiteral("数据库驱动类型: %1").arg(testDb.driverName()));

    if (!testDb.open()) {
        const QString errorMsg = QStringLiteral("连接 MySQL 失败: %1").arg(testDb.lastError().text());
        qCritical() << errorMsg;
        emit logGenerated(QStringLiteral("DBManager"), errorMsg);

        const QString errorText = testDb.lastError().text();
        if (errorText.contains(QStringLiteral("Access denied"))) {
            emit logGenerated(QStringLiteral("DBManager"), QStringLiteral("原因分析: 用户凭证错误（用户名或密码不正确）"));
        } else if (errorText.contains(QStringLiteral("Unknown database"))) {
            emit logGenerated(QStringLiteral("DBManager"), QStringLiteral("原因分析: 指定的数据库不存在"));
        } else if (errorText.contains(QStringLiteral("Can't connect"))) {
            emit logGenerated(QStringLiteral("DBManager"), QStringLiteral("原因分析: 无法连接到 MySQL 服务器"));
        } else if (errorText.contains(QStringLiteral("Driver not loaded"))) {
            emit logGenerated(QStringLiteral("DBManager"), QStringLiteral("原因分析: MySQL 驱动加载失败"));
        }

        testDb.close();
        QSqlDatabase::removeDatabase(QStringLiteral("testConnection"));
        m_isConnected = false;
        return false;
    }

    emit logGenerated(QStringLiteral("DBManager"), QStringLiteral("✓ 成功连接到 MySQL 服务器"));
    testDb.close();
    QSqlDatabase::removeDatabase(QStringLiteral("testConnection"));

    if (QSqlDatabase::contains(QStringLiteral("dbManagerConnection"))) {
        m_db = QSqlDatabase::database(QStringLiteral("dbManagerConnection"));
        if (m_db.isOpen()) {
            m_db.close();
        }
    } else {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), QStringLiteral("dbManagerConnection"));
    }

    m_db.setHostName(host);
    m_db.setPort(port);
    m_db.setUserName(user);
    m_db.setPassword(pwd);
    m_db.setDatabaseName(dbName);
    m_db.setConnectOptions(QStringLiteral("MYSQL_OPT_CONNECT_TIMEOUT=5"));

    emit logGenerated(QStringLiteral("DBManager"), QStringLiteral("尝试连接到目标数据库: %1").arg(dbName));

    if (!m_db.open()) {
        const QString errorMsg = QStringLiteral("连接数据库 %1 失败: %2").arg(dbName).arg(m_db.lastError().text());
        qCritical() << errorMsg;
        emit logGenerated(QStringLiteral("DBManager"), errorMsg);

        if (m_db.lastError().text().contains(QStringLiteral("Unknown database"))) {
            emit logGenerated(QStringLiteral("DBManager"),
                              QStringLiteral("原因分析: 数据库 %1 不存在，请先创建").arg(dbName));
            emit logGenerated(QStringLiteral("DBManager"),
                              QStringLiteral("可以使用命令: CREATE DATABASE %1;").arg(dbName));
        }

        m_isConnected = false;
        return false;
    }

    m_isConnected = true;
    m_dbEverConnected = true;
    qDebug() << "数据库连接成功";
    emit logGenerated(QStringLiteral("DBManager"), QStringLiteral("✓ 成功连接到数据库: %1").arg(dbName));

    if (!createSchema()) {
        m_isConnected = false;
        return false;
    }

    if (!ensureReadConnection()) {
        emit logGenerated(QStringLiteral("DBManager"), QStringLiteral("只读数据库连接初始化失败"));
    }

    m_flushTimer->start();
    return true;
}

bool DBManager::createSchema()
{
    QSqlQuery query(m_db);

    const QString createAlarmSql = R"(
        CREATE TABLE IF NOT EXISTS alarm_handling (
            id INT PRIMARY KEY AUTO_INCREMENT COMMENT '记录ID',
            handle_time DATETIME NOT NULL COMMENT '处理时间',
            handler VARCHAR(50) NOT NULL COMMENT '处理人员',
            action TEXT NOT NULL COMMENT '处理方式',
            create_time DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '记录创建时间',
            INDEX idx_handle_time (handle_time),
            INDEX idx_handler (handler)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT '报警处理记录表';
    )";
    if (!query.exec(createAlarmSql)) {
        qCritical() << "创建报警处理记录表失败：" << query.lastError().text();
        return false;
    }

    const QString createRateSql = R"(
        CREATE TABLE IF NOT EXISTS qualified_rate (
            id INT PRIMARY KEY AUTO_INCREMENT,
            time DATETIME NOT NULL,
            w_qualified_rate DOUBLE NOT NULL,
            t_qualified_rate DOUBLE NOT NULL,
            e_qualified_rate DOUBLE NOT NULL,
            avg_temperature DOUBLE,
            avg_humidity DOUBLE,
            avg_cleanliness DOUBLE,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_time (time)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT '合格率数据表';
    )";
    if (!query.exec(createRateSql)) {
        qCritical() << "创建合格率数据表失败：" << query.lastError().text();
        return false;
    }

    const QString createPollDeviceSql = R"(
        CREATE TABLE IF NOT EXISTS poll_device (
            id INT PRIMARY KEY AUTO_INCREMENT,
            modbus_addr INT NOT NULL,
            device_label VARCHAR(64) NULL,
            w_config VARCHAR(64) NULL,
            t_config VARCHAR(64) NULL,
            e_config VARCHAR(64) NULL,
            c_config VARCHAR(64) NULL,
            i_config VARCHAR(64) NULL,
            enabled TINYINT(1) NOT NULL DEFAULT 1,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
            UNIQUE KEY uk_modbus_addr (modbus_addr)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT '轮询设备配置表';
    )";
    if (!query.exec(createPollDeviceSql)) {
        qCritical() << "创建 poll_device 表失败：" << query.lastError().text();
        return false;
    }

    const QString createPollChannelSql = R"(
        CREATE TABLE IF NOT EXISTS poll_channel (
            id INT PRIMARY KEY AUTO_INCREMENT,
            device_id INT NOT NULL,
            channel_type CHAR(1) NOT NULL,
            channel_no INT NOT NULL,
            point_id VARCHAR(64) NOT NULL,
            metric_key VARCHAR(32) NULL,
            start_register INT NOT NULL DEFAULT 0,
            register_count INT NOT NULL DEFAULT 1,
            enabled TINYINT(1) NOT NULL DEFAULT 1,
            UNIQUE KEY uk_point_id (point_id),
            INDEX idx_device_id (device_id),
            CONSTRAINT fk_poll_channel_device FOREIGN KEY (device_id)
                REFERENCES poll_device(id) ON DELETE CASCADE
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT '轮询通道配置表';
    )";
    if (!query.exec(createPollChannelSql)) {
        qCritical() << "创建 poll_channel 表失败：" << query.lastError().text();
        return false;
    }

    const QString createChannelReadingSql = R"(
        CREATE TABLE IF NOT EXISTS channel_reading (
            id BIGINT PRIMARY KEY AUTO_INCREMENT,
            channel_id INT NOT NULL,
            device_id INT NOT NULL,
            record_time DATETIME(3) NOT NULL,
            value_num DOUBLE NULL,
            value_raw VARCHAR(64) NULL,
            status VARCHAR(16) NULL,
            status_desc VARCHAR(64) NULL,
            INDEX idx_device_time (device_id, record_time),
            INDEX idx_channel_time (channel_id, record_time),
            CONSTRAINT fk_channel_reading_channel FOREIGN KEY (channel_id)
                REFERENCES poll_channel(id) ON DELETE CASCADE
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT '通道读数表';
    )";
    if (!query.exec(createChannelReadingSql)) {
        qCritical() << "创建 channel_reading 表失败：" << query.lastError().text();
        return false;
    }

    return true;
}

bool DBManager::ensureReadConnection()
{
    if (m_isConnected && m_readDb.isOpen()) {
        QSqlQuery testQuery(m_readDb);
        if (testQuery.exec(QStringLiteral("SELECT 1"))) {
            return true;
        }
    }

    if (QSqlDatabase::contains(QStringLiteral("dbManagerReadConnection"))) {
        m_readDb = QSqlDatabase::database(QStringLiteral("dbManagerReadConnection"));
        if (m_readDb.isOpen()) {
            m_readDb.close();
        }
    } else {
        m_readDb = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), QStringLiteral("dbManagerReadConnection"));
    }

    m_readDb.setHostName(m_host);
    m_readDb.setPort(m_port);
    m_readDb.setUserName(m_user);
    m_readDb.setPassword(m_password);
    m_readDb.setDatabaseName(m_dbName);
    m_readDb.setConnectOptions(QStringLiteral("MYSQL_OPT_CONNECT_TIMEOUT=5"));

    return m_readDb.open();
}

bool DBManager::checkAndReconnect()
{
    if (m_isConnected && m_db.isOpen()) {
        QSqlQuery testQuery(m_db);
        if (testQuery.exec(QStringLiteral("SELECT 1"))) {
            return true;
        }
    }

    // 从未成功连上过时直接失败，避免启动阶段在主线程空等重试
    if (!m_dbEverConnected) {
        return false;
    }

    const int maxRetry = 3;
    for (int retry = 0; retry < maxRetry; ++retry) {
        QString logMessage = QStringLiteral("数据库连接异常，尝试重新连接... (尝试%1/%2)")
                                 .arg(retry + 1)
                                 .arg(maxRetry);
        emit logGenerated(QStringLiteral("DBManager"), logMessage);
        writeLogToFile(QStringLiteral("DBManager"), logMessage);

        if (m_db.isOpen()) {
            m_db.close();
        }

        if (QSqlDatabase::contains(QStringLiteral("dbManagerConnection"))) {
            m_db = QSqlDatabase::database(QStringLiteral("dbManagerConnection"));
        } else {
            m_db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), QStringLiteral("dbManagerConnection"));
        }

        m_db.setHostName(m_host);
        m_db.setPort(m_port);
        m_db.setUserName(m_user);
        m_db.setPassword(m_password);
        m_db.setDatabaseName(m_dbName);
        m_db.setConnectOptions(QStringLiteral("MYSQL_OPT_CONNECT_TIMEOUT=5"));

        if (m_db.open()) {
            m_isConnected = true;
            logMessage = QStringLiteral("数据库重新连接成功");
            emit logGenerated(QStringLiteral("DBManager"), logMessage);
            writeLogToFile(QStringLiteral("DBManager"), logMessage);
            return true;
        }

        logMessage = QStringLiteral("数据库重新连接失败：") + m_db.lastError().text();
        emit logGenerated(QStringLiteral("DBManager"), logMessage);
        writeLogToFile(QStringLiteral("DBManager"), logMessage);

        if (retry < maxRetry - 1) {
            QThread::msleep(1000);
        }
    }

    m_isConnected = false;
    return false;
}

bool DBManager::syncPollConfigFromRows(const QVector<QStringList>& rows)
{
    QMutexLocker locker(&m_writeMutex);
    if (!checkAndReconnect()) {
        return false;
    }

    m_pointChannelIds.clear();
    m_pointDeviceIds.clear();

    QSqlQuery query(m_db);

    for (const QStringList& row : rows) {
        if (row.isEmpty()) {
            continue;
        }

        bool addrOk = false;
        const int modbusAddr = row[0].toInt(&addrOk);
        if (!addrOk || modbusAddr <= 0) {
            continue;
        }

        const QString wConfig = row.size() > 1 ? row[1].trimmed() : QString();
        const QString tConfig = row.size() > 2 ? row[2].trimmed() : QString();
        const QString eConfig = row.size() > 3 ? row[3].trimmed() : QString();
        const QString cConfig = row.size() > 4 ? row[4].trimmed() : QString();
        const QString iConfig = row.size() > 5 ? row[5].trimmed() : QString();

        query.prepare(R"(
            INSERT INTO poll_device (modbus_addr, device_label, w_config, t_config, e_config, c_config, i_config, enabled)
            VALUES (:modbusAddr, :deviceLabel, :wConfig, :tConfig, :eConfig, :cConfig, :iConfig, 1)
            ON DUPLICATE KEY UPDATE
                device_label = VALUES(device_label),
                w_config = VALUES(w_config),
                t_config = VALUES(t_config),
                e_config = VALUES(e_config),
                c_config = VALUES(c_config),
                i_config = VALUES(i_config),
                enabled = 1,
                updated_at = CURRENT_TIMESTAMP
        )");
        query.bindValue(QStringLiteral(":modbusAddr"), modbusAddr);
        query.bindValue(QStringLiteral(":deviceLabel"), QStringLiteral("Device %1").arg(modbusAddr));
        query.bindValue(QStringLiteral(":wConfig"), wConfig.isEmpty() ? QVariant(QVariant::String) : wConfig);
        query.bindValue(QStringLiteral(":tConfig"), tConfig.isEmpty() ? QVariant(QVariant::String) : tConfig);
        query.bindValue(QStringLiteral(":eConfig"), eConfig.isEmpty() ? QVariant(QVariant::String) : eConfig);
        query.bindValue(QStringLiteral(":cConfig"), cConfig.isEmpty() ? QVariant(QVariant::String) : cConfig);
        query.bindValue(QStringLiteral(":iConfig"), iConfig.isEmpty() ? QVariant(QVariant::String) : iConfig);

        if (!query.exec()) {
            qCritical() << "同步 poll_device 失败：" << query.lastError().text();
            return false;
        }

        query.prepare(QStringLiteral("SELECT id FROM poll_device WHERE modbus_addr = :modbusAddr"));
        query.bindValue(QStringLiteral(":modbusAddr"), modbusAddr);
        if (!query.exec() || !query.next()) {
            qCritical() << "查询 poll_device id 失败：" << query.lastError().text();
            return false;
        }

        const qint64 pollDeviceId = query.value(0).toLongLong();

        query.prepare(QStringLiteral("DELETE FROM poll_channel WHERE device_id = :deviceId"));
        query.bindValue(QStringLiteral(":deviceId"), pollDeviceId);
        if (!query.exec()) {
            qCritical() << "删除旧 poll_channel 失败：" << query.lastError().text();
            return false;
        }

        const QVector<PollRowInfo> entries = parsePollConfigEntries(row);
        for (const PollRowInfo& info : entries) {
            if (info.typePrefix == QStringLiteral("C")) {
                static const char* metricKeys[] = {
                    "temp", "humidity", "dust_03um", "dust_05um", "dust_10um",
                    "dust_25um", "dust_50um", "dust_10um_total"
                };
                int channelNo = 1;
                for (const char* metricKey : metricKeys) {
                    const QString pointId = dustPointId(info.modbusAddr, QString::fromLatin1(metricKey));
                    query.prepare(R"(
                        INSERT INTO poll_channel
                            (device_id, channel_type, channel_no, point_id, metric_key, start_register, register_count, enabled)
                        VALUES (:deviceId, 'C', :channelNo, :pointId, :metricKey, 0, 1, 1)
                    )");
                    query.bindValue(QStringLiteral(":deviceId"), pollDeviceId);
                    query.bindValue(QStringLiteral(":channelNo"), channelNo++);
                    query.bindValue(QStringLiteral(":pointId"), pointId);
                    query.bindValue(QStringLiteral(":metricKey"), QString::fromLatin1(metricKey));
                    if (!query.exec()) {
                        qCritical() << "插入 C poll_channel 失败：" << query.lastError().text();
                        return false;
                    }
                }
            } else if (info.typePrefix == QStringLiteral("I")) {
                const QString pointId = makePollDeviceId(QStringLiteral("I"), info.modbusAddr, info.range.startChannel);
                query.prepare(R"(
                    INSERT INTO poll_channel
                        (device_id, channel_type, channel_no, point_id, metric_key, start_register, register_count, enabled)
                    VALUES (:deviceId, 'I', :channelNo, :pointId, NULL, :startRegister, :registerCount, 1)
                )");
                query.bindValue(QStringLiteral(":deviceId"), pollDeviceId);
                query.bindValue(QStringLiteral(":channelNo"), info.range.startChannel);
                query.bindValue(QStringLiteral(":pointId"), pointId);
                query.bindValue(QStringLiteral(":startRegister"), info.range.startRegister);
                query.bindValue(QStringLiteral(":registerCount"), info.range.registerCount);
                if (!query.exec()) {
                    qCritical() << "插入 I poll_channel 失败：" << query.lastError().text();
                    return false;
                }
            } else {
                for (int ch = info.range.startChannel; ch <= info.range.endChannel; ++ch) {
                    const QString pointId = makePollDeviceId(info.typePrefix, info.modbusAddr, ch);
                    const int startRegister = info.range.startRegister + (ch - info.range.startChannel);
                    query.prepare(R"(
                        INSERT INTO poll_channel
                            (device_id, channel_type, channel_no, point_id, metric_key, start_register, register_count, enabled)
                        VALUES (:deviceId, :channelType, :channelNo, :pointId, NULL, :startRegister, 1, 1)
                    )");
                    query.bindValue(QStringLiteral(":deviceId"), pollDeviceId);
                    query.bindValue(QStringLiteral(":channelType"), info.typePrefix);
                    query.bindValue(QStringLiteral(":channelNo"), ch);
                    query.bindValue(QStringLiteral(":pointId"), pointId);
                    query.bindValue(QStringLiteral(":startRegister"), startRegister);
                    if (!query.exec()) {
                        qCritical() << "插入 W/T/E poll_channel 失败：" << query.lastError().text();
                        return false;
                    }
                }
            }
        }
    }

    query.prepare(R"(
        SELECT id, point_id, device_id
        FROM poll_channel
        WHERE enabled = 1
    )");
    if (!query.exec()) {
        qCritical() << "加载 point_id 缓存失败：" << query.lastError().text();
        return false;
    }

    while (query.next()) {
        const QString pointId = query.value(QStringLiteral("point_id")).toString();
        m_pointChannelIds.insert(pointId, query.value(0).toLongLong());
        m_pointDeviceIds.insert(pointId, query.value(QStringLiteral("device_id")).toLongLong());
    }

    return true;
}

qint64 DBManager::resolveChannelId(const QString& pointId, qint64& deviceIdOut)
{
    if (m_pointChannelIds.contains(pointId)) {
        deviceIdOut = m_pointDeviceIds.value(pointId);
        return m_pointChannelIds.value(pointId);
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id, device_id FROM poll_channel WHERE point_id = :pointId"));
    query.bindValue(QStringLiteral(":pointId"), pointId);
    if (!query.exec() || !query.next()) {
        return 0;
    }

    const qint64 channelId = query.value(0).toLongLong();
    deviceIdOut = query.value(1).toLongLong();
    m_pointChannelIds.insert(pointId, channelId);
    m_pointDeviceIds.insert(pointId, deviceIdOut);
    return channelId;
}

void DBManager::enqueueReading(const QString& pointId, const QDateTime& recordTime,
                               double valueNum, bool hasValueNum, const QString& valueRaw,
                               const QString& status, const QString& statusDesc)
{
    QMutexLocker locker(&m_writeMutex);

    qint64 deviceId = 0;
    const qint64 channelId = resolveChannelId(pointId, deviceId);
    if (channelId <= 0) {
        return;
    }

    PendingReading reading;
    reading.channelId = channelId;
    reading.deviceId = deviceId;
    reading.recordTime = recordTime;
    reading.valueNum = valueNum;
    reading.hasValueNum = hasValueNum;
    reading.valueRaw = valueRaw;
    reading.status = status;
    reading.statusDesc = statusDesc;
    m_pendingReadings.append(reading);
}

void DBManager::flushPendingReadings()
{
    QMutexLocker locker(&m_writeMutex);
    flushPendingReadingsLocked();
}

bool DBManager::flushPendingReadingsLocked()
{
    if (m_pendingReadings.isEmpty()) {
        return true;
    }

    if (!checkAndReconnect()) {
        return false;
    }

    QSqlQuery query(m_db);
    QString sql = QStringLiteral(
        "INSERT INTO channel_reading (channel_id, device_id, record_time, value_num, value_raw, status, status_desc) VALUES ");

    QStringList valueClauses;
    for (int i = 0; i < m_pendingReadings.size(); ++i) {
        valueClauses.append(QStringLiteral("(:channelId%1, :deviceId%1, :recordTime%1, :valueNum%1, :valueRaw%1, :status%1, :statusDesc%1)")
                                .arg(i));
    }

    sql += valueClauses.join(QStringLiteral(", "));

    if (!query.prepare(sql)) {
        qCritical() << "批量插入 channel_reading 准备失败：" << query.lastError().text();
        return false;
    }

    for (int bindIndex = 0; bindIndex < m_pendingReadings.size(); ++bindIndex) {
        const PendingReading& reading = m_pendingReadings.at(bindIndex);
        query.bindValue(QStringLiteral(":channelId%1").arg(bindIndex), reading.channelId);
        query.bindValue(QStringLiteral(":deviceId%1").arg(bindIndex), reading.deviceId);
        query.bindValue(QStringLiteral(":recordTime%1").arg(bindIndex), reading.recordTime);
        query.bindValue(QStringLiteral(":valueNum%1").arg(bindIndex),
                        reading.hasValueNum ? QVariant(reading.valueNum) : QVariant(QVariant::Double));
        query.bindValue(QStringLiteral(":valueRaw%1").arg(bindIndex), reading.valueRaw);
        query.bindValue(QStringLiteral(":status%1").arg(bindIndex), reading.status);
        query.bindValue(QStringLiteral(":statusDesc%1").arg(bindIndex), reading.statusDesc);
    }

    if (!query.exec()) {
        qCritical() << "批量插入 channel_reading 失败：" << query.lastError().text();
        return false;
    }

    m_pendingReadings.clear();
    return true;
}

void DBManager::handleWteData(const QString& deviceType, const QString& deviceId,
                              const QDateTime& recordTime, uint32_t value,
                              const QString& status, const QString& statusDesc)
{
    Q_UNUSED(deviceType);
    enqueueReading(deviceId, recordTime, static_cast<double>(value), true,
                   QString::number(value), status, statusDesc);
}

void DBManager::handleDustData(const QString& deviceId, const QDateTime& recordTime,
                               const QString& temp, const QString& humidity,
                               const QString& dust03, const QString& dust05,
                               const QString& dust10, const QString& dust25,
                               const QString& dust50, const QString& dust10Total)
{
    const int modbusAddr = parseModbusAddrFromCDeviceId(deviceId);
    if (modbusAddr <= 0) {
        return;
    }

    struct DustMetric {
        QString metricKey;
        QString rawValue;
    };

    const QList<DustMetric> metrics = {
        {QStringLiteral("temp"), temp},
        {QStringLiteral("humidity"), humidity},
        {QStringLiteral("dust_03um"), dust03},
        {QStringLiteral("dust_05um"), dust05},
        {QStringLiteral("dust_10um"), dust10},
        {QStringLiteral("dust_25um"), dust25},
        {QStringLiteral("dust_50um"), dust50},
        {QStringLiteral("dust_10um_total"), dust10Total}
    };

    for (const DustMetric& metric : metrics) {
        bool ok = false;
        const double valueNum = parseDisplayNumber(metric.rawValue, &ok);
        const QString pointId = dustPointId(modbusAddr, metric.metricKey);
        enqueueReading(pointId, recordTime, valueNum, ok, metric.rawValue, QString(), QString());
    }
}

void DBManager::handleChannelReading(const QString& pointId, const QDateTime& recordTime,
                                     double valueNum, const QString& valueRaw,
                                     const QString& status, const QString& statusDesc)
{
    enqueueReading(pointId, recordTime, valueNum, true, valueRaw, status, statusDesc);
}

class DBManager::ExportDeviceTask : public QRunnable
{
public:
    ExportDeviceTask(DBManager* manager, int modbusAddr, const QString& filePath)
        : m_manager(manager)
        , m_modbusAddr(modbusAddr)
        , m_filePath(filePath)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QString errorMessage;
        const bool success = m_manager->exportDeviceDataToCsv(m_modbusAddr, m_filePath, errorMessage);
        QMetaObject::invokeMethod(m_manager, [this, success, errorMessage]() {
            emit m_manager->exportDeviceDataFinished(m_modbusAddr, success, m_filePath, errorMessage);
        }, Qt::QueuedConnection);
    }

private:
    DBManager* m_manager;
    int m_modbusAddr;
    QString m_filePath;
};

void DBManager::requestExportDeviceData(int modbusAddr, const QString& filePath)
{
    m_threadPool->start(new ExportDeviceTask(this, modbusAddr, filePath));
}

bool DBManager::exportDeviceDataToCsv(int modbusAddr, const QString& filePath, QString& errorMessage)
{
    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        errorMessage = QStringLiteral("只读数据库连接不可用");
        return false;
    }

    QSqlQuery deviceQuery(m_readDb);
    deviceQuery.prepare(QStringLiteral("SELECT id FROM poll_device WHERE modbus_addr = :modbusAddr"));
    deviceQuery.bindValue(QStringLiteral(":modbusAddr"), modbusAddr);
    if (!deviceQuery.exec() || !deviceQuery.next()) {
        errorMessage = QStringLiteral("未找到 modbus 地址 %1 对应的设备").arg(modbusAddr);
        return false;
    }

    const qint64 deviceId = deviceQuery.value(0).toLongLong();

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errorMessage = QStringLiteral("无法打开导出文件: %1").arg(file.errorString());
        return false;
    }

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << QChar(0xFEFF);
    out << "modbus_addr,point_id,channel_type,channel_no,metric_key,record_time,value_num,value_raw,status,status_desc\n";

    const int pageSize = 5000;
    qint64 offset = 0;

    while (true) {
        QSqlQuery query(m_readDb);
        query.prepare(R"(
            SELECT pd.modbus_addr, pc.point_id, pc.channel_type, pc.channel_no, pc.metric_key,
                   cr.record_time, cr.value_num, cr.value_raw, cr.status, cr.status_desc
            FROM channel_reading cr
            INNER JOIN poll_channel pc ON cr.channel_id = pc.id
            INNER JOIN poll_device pd ON cr.device_id = pd.id
            WHERE cr.device_id = :deviceId
            ORDER BY cr.record_time, cr.id
            LIMIT :limit OFFSET :offset
        )");
        query.bindValue(QStringLiteral(":deviceId"), deviceId);
        query.bindValue(QStringLiteral(":limit"), pageSize);
        query.bindValue(QStringLiteral(":offset"), offset);

        if (!query.exec()) {
            errorMessage = query.lastError().text();
            return false;
        }

        int rowCount = 0;
        while (query.next()) {
            ++rowCount;
            const auto csvField = [](const QVariant& value) -> QString {
                QString text = value.toString();
                text.replace('"', QStringLiteral("\"\""));
                return QStringLiteral("\"%1\"").arg(text);
            };

            out << query.value(QStringLiteral("modbus_addr")).toInt() << ','
                << csvField(query.value(QStringLiteral("point_id"))) << ','
                << csvField(query.value(QStringLiteral("channel_type"))) << ','
                << query.value(QStringLiteral("channel_no")).toInt() << ','
                << csvField(query.value(QStringLiteral("metric_key"))) << ','
                << csvField(query.value(QStringLiteral("record_time"))) << ','
                << csvField(query.value(QStringLiteral("value_num"))) << ','
                << csvField(query.value(QStringLiteral("value_raw"))) << ','
                << csvField(query.value(QStringLiteral("status"))) << ','
                << csvField(query.value(QStringLiteral("status_desc"))) << '\n';
        }

        if (rowCount < pageSize) {
            break;
        }
        offset += pageSize;
    }

    file.close();
    return true;
}

bool DBManager::insertQualifiedRateData(const QDateTime& time, double wRate, double tRate, double eRate,
                                        double avgTemp, double avgHumidity, double avgCleanliness)
{
    QMutexLocker locker(&m_writeMutex);
    if (!checkAndReconnect()) {
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO qualified_rate
            (time, w_qualified_rate, t_qualified_rate, e_qualified_rate, avg_temperature, avg_humidity, avg_cleanliness)
        VALUES (:time, :w_rate, :t_rate, :e_rate, :temp, :humidity, :cleanliness)
    )");
    query.bindValue(QStringLiteral(":time"), time);
    query.bindValue(QStringLiteral(":w_rate"), wRate);
    query.bindValue(QStringLiteral(":t_rate"), tRate);
    query.bindValue(QStringLiteral(":e_rate"), eRate);
    query.bindValue(QStringLiteral(":temp"), avgTemp);
    query.bindValue(QStringLiteral(":humidity"), avgHumidity);
    query.bindValue(QStringLiteral(":cleanliness"), avgCleanliness);

    if (!query.exec()) {
        qCritical() << "插入合格率数据失败：" << query.lastError().text();
        return false;
    }

    return true;
}

void DBManager::handleQualifiedRateData(const QDateTime& time, double wRate, double tRate, double eRate,
                                        double avgTemp, double avgHumidity, double avgCleanliness)
{
    const QString logMessage = QStringLiteral("收到合格率数据：时间=%1, 腕带=%2%, 台垫=%3%, 设备=%4%, 温度=%5℃, 湿度=%6%RH, 洁净度=%7")
                                   .arg(time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                                   .arg(QString::number(wRate * 100, 'f', 2))
                                   .arg(QString::number(tRate * 100, 'f', 2))
                                   .arg(QString::number(eRate * 100, 'f', 2))
                                   .arg(QString::number(avgTemp, 'f', 1))
                                   .arg(QString::number(avgHumidity, 'f', 1))
                                   .arg(QString::number(avgCleanliness, 'f', 0));
    emit logGenerated(QStringLiteral("DBManager"), logMessage);
    writeLogToFile(QStringLiteral("DBManager"), logMessage);

    if (qFuzzyIsNull(avgTemp) || qFuzzyIsNull(avgHumidity)) {
        const QString skipLog = QStringLiteral("⏭ 跳过插入：平均温度或平均湿度无效（温度=%1℃, 湿度=%2%RH）")
                                    .arg(QString::number(avgTemp, 'f', 1))
                                    .arg(QString::number(avgHumidity, 'f', 1));
        emit logGenerated(QStringLiteral("DBManager"), skipLog);
        writeLogToFile(QStringLiteral("DBManager"), skipLog);
        return;
    }

    struct QualifiedRateTask : public QRunnable {
        DBManager* manager;
        QDateTime time;
        double wRate;
        double tRate;
        double eRate;
        double avgTemp;
        double avgHumidity;
        double avgCleanliness;

        QualifiedRateTask(DBManager* m, const QDateTime& t, double w, double tRateValue, double e,
                          double temp, double humidity, double cleanliness)
            : manager(m)
            , time(t)
            , wRate(w)
            , tRate(tRateValue)
            , eRate(e)
            , avgTemp(temp)
            , avgHumidity(humidity)
            , avgCleanliness(cleanliness)
        {
            setAutoDelete(true);
        }

        void run() override
        {
            emit manager->logGenerated(QStringLiteral("DBManager"), QStringLiteral("🔄 开始插入合格率数据到数据库..."));
            manager->writeLogToFile(QStringLiteral("DBManager"), QStringLiteral("开始插入合格率数据到数据库..."));

            const bool success = manager->insertQualifiedRateData(time, wRate, tRate, eRate,
                                                                  avgTemp, avgHumidity, avgCleanliness);
            if (success) {
                const QString successLog = QStringLiteral("✅ 合格率数据插入成功：腕带=%1%, 台垫=%2%, 设备=%3%, 温度=%4℃, 湿度=%5%RH, 洁净度=%6")
                                               .arg(QString::number(wRate * 100, 'f', 2))
                                               .arg(QString::number(tRate * 100, 'f', 2))
                                               .arg(QString::number(eRate * 100, 'f', 2))
                                               .arg(QString::number(avgTemp, 'f', 1))
                                               .arg(QString::number(avgHumidity, 'f', 1))
                                               .arg(QString::number(avgCleanliness, 'f', 0));
                emit manager->logGenerated(QStringLiteral("DBManager"), successLog);
                manager->writeLogToFile(QStringLiteral("DBManager"), successLog);
            } else {
                emit manager->logGenerated(QStringLiteral("DBManager"), QStringLiteral("❌ 合格率数据插入失败"));
                manager->writeLogToFile(QStringLiteral("DBManager"), QStringLiteral("合格率数据插入失败"));
            }
        }
    };

    m_threadPool->start(new QualifiedRateTask(this, time, wRate, tRate, eRate,
                                              avgTemp, avgHumidity, avgCleanliness));
}

bool DBManager::insertAlarmHandling(const QDateTime& handleTime, const QString& handler, const QString& action)
{
    QMutexLocker locker(&m_writeMutex);
    if (!checkAndReconnect()) {
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO alarm_handling (handle_time, handler, action) VALUES (:handleTime, :handler, :action)"));
    query.bindValue(QStringLiteral(":handleTime"), handleTime);
    query.bindValue(QStringLiteral(":handler"), handler);
    query.bindValue(QStringLiteral(":action"), action);

    if (!query.exec()) {
        qCritical() << "插入告警处理记录失败：" << query.lastError().text();
        return false;
    }

    return true;
}

QList<QMap<QString, QVariant>> DBManager::getAlarmHandlingRecords(const QDateTime& startTime, const QDateTime& endTime)
{
    QList<QMap<QString, QVariant>> result;

    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return result;
    }

    QSqlQuery query(m_readDb);
    query.prepare(R"(
        SELECT id, handle_time, handler, action
        FROM alarm_handling
        WHERE handle_time >= :startTime AND handle_time <= :endTime
        ORDER BY handle_time DESC
    )");
    query.bindValue(QStringLiteral(":startTime"), startTime);
    query.bindValue(QStringLiteral(":endTime"), endTime);

    if (query.exec()) {
        while (query.next()) {
            QMap<QString, QVariant> record;
            record[QStringLiteral("time")] = query.value(QStringLiteral("handle_time"));
            record[QStringLiteral("person")] = query.value(QStringLiteral("handler"));
            record[QStringLiteral("thing")] = query.value(QStringLiteral("action"));
            result.append(record);
        }
    } else {
        qCritical() << "查询告警处理记录失败：" << query.lastError().text();
    }

    return result;
}

QList<QMap<QString, QVariant>> DBManager::getLatestQualifiedRateRecords(int limit)
{
    QList<QMap<QString, QVariant>> result;

    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return result;
    }

    QSqlQuery query(m_readDb);
    query.prepare(R"(
        SELECT time, w_qualified_rate, t_qualified_rate, e_qualified_rate,
               avg_temperature, avg_humidity, avg_cleanliness
        FROM qualified_rate
        ORDER BY time DESC
        LIMIT :limit
    )");
    query.bindValue(QStringLiteral(":limit"), limit);

    if (query.exec()) {
        while (query.next()) {
            QMap<QString, QVariant> record;
            record[QStringLiteral("time")] = query.value(QStringLiteral("time"));
            record[QStringLiteral("wRate")] = query.value(QStringLiteral("w_qualified_rate"));
            record[QStringLiteral("tRate")] = query.value(QStringLiteral("t_qualified_rate"));
            record[QStringLiteral("eRate")] = query.value(QStringLiteral("e_qualified_rate"));
            record[QStringLiteral("avgTemp")] = query.value(QStringLiteral("avg_temperature"));
            record[QStringLiteral("avgHumidity")] = query.value(QStringLiteral("avg_humidity"));
            record[QStringLiteral("avgCleanliness")] = query.value(QStringLiteral("avg_cleanliness"));
            result.append(record);
        }
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getDustHistoryData(const QString& indexId, const QDateTime& startTime,
                                                              const QDateTime& endTime, int intervalMinutes)
{
    Q_UNUSED(intervalMinutes);
    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return {};
    }
    return queryChannelHistory(m_readDb, QStringLiteral("C"), indexId, startTime, endTime);
}

QList<QPair<QDateTime, double>> DBManager::getWHistoryData(const QDateTime& startTime, const QDateTime& endTime,
                                                           int intervalMinutes)
{
    Q_UNUSED(intervalMinutes);
    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return {};
    }
    return queryChannelHistory(m_readDb, QStringLiteral("W"), QString(), startTime, endTime);
}

QList<QPair<QDateTime, double>> DBManager::getTHistoryData(const QDateTime& startTime, const QDateTime& endTime,
                                                           int intervalMinutes)
{
    Q_UNUSED(intervalMinutes);
    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return {};
    }
    return queryChannelHistory(m_readDb, QStringLiteral("T"), QString(), startTime, endTime);
}

QList<QPair<QDateTime, double>> DBManager::getEHistoryData(const QDateTime& startTime, const QDateTime& endTime,
                                                           int intervalMinutes)
{
    Q_UNUSED(intervalMinutes);
    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return {};
    }
    return queryChannelHistory(m_readDb, QStringLiteral("E"), QString(), startTime, endTime);
}

QList<QPair<QDateTime, double>> DBManager::getQualifiedRateData(const QDateTime& startTime, const QDateTime& endTime,
                                                                int intervalMinutes)
{
    Q_UNUSED(intervalMinutes);
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return result;
    }

    QSqlQuery query(m_readDb);
    query.prepare(R"(
        SELECT
            DATE_FORMAT(time, '%Y-%m-%d %H:%i:00') AS time_slot,
            AVG((w_qualified_rate + t_qualified_rate + e_qualified_rate) / 3) AS avg_rate
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )");
    query.bindValue(QStringLiteral(":startTime"), startTime);
    query.bindValue(QStringLiteral(":endTime"), endTime);

    if (query.exec()) {
        while (query.next()) {
            const QDateTime time = QDateTime::fromString(query.value(QStringLiteral("time_slot")).toString(),
                                                         QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            result.append(qMakePair(time, query.value(QStringLiteral("avg_rate")).toDouble()));
        }
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getWQualifiedRateData(const QDateTime& startTime, const QDateTime& endTime,
                                                                  int intervalMinutes)
{
    Q_UNUSED(intervalMinutes);
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return result;
    }

    QSqlQuery query(m_readDb);
    query.prepare(R"(
        SELECT DATE_FORMAT(time, '%Y-%m-%d %H:%i:00') AS time_slot, AVG(w_qualified_rate) AS avg_rate
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )");
    query.bindValue(QStringLiteral(":startTime"), startTime);
    query.bindValue(QStringLiteral(":endTime"), endTime);

    if (query.exec()) {
        while (query.next()) {
            const QDateTime time = QDateTime::fromString(query.value(QStringLiteral("time_slot")).toString(),
                                                         QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            result.append(qMakePair(time, query.value(QStringLiteral("avg_rate")).toDouble()));
        }
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getTQualifiedRateData(const QDateTime& startTime, const QDateTime& endTime,
                                                                  int intervalMinutes)
{
    Q_UNUSED(intervalMinutes);
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return result;
    }

    QSqlQuery query(m_readDb);
    query.prepare(R"(
        SELECT DATE_FORMAT(time, '%Y-%m-%d %H:%i:00') AS time_slot, AVG(t_qualified_rate) AS avg_rate
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )");
    query.bindValue(QStringLiteral(":startTime"), startTime);
    query.bindValue(QStringLiteral(":endTime"), endTime);

    if (query.exec()) {
        while (query.next()) {
            const QDateTime time = QDateTime::fromString(query.value(QStringLiteral("time_slot")).toString(),
                                                         QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            result.append(qMakePair(time, query.value(QStringLiteral("avg_rate")).toDouble()));
        }
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getEQualifiedRateData(const QDateTime& startTime, const QDateTime& endTime,
                                                                  int intervalMinutes)
{
    Q_UNUSED(intervalMinutes);
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return result;
    }

    QSqlQuery query(m_readDb);
    query.prepare(R"(
        SELECT DATE_FORMAT(time, '%Y-%m-%d %H:%i:00') AS time_slot, AVG(e_qualified_rate) AS avg_rate
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )");
    query.bindValue(QStringLiteral(":startTime"), startTime);
    query.bindValue(QStringLiteral(":endTime"), endTime);

    if (query.exec()) {
        while (query.next()) {
            const QDateTime time = QDateTime::fromString(query.value(QStringLiteral("time_slot")).toString(),
                                                         QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            result.append(qMakePair(time, query.value(QStringLiteral("avg_rate")).toDouble()));
        }
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getTempHistoryData(const QDateTime& startTime, const QDateTime& endTime,
                                                              int intervalMinutes)
{
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_readMutex);
    const QString logMessage = QStringLiteral("查询温度历史数据：开始时间=%1, 结束时间=%2, 间隔=%3分钟")
                                   .arg(startTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                                   .arg(endTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                                   .arg(intervalMinutes);
    emit logGenerated(QStringLiteral("DBManager"), logMessage);
    writeLogToFile(QStringLiteral("DBManager"), logMessage);

    if (!ensureReadConnection()) {
        emit logGenerated(QStringLiteral("DBManager"), QStringLiteral("数据库连接失败，无法查询温度历史数据"));
        return result;
    }

    QSqlQuery query(m_readDb);
    query.prepare(R"(
        SELECT
            DATE_FORMAT(DATE_ADD(time, INTERVAL -MINUTE(time) % :interval MINUTE), '%Y-%m-%d %H:%i:00') AS time_slot,
            AVG(avg_temperature) AS avg_value
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )");
    query.bindValue(QStringLiteral(":startTime"), startTime);
    query.bindValue(QStringLiteral(":endTime"), endTime);
    query.bindValue(QStringLiteral(":interval"), intervalMinutes);

    if (query.exec()) {
        while (query.next()) {
            const QDateTime time = QDateTime::fromString(query.value(QStringLiteral("time_slot")).toString(),
                                                         QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            result.append(qMakePair(time, query.value(QStringLiteral("avg_value")).toDouble()));
        }
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getHumidityHistoryData(const QDateTime& startTime, const QDateTime& endTime,
                                                                  int intervalMinutes)
{
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return result;
    }

    QSqlQuery query(m_readDb);
    query.prepare(R"(
        SELECT
            DATE_FORMAT(DATE_ADD(time, INTERVAL -MINUTE(time) % :interval MINUTE), '%Y-%m-%d %H:%i:00') AS time_slot,
            AVG(avg_humidity) AS avg_value
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )");
    query.bindValue(QStringLiteral(":startTime"), startTime);
    query.bindValue(QStringLiteral(":endTime"), endTime);
    query.bindValue(QStringLiteral(":interval"), intervalMinutes);

    if (query.exec()) {
        while (query.next()) {
            const QDateTime time = QDateTime::fromString(query.value(QStringLiteral("time_slot")).toString(),
                                                         QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            result.append(qMakePair(time, query.value(QStringLiteral("avg_value")).toDouble()));
        }
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getCleanlinessHistoryData(const QDateTime& startTime, const QDateTime& endTime,
                                                                       int intervalMinutes)
{
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return result;
    }

    QSqlQuery query(m_readDb);
    query.prepare(R"(
        SELECT
            DATE_FORMAT(DATE_ADD(time, INTERVAL -MINUTE(time) % :interval MINUTE), '%Y-%m-%d %H:%i:00') AS time_slot,
            AVG(avg_cleanliness) AS avg_value
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )");
    query.bindValue(QStringLiteral(":startTime"), startTime);
    query.bindValue(QStringLiteral(":endTime"), endTime);
    query.bindValue(QStringLiteral(":interval"), intervalMinutes);

    if (query.exec()) {
        while (query.next()) {
            const QDateTime time = QDateTime::fromString(query.value(QStringLiteral("time_slot")).toString(),
                                                         QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            result.append(qMakePair(time, query.value(QStringLiteral("avg_value")).toDouble()));
        }
    }

    return result;
}

QMap<QDateTime, QMap<QString, double>> DBManager::getHistoryChartData()
{
    QMap<QDateTime, QMap<QString, double>> result;

    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return result;
    }

    QSqlQuery query(m_readDb);
    const QDateTime endTime = QDateTime::currentDateTime();
    const QDateTime startTime = endTime.addDays(-1);

    query.prepare(R"(
        SELECT time, w_qualified_rate, t_qualified_rate, e_qualified_rate,
               avg_temperature, avg_humidity, avg_cleanliness
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        ORDER BY time
    )");
    query.bindValue(QStringLiteral(":startTime"), startTime);
    query.bindValue(QStringLiteral(":endTime"), endTime);

    if (query.exec()) {
        QList<QPair<QDateTime, QMap<QString, double>>> allData;
        while (query.next()) {
            const QDateTime time = query.value(QStringLiteral("time")).toDateTime();
            QMap<QString, double> dataMap;
            dataMap[QStringLiteral("腕带合格率")] = query.value(QStringLiteral("w_qualified_rate")).toDouble();
            dataMap[QStringLiteral("台垫合格率")] = query.value(QStringLiteral("t_qualified_rate")).toDouble();
            dataMap[QStringLiteral("设备合格率")] = query.value(QStringLiteral("e_qualified_rate")).toDouble();
            dataMap[QStringLiteral("平均温度")] = query.value(QStringLiteral("avg_temperature")).toDouble();
            dataMap[QStringLiteral("平均湿度")] = query.value(QStringLiteral("avg_humidity")).toDouble();
            dataMap[QStringLiteral("平均洁净度")] = query.value(QStringLiteral("avg_cleanliness")).toDouble();
            allData.append(qMakePair(time, dataMap));
        }

        for (int i = 0; i < allData.size(); i += 5) {
            result[allData[i].first] = allData[i].second;
        }
        if (result.isEmpty() && !allData.isEmpty()) {
            result[allData.last().first] = allData.last().second;
        }
    }

    return result;
}

QJsonObject DBManager::getHistoryChartData(const QString& timeRange)
{
    QJsonObject result;

    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return result;
    }

    QSqlQuery query(m_readDb);
    const QDateTime endTime = QDateTime::currentDateTime();
    QDateTime startTime = endTime.addDays(-1);

    if (timeRange == QStringLiteral("1h")) {
        startTime = endTime.addSecs(-3600);
    } else if (timeRange == QStringLiteral("7d")) {
        startTime = endTime.addDays(-7);
    }

    query.prepare(R"(
        SELECT time, w_qualified_rate, t_qualified_rate, e_qualified_rate,
               avg_temperature, avg_humidity, avg_cleanliness
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        ORDER BY time
    )");
    query.bindValue(QStringLiteral(":startTime"), startTime);
    query.bindValue(QStringLiteral(":endTime"), endTime);

    if (query.exec()) {
        QJsonArray timeArray;
        QJsonArray wRateArray;
        QJsonArray tRateArray;
        QJsonArray eRateArray;
        QJsonArray tempArray;
        QJsonArray humidityArray;
        QJsonArray cleanlinessArray;

        while (query.next()) {
            timeArray.append(query.value(QStringLiteral("time")).toDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
            wRateArray.append(query.value(QStringLiteral("w_qualified_rate")).toDouble());
            tRateArray.append(query.value(QStringLiteral("t_qualified_rate")).toDouble());
            eRateArray.append(query.value(QStringLiteral("e_qualified_rate")).toDouble());
            tempArray.append(query.value(QStringLiteral("avg_temperature")).toDouble());
            humidityArray.append(query.value(QStringLiteral("avg_humidity")).toDouble());
            cleanlinessArray.append(query.value(QStringLiteral("avg_cleanliness")).toDouble());
        }

        result[QStringLiteral("time")] = timeArray;
        result[QStringLiteral("wRate")] = wRateArray;
        result[QStringLiteral("tRate")] = tRateArray;
        result[QStringLiteral("eRate")] = eRateArray;
        result[QStringLiteral("temperature")] = tempArray;
        result[QStringLiteral("humidity")] = humidityArray;
        result[QStringLiteral("cleanliness")] = cleanlinessArray;
    }

    return result;
}

QVector<double> DBManager::getAverageDataFromTimeRange(const QDateTime& startTime, int minutesRange)
{
    QVector<double> result(6, 0.0);

    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return result;
    }

    QSqlQuery query(m_readDb);
    const QDateTime endTime = startTime.addSecs(minutesRange * 60);

    query.prepare(R"(
        SELECT
            AVG(w_qualified_rate) AS avg_w_rate,
            AVG(t_qualified_rate) AS avg_t_rate,
            AVG(e_qualified_rate) AS avg_e_rate,
            AVG(avg_temperature) AS avg_temp,
            AVG(avg_humidity) AS avg_humidity,
            AVG(avg_cleanliness) AS avg_cleanliness
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
    )");
    query.bindValue(QStringLiteral(":startTime"), startTime);
    query.bindValue(QStringLiteral(":endTime"), endTime);

    if (query.exec() && query.next()) {
        result[0] = query.value(QStringLiteral("avg_w_rate")).toDouble();
        result[1] = query.value(QStringLiteral("avg_t_rate")).toDouble();
        result[2] = query.value(QStringLiteral("avg_e_rate")).toDouble();
        result[3] = query.value(QStringLiteral("avg_temp")).toDouble();
        result[4] = query.value(QStringLiteral("avg_humidity")).toDouble();
        result[5] = query.value(QStringLiteral("avg_cleanliness")).toDouble();
    }

    return result;
}

QMap<QString, QPair<int, int>> DBManager::getPollingStatistics(const QDateTime& time, int minutesRange)
{
    QMap<QString, QPair<int, int>> result;

    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return result;
    }

    QSqlQuery query(m_readDb);
    const QDateTime startTime = time.addSecs(-minutesRange * 60);

    query.prepare(R"(
        SELECT pc.channel_type,
               COUNT(*) AS total_count,
               SUM(CASE WHEN cr.status = '1' THEN 1 ELSE 0 END) AS qualified_count
        FROM channel_reading cr
        INNER JOIN poll_channel pc ON cr.channel_id = pc.id
        WHERE pc.channel_type IN ('W', 'T', 'E')
          AND cr.record_time >= :startTime AND cr.record_time <= :endTime
        GROUP BY pc.channel_type
    )");
    query.bindValue(QStringLiteral(":startTime"), startTime);
    query.bindValue(QStringLiteral(":endTime"), time);

    if (query.exec()) {
        while (query.next()) {
            const QString channelType = query.value(QStringLiteral("channel_type")).toString();
            result[channelType] = qMakePair(query.value(QStringLiteral("total_count")).toInt(),
                                            query.value(QStringLiteral("qualified_count")).toInt());
        }
    }

    return result;
}

QVector<double> DBManager::getPollingEnvData(const QDateTime& time, int minutesRange)
{
    QVector<double> result(3, 0.0);

    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        return result;
    }

    QSqlQuery query(m_readDb);
    const QDateTime startTime = time.addSecs(-minutesRange * 60);

    query.prepare(R"(
        SELECT pc.metric_key, AVG(cr.value_num) AS avg_value
        FROM channel_reading cr
        INNER JOIN poll_channel pc ON cr.channel_id = pc.id
        WHERE pc.channel_type = 'C'
          AND pc.metric_key IN ('temp', 'humidity', 'dust_05um')
          AND cr.record_time >= :startTime AND cr.record_time <= :endTime
          AND cr.value_num IS NOT NULL
        GROUP BY pc.metric_key
    )");
    query.bindValue(QStringLiteral(":startTime"), startTime);
    query.bindValue(QStringLiteral(":endTime"), time);

    if (query.exec()) {
        while (query.next()) {
            const QString metricKey = query.value(QStringLiteral("metric_key")).toString();
            const double avgValue = query.value(QStringLiteral("avg_value")).toDouble();
            if (metricKey == QStringLiteral("temp")) {
                result[0] = avgValue;
            } else if (metricKey == QStringLiteral("humidity")) {
                result[1] = avgValue;
            } else if (metricKey == QStringLiteral("dust_05um")) {
                result[2] = avgValue;
            }
        }
    }

    return result;
}

void DBManager::writeLogToFile(const QString& workerName, const QString& message)
{
    static QMutex fileMutex;
    QMutexLocker locker(&fileMutex);

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString logFilePath = appDir + QStringLiteral("/logs");

    QDir logDir(logFilePath);
    if (!logDir.exists()) {
        logDir.mkpath(logFilePath);
    }

    const QString dateStr = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd"));
    const QString logFileName = logFilePath + QDir::separator() + dateStr + QStringLiteral("_system.log");

    QFile logFile(logFileName);
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&logFile);
        const QString timeStr = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        out << '[' << timeStr << "] [" << workerName << "] " << message << '\n';
        logFile.close();
    }
}

QString DBManager::buildMesReadingsWhereClause(const MesReadingQueryFilter& filter, QStringList& bindKeys) const
{
    QStringList clauses;
    clauses << QStringLiteral("pc.enabled = 1");
    clauses << QStringLiteral("pd.enabled = 1");

    if (!filter.allModbusAddrs && !filter.modbusAddrs.isEmpty()) {
        QStringList placeholders;
        for (int i = 0; i < filter.modbusAddrs.size(); ++i) {
            const QString key = QStringLiteral("modbusAddr%1").arg(i);
            placeholders << QStringLiteral(":") + key;
            bindKeys.append(key);
        }
        clauses << QStringLiteral("pd.modbus_addr IN (%1)").arg(placeholders.join(QStringLiteral(", ")));
    }

    if (!filter.allTypes && !filter.channelTypes.isEmpty()) {
        QStringList placeholders;
        for (int i = 0; i < filter.channelTypes.size(); ++i) {
            const QString key = QStringLiteral("channelType%1").arg(i);
            placeholders << QStringLiteral(":") + key;
            bindKeys.append(key);
        }
        clauses << QStringLiteral("pc.channel_type IN (%1)").arg(placeholders.join(QStringLiteral(", ")));
    }

    if (!filter.allChannels && !filter.channelNos.isEmpty()) {
        QStringList placeholders;
        for (int i = 0; i < filter.channelNos.size(); ++i) {
            const QString key = QStringLiteral("channelNo%1").arg(i);
            placeholders << QStringLiteral(":") + key;
            bindKeys.append(key);
        }
        clauses << QStringLiteral("pc.channel_no IN (%1)").arg(placeholders.join(QStringLiteral(", ")));
    }

    if (filter.hasStartTime) {
        clauses << QStringLiteral("cr.record_time >= :startTime");
        bindKeys.append(QStringLiteral("startTime"));
    }
    if (filter.hasEndTime) {
        clauses << QStringLiteral("cr.record_time <= :endTime");
        bindKeys.append(QStringLiteral("endTime"));
    }

    return clauses.join(QStringLiteral(" AND "));
}

void DBManager::bindMesReadingsFilter(QSqlQuery& query, const MesReadingQueryFilter& filter,
                                      const QStringList& bindKeys) const
{
    int modbusIndex = 0;
    int typeIndex = 0;
    int channelIndex = 0;

    for (const QString& key : bindKeys) {
        if (key.startsWith(QStringLiteral("modbusAddr"))) {
            query.bindValue(QStringLiteral(":") + key, filter.modbusAddrs.at(modbusIndex++));
        } else if (key.startsWith(QStringLiteral("channelType"))) {
            query.bindValue(QStringLiteral(":") + key, filter.channelTypes.at(typeIndex++));
        } else if (key.startsWith(QStringLiteral("channelNo"))) {
            query.bindValue(QStringLiteral(":") + key, filter.channelNos.at(channelIndex++));
        } else if (key == QStringLiteral("startTime")) {
            query.bindValue(QStringLiteral(":startTime"), filter.startTime);
        } else if (key == QStringLiteral("endTime")) {
            query.bindValue(QStringLiteral(":endTime"), filter.endTime);
        }
    }
}

MesReadingQueryResult DBManager::queryMesReadings(const MesReadingQueryFilter& filter)
{
    MesReadingQueryResult result;
    result.appliedFilter = filter;

    QMutexLocker locker(&m_readMutex);
    if (!ensureReadConnection()) {
        result.errorCode = QStringLiteral("DB_UNAVAILABLE");
        result.errorMessage = QStringLiteral("数据库连接不可用");
        return result;
    }

    QStringList bindKeys;
    const QString whereClause = buildMesReadingsWhereClause(filter, bindKeys);
    const int page = filter.allPageSize ? 1 : qMax(1, filter.page);
    const int pageSize = filter.allPageSize ? 0 : qMax(1, filter.pageSize);
    const int offset = filter.allPageSize ? 0 : (page - 1) * pageSize;

    QSqlQuery countQuery(m_readDb);
    const QString countSql = QStringLiteral(
        "SELECT COUNT(*) FROM channel_reading cr "
        "INNER JOIN poll_channel pc ON cr.channel_id = pc.id "
        "INNER JOIN poll_device pd ON cr.device_id = pd.id "
        "WHERE %1").arg(whereClause);
    countQuery.prepare(countSql);
    bindMesReadingsFilter(countQuery, filter, bindKeys);

    if (!countQuery.exec() || !countQuery.next()) {
        result.errorCode = QStringLiteral("QUERY_FAILED");
        result.errorMessage = countQuery.lastError().text();
        return result;
    }

    result.total = countQuery.value(0).toInt();

    bindKeys.clear();
    const QString whereClauseData = buildMesReadingsWhereClause(filter, bindKeys);

    QSqlQuery dataQuery(m_readDb);
    QString dataSql = QStringLiteral(
        "SELECT pc.point_id, cr.record_time, cr.value_num, cr.status_desc "
        "FROM channel_reading cr "
        "INNER JOIN poll_channel pc ON cr.channel_id = pc.id "
        "INNER JOIN poll_device pd ON cr.device_id = pd.id "
        "WHERE %1 "
        "ORDER BY cr.record_time ASC, pd.modbus_addr ASC, pc.channel_type ASC, pc.channel_no ASC, cr.id ASC").arg(whereClauseData);
    if (!filter.allPageSize) {
        dataSql += QStringLiteral(" LIMIT :limit OFFSET :offset");
    }
    dataQuery.prepare(dataSql);
    bindMesReadingsFilter(dataQuery, filter, bindKeys);
    if (!filter.allPageSize) {
        dataQuery.bindValue(QStringLiteral(":limit"), pageSize);
        dataQuery.bindValue(QStringLiteral(":offset"), offset);
    }

    if (!dataQuery.exec()) {
        result.errorCode = QStringLiteral("QUERY_FAILED");
        result.errorMessage = dataQuery.lastError().text();
        return result;
    }

    while (dataQuery.next()) {
        MesReadingRow row;
        row.pointId = dataQuery.value(QStringLiteral("point_id")).toString();
        row.recordTime = dataQuery.value(QStringLiteral("record_time")).toDateTime();
        if (dataQuery.value(QStringLiteral("value_num")).isNull()) {
            row.hasValueNum = false;
        } else {
            row.hasValueNum = true;
            row.valueNum = dataQuery.value(QStringLiteral("value_num")).toDouble();
        }
        row.statusDesc = dataQuery.value(QStringLiteral("status_desc")).toString();
        result.items.append(row);
    }

    result.success = true;
    result.appliedFilter.page = page;
    if (filter.allPageSize) {
        result.appliedFilter.allPageSize = true;
        result.appliedFilter.pageSize = result.total;
    } else {
        result.appliedFilter.pageSize = pageSize;
    }
    return result;
}

class DBManager::AsyncQueryTask : public QRunnable
{
public:
    enum QueryType {
        TempHistory,
        HumidityHistory,
        CleanlinessHistory,
        AverageData
    };

    AsyncQueryTask(DBManager* manager, QueryType type, const QDateTime& startTime,
                   const QDateTime& endTime, int intervalMinutes)
        : m_manager(manager)
        , m_type(type)
        , m_startTime(startTime)
        , m_endTime(endTime)
        , m_intervalMinutes(intervalMinutes)
        , m_minutesRange(0)
    {
        setAutoDelete(true);
    }

    AsyncQueryTask(DBManager* manager, const QDateTime& startTime, int minutesRange)
        : m_manager(manager)
        , m_type(AverageData)
        , m_startTime(startTime)
        , m_endTime()
        , m_intervalMinutes(0)
        , m_minutesRange(minutesRange)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        switch (m_type) {
        case TempHistory: {
            const QList<QPair<QDateTime, double>> data =
                m_manager->getTempHistoryData(m_startTime, m_endTime, m_intervalMinutes);
            emit m_manager->tempHistoryDataReady(data);
            break;
        }
        case HumidityHistory: {
            const QList<QPair<QDateTime, double>> data =
                m_manager->getHumidityHistoryData(m_startTime, m_endTime, m_intervalMinutes);
            emit m_manager->humidityHistoryDataReady(data);
            break;
        }
        case CleanlinessHistory: {
            const QList<QPair<QDateTime, double>> data =
                m_manager->getCleanlinessHistoryData(m_startTime, m_endTime, m_intervalMinutes);
            emit m_manager->cleanlinessHistoryDataReady(data);
            break;
        }
        case AverageData: {
            const QVector<double> data = m_manager->getAverageDataFromTimeRange(m_startTime, m_minutesRange);
            emit m_manager->averageDataReady(data);
            break;
        }
        }
    }

private:
    DBManager* m_manager;
    QueryType m_type;
    QDateTime m_startTime;
    QDateTime m_endTime;
    int m_intervalMinutes;
    int m_minutesRange;
};
