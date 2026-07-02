#include "dbmanager.h"
#include <QDebug>
#include <QSqlQuery>
#include <QSqlError>
#include <QStringBuilder>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>
#include <QDir>

DBManager* DBManager::instance()
{
    static DBManager instance;
    return &instance;
}

DBManager::DBManager(QObject *parent) : QObject(parent), m_isConnected(false)
{
    // 初始化线程池
    m_threadPool = new QThreadPool(this);
    m_threadPool->setMaxThreadCount(4); // 设置最大线程数
}

DBManager::~DBManager()
{
    if (m_threadPool) {
        m_threadPool->clear();
        m_threadPool->waitForDone();
        delete m_threadPool;
    }
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool DBManager::initDB(const QString& host, int port, const QString& user, const QString& pwd, const QString& dbName)
{
    m_host = host;
    m_port = port;
    m_user = user;
    m_password = pwd;
    m_dbName = dbName;

    // 先尝试连接到 MySQL 默认数据库，检查凭证是否正确
    QSqlDatabase testDb = QSqlDatabase::addDatabase("QMYSQL", "testConnection");
    testDb.setHostName(host);
    testDb.setPort(port);
    testDb.setUserName(user);
    testDb.setPassword(pwd);
    testDb.setDatabaseName("mysql"); // 使用 mysql 数据库进行测试
    testDb.setConnectOptions("MYSQL_OPT_CONNECT_TIMEOUT=5");

    emit logGenerated("DBManager", QString("尝试连接到 MySQL: host=%1, port=%2, user=%3").arg(host).arg(port).arg(user));

    // 检查 QMYSQL 驱动是否真的可用
    emit logGenerated("DBManager", QString("检查 QMYSQL 驱动: %1").arg(testDb.isValid() ? "有效" : "无效"));
    emit logGenerated("DBManager", QString("数据库驱动类型: %1").arg(testDb.driverName()));

    if (!testDb.open()) {
        QString errorMsg = QString("连接 MySQL 失败: %1").arg(testDb.lastError().text());
        qCritical() << errorMsg;
        emit logGenerated("DBManager", errorMsg);
        
        // 分析错误原因
        QString errorText = testDb.lastError().text();
        if (errorText.contains("Access denied")) {
            emit logGenerated("DBManager", "原因分析: 用户凭证错误（用户名或密码不正确）");
        } else if (errorText.contains("Unknown database")) {
            emit logGenerated("DBManager", "原因分析: 指定的数据库不存在");
        } else if (errorText.contains("Can't connect")) {
            emit logGenerated("DBManager", "原因分析: 无法连接到 MySQL 服务器");
        } else if (errorText.contains("Driver not loaded")) {
            emit logGenerated("DBManager", "原因分析: MySQL 驱动加载失败");
            emit logGenerated("DBManager", "可能的解决方法:");
            emit logGenerated("DBManager", "1. 确保 libmysql.dll 版本与 MySQL Server 匹配");
            emit logGenerated("DBManager", "2. 确保程序架构(32位/64位)与 libmysql.dll 匹配");
            emit logGenerated("DBManager", "3. 检查 qsqlmysql.dll 是否来自正确的 Qt 版本");
            emit logGenerated("DBManager", "4. 尝试使用 ODBC 连接作为替代方案");
        }
        
        testDb.close();
        QSqlDatabase::removeDatabase("testConnection");
        m_isConnected = false;
        return false;
    }

    emit logGenerated("DBManager", "✓ 成功连接到 MySQL 服务器");
    testDb.close();
    QSqlDatabase::removeDatabase("testConnection");

    // 现在连接到目标数据库
    m_db = QSqlDatabase::addDatabase("QMYSQL", "dbManagerConnection");
    m_db.setHostName(host);
    m_db.setPort(port);
    m_db.setUserName(user);
    m_db.setPassword(pwd);
    m_db.setDatabaseName(dbName);
    m_db.setConnectOptions("MYSQL_OPT_CONNECT_TIMEOUT=5");

    emit logGenerated("DBManager", QString("尝试连接到目标数据库: %1").arg(dbName));

    if (!m_db.open()) {
        QString errorMsg = QString("连接数据库 %1 失败: %2").arg(dbName).arg(m_db.lastError().text());
        qCritical() << errorMsg;
        emit logGenerated("DBManager", errorMsg);
        
        if (m_db.lastError().text().contains("Unknown database")) {
            emit logGenerated("DBManager", QString("原因分析: 数据库 %1 不存在，请先创建").arg(dbName));
            emit logGenerated("DBManager", QString("可以使用命令: CREATE DATABASE %1;").arg(dbName));
        }
        
        m_isConnected = false;
        return false;
    }

    m_isConnected = true;
    qDebug() << "数据库连接成功";
    emit logGenerated("DBManager", QString("✓ 成功连接到数据库: %1").arg(dbName));


    // 创建报警处理记录表（不存在则创建）
    QSqlQuery alarmQuery(m_db);
    QString createAlarmSql = R"(
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
    if (!alarmQuery.exec(createAlarmSql)) {
        qCritical() << "创建报警处理记录表失败：" << alarmQuery.lastError().text();
    }

    // 创建设备表（不存在则创建）
    QSqlQuery deviceQuery(m_db);
    QString createDeviceSql = R"(
        CREATE TABLE IF NOT EXISTS devices (
            id INT PRIMARY KEY AUTO_INCREMENT,
            device_id VARCHAR(20) NOT NULL UNIQUE,
            device_type VARCHAR(10) NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_device_id (device_id)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT '设备表';
    )";
    if (!deviceQuery.exec(createDeviceSql)) {
        qCritical() << "创建设备表失败：" << deviceQuery.lastError().text();
    }

    // 创建W设备数据表（不存在则创建）
    QSqlQuery wQuery(m_db);
    QString createWSql = R"(
        CREATE TABLE IF NOT EXISTS w_data (
            id INT AUTO_INCREMENT PRIMARY KEY,
            device_id VARCHAR(20) NOT NULL,
            value DOUBLE NOT NULL,
            time DATETIME NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_device_time (device_id, time)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT 'W设备数据表';
    )";
    if (!wQuery.exec(createWSql)) {
        qCritical() << "创建W设备数据表失败：" << wQuery.lastError().text();
    }

    // 创建T设备数据表（不存在则创建）
    QSqlQuery tQuery(m_db);
    QString createTSql = R"(
        CREATE TABLE IF NOT EXISTS t_data (
            id INT AUTO_INCREMENT PRIMARY KEY,
            device_id VARCHAR(20) NOT NULL,
            value DOUBLE NOT NULL,
            time DATETIME NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_device_time (device_id, time)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT 'T设备数据表';
    )";
    if (!tQuery.exec(createTSql)) {
        qCritical() << "创建T设备数据表失败：" << tQuery.lastError().text();
    }

    // 创建E设备数据表（不存在则创建）
    QSqlQuery eQuery(m_db);
    QString createESql = R"(
        CREATE TABLE IF NOT EXISTS e_data (
            id INT AUTO_INCREMENT PRIMARY KEY,
            device_id VARCHAR(20) NOT NULL,
            value DOUBLE NOT NULL,
            time DATETIME NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_device_time (device_id, time)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT 'E设备数据表';
    )";
    if (!eQuery.exec(createESql)) {
        qCritical() << "创建E设备数据表失败：" << eQuery.lastError().text();
    }

    // 创建尘埃数据表（不存在则创建）
    QSqlQuery dustQuery(m_db);
    QString createDustSql = R"(
        CREATE TABLE IF NOT EXISTS dust_data (
            id INT AUTO_INCREMENT PRIMARY KEY,
            index_id INT NOT NULL,
            value DOUBLE NOT NULL,
            time DATETIME NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_index_time (index_id, time)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT '尘埃数据表';
    )";
    if (!dustQuery.exec(createDustSql)) {
        qCritical() << "创建尘埃数据表失败：" << dustQuery.lastError().text();
    }

    // 创建合格率数据表（不存在则创建）
    QSqlQuery rateQuery(m_db);
    QString createRateSql = R"(
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
    if (!rateQuery.exec(createRateSql)) {
        qCritical() << "创建合格率数据表失败：" << rateQuery.lastError().text();
    }

    return true;
}

bool DBManager::addDevice(const QString& devId, const QString& devType)
{
    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return false;
    }

    QSqlQuery query(m_db);

    if (deviceExists(devId)) {
        return true; // 设备已存在
    }

    query.prepare("INSERT INTO devices (device_id, device_type) VALUES (:devId, :devType)");
    query.bindValue(":devId", devId);
    query.bindValue(":devType", devType);

    if (!query.exec()) {
        qCritical() << "添加设备失败：" << query.lastError().text();
        return false;
    }

    return true;
}

bool DBManager::deviceExists(const QString& devId)
{
    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT COUNT(*) FROM devices WHERE device_id = :devId");
    query.bindValue(":devId", devId);

    if (!query.exec()) {
        qCritical() << "查询设备是否存在失败：" << query.lastError().text();
        return false;
    }

    if (query.next()) {
        return query.value(0).toInt() > 0;
    }

    return false;
}

bool DBManager::insertData(const QString& devId, double parsedValue)
{
    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return false;
    }

    QSqlQuery query(m_db);

    // 确定设备类型对应的表
    QString tableName;
    if (devId.startsWith("W")) {
        tableName = "w_data";
    } else if (devId.startsWith("T")) {
        tableName = "t_data";
    } else if (devId.startsWith("E")) {
        tableName = "e_data";
    } else {
        return false; // 未知设备类型
    }

    // 检查设备是否存在，不存在则添加
    if (!deviceExists(devId)) {
        QString devType = devId.left(1); // 取首字母作为设备类型
        if (!addDevice(devId, devType)) {
            return false;
        }
    }

    // 插入数据
    query.prepare(QString("INSERT INTO %1 (device_id, value, time) VALUES (:devId, :value, NOW())").arg(tableName));
    query.bindValue(":devId", devId);
    query.bindValue(":value", parsedValue);

    if (!query.exec()) {
        qCritical() << "插入数据失败：" << query.lastError().text();
        return false;
    }

    return true;
}

bool DBManager::insertDustData(const QString& devId, const QString& indexId, double parsedValue)
{
    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return false;
    }

    QSqlQuery query(m_db);

    // 检查设备是否存在，不存在则添加
    if (!deviceExists(devId)) {
        if (!addDevice(devId, "C")) {
            return false;
        }
    }

    // 插入数据
    query.prepare("INSERT INTO dust_data (device_id, index_id, value, time) VALUES (:devId, :indexId, :value, NOW())");
    query.bindValue(":devId", devId);
    query.bindValue(":indexId", indexId);
    query.bindValue(":value", parsedValue);

    if (!query.exec()) {
        qCritical() << "插入尘埃数据失败：" << query.lastError().text();
        return false;
    }

    return true;
}

bool DBManager::insertIonFanData(const QString& devId, const QString& status1, const QString& status2)
{
    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return false;
    }

    QSqlQuery query(m_db);

    // 检查设备是否存在，不存在则添加
    if (!deviceExists(devId)) {
        if (!addDevice(devId, "I")) {
            return false;
        }
    }

    // 插入数据
    query.prepare("INSERT INTO ion_fan_data (device_id, status1, status2, time) VALUES (:devId, :status1, :status2, NOW())");
    query.bindValue(":devId", devId);
    query.bindValue(":status1", status1);
    query.bindValue(":status2", status2);

    if (!query.exec()) {
        qCritical() << "插入离子风机数据失败：" << query.lastError().text();
        return false;
    }

    return true;
}

QList<QPair<QDateTime, double>> DBManager::getDustHistoryData(const QString& indexId, const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes)
{
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return result;
    }

    QSqlQuery query(m_db);

    // 构建SQL查询，按时间间隔分组并计算平均值
    QString sql = R"(
        SELECT
            DATE_FORMAT(time, '%Y-%m-%d %H:%i:00') as time_slot,
            AVG(value) as avg_value
        FROM dust_data
        WHERE index_id = :indexId AND time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )";

    query.prepare(sql);
    query.bindValue(":indexId", indexId);
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", endTime);

    if (query.exec()) {
        while (query.next()) {
            QDateTime time = QDateTime::fromString(query.value("time_slot").toString(), "yyyy-MM-dd HH:mm:ss");
            double value = query.value("avg_value").toDouble();
            result.append(qMakePair(time, value));
        }
    } else {
        qCritical() << "查询尘埃历史数据失败：" << query.lastError().text();
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getWHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes)
{
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return result;
    }

    QSqlQuery query(m_db);

    // 构建SQL查询，按时间间隔分组并计算平均值
    QString sql = R"(
        SELECT
            DATE_FORMAT(time, '%Y-%m-%d %H:%i:00') as time_slot,
            AVG(value) as avg_value
        FROM w_data
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )";

    query.prepare(sql);
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", endTime);

    if (query.exec()) {
        while (query.next()) {
            QDateTime time = QDateTime::fromString(query.value("time_slot").toString(), "yyyy-MM-dd HH:mm:ss");
            double value = query.value("avg_value").toDouble();
            result.append(qMakePair(time, value));
        }
    } else {
        qCritical() << "查询W设备历史数据失败：" << query.lastError().text();
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getTHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes)
{
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return result;
    }

    QSqlQuery query(m_db);

    // 构建SQL查询，按时间间隔分组并计算平均值
    QString sql = R"(
        SELECT
            DATE_FORMAT(time, '%Y-%m-%d %H:%i:00') as time_slot,
            AVG(value) as avg_value
        FROM t_data
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )";

    query.prepare(sql);
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", endTime);

    if (query.exec()) {
        while (query.next()) {
            QDateTime time = QDateTime::fromString(query.value("time_slot").toString(), "yyyy-MM-dd HH:mm:ss");
            double value = query.value("avg_value").toDouble();
            result.append(qMakePair(time, value));
        }
    } else {
        qCritical() << "查询T设备历史数据失败：" << query.lastError().text();
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getEHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes)
{
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return result;
    }

    QSqlQuery query(m_db);

    // 构建SQL查询，按时间间隔分组并计算平均值
    QString sql = R"(
        SELECT
            DATE_FORMAT(time, '%Y-%m-%d %H:%i:00') as time_slot,
            AVG(value) as avg_value
        FROM e_data
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )";

    query.prepare(sql);
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", endTime);

    if (query.exec()) {
        while (query.next()) {
            QDateTime time = QDateTime::fromString(query.value("time_slot").toString(), "yyyy-MM-dd HH:mm:ss");
            double value = query.value("avg_value").toDouble();
            result.append(qMakePair(time, value));
        }
    } else {
        qCritical() << "查询E设备历史数据失败：" << query.lastError().text();
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getQualifiedRateData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes)
{
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return result;
    }

    QSqlQuery query(m_db);

    // 构建SQL查询，按时间间隔分组并计算平均值
    QString sql = R"(
        SELECT
            DATE_FORMAT(time, '%Y-%m-%d %H:%i:00') as time_slot,
            AVG((w_qualified_rate + t_qualified_rate + e_qualified_rate) / 3) as avg_rate
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )";

    query.prepare(sql);
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", endTime);

    if (query.exec()) {
        while (query.next()) {
            QDateTime time = QDateTime::fromString(query.value("time_slot").toString(), "yyyy-MM-dd HH:mm:ss");
            double value = query.value("avg_rate").toDouble();
            result.append(qMakePair(time, value));
        }
    } else {
        qCritical() << "查询合格率历史数据失败：" << query.lastError().text();
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getWQualifiedRateData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes)
{
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return result;
    }

    QSqlQuery query(m_db);

    // 构建SQL查询，按时间间隔分组并计算平均值
    QString sql = R"(
        SELECT
            DATE_FORMAT(time, '%Y-%m-%d %H:%i:00') as time_slot,
            AVG(w_qualified_rate) as avg_rate
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )";

    query.prepare(sql);
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", endTime);

    if (query.exec()) {
        while (query.next()) {
            QDateTime time = QDateTime::fromString(query.value("time_slot").toString(), "yyyy-MM-dd HH:mm:ss");
            double value = query.value("avg_rate").toDouble();
            result.append(qMakePair(time, value));
        }
    } else {
        qCritical() << "查询W设备合格率历史数据失败：" << query.lastError().text();
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getTQualifiedRateData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes)
{
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return result;
    }

    QSqlQuery query(m_db);

    // 构建SQL查询，按时间间隔分组并计算平均值
    QString sql = R"(
        SELECT
            DATE_FORMAT(time, '%Y-%m-%d %H:%i:00') as time_slot,
            AVG(t_qualified_rate) as avg_rate
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )";

    query.prepare(sql);
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", endTime);

    if (query.exec()) {
        while (query.next()) {
            QDateTime time = QDateTime::fromString(query.value("time_slot").toString(), "yyyy-MM-dd HH:mm:ss");
            double value = query.value("avg_rate").toDouble();
            result.append(qMakePair(time, value));
        }
    } else {
        qCritical() << "查询T设备合格率历史数据失败：" << query.lastError().text();
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getEQualifiedRateData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes)
{
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return result;
    }

    QSqlQuery query(m_db);

    // 构建SQL查询，按时间间隔分组并计算平均值
    QString sql = R"(
        SELECT
            DATE_FORMAT(time, '%Y-%m-%d %H:%i:00') as time_slot,
            AVG(e_qualified_rate) as avg_rate
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )";

    query.prepare(sql);
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", endTime);

    if (query.exec()) {
        while (query.next()) {
            QDateTime time = QDateTime::fromString(query.value("time_slot").toString(), "yyyy-MM-dd HH:mm:ss");
            double value = query.value("avg_rate").toDouble();
            result.append(qMakePair(time, value));
        }
    } else {
        qCritical() << "查询E设备合格率历史数据失败：" << query.lastError().text();
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getTempHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes)
{
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_mutex);

    // 发送查询开始日志
    QString logMessage = QString("查询温度历史数据：开始时间=%1, 结束时间=%2, 间隔=%3分钟").arg(startTime.toString("yyyy-MM-dd HH:mm:ss")).arg(endTime.toString("yyyy-MM-dd HH:mm:ss")).arg(intervalMinutes);
    emit logGenerated("DBManager", logMessage);
    writeLogToFile("DBManager", logMessage);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        QString errorLog = "数据库连接失败，无法查询温度历史数据";
        emit logGenerated("DBManager", errorLog);
        writeLogToFile("DBManager", errorLog);
        return result;
    }

    QSqlQuery query(m_db);

    // 构建SQL查询，按时间间隔分组并计算平均值
    QString sql = R"(
        SELECT
            DATE_FORMAT(DATE_ADD(time, INTERVAL -MINUTE(time) % :interval MINUTE), '%Y-%m-%d %H:%i:00') as time_slot,
            AVG(avg_temperature) as avg_value
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )";

    query.prepare(sql);
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", endTime);
    query.bindValue(":interval", intervalMinutes);

    if (query.exec()) {
        while (query.next()) {
            QDateTime time = QDateTime::fromString(query.value("time_slot").toString(), "yyyy-MM-dd HH:mm:ss");
            double value = query.value("avg_value").toDouble();
            result.append(qMakePair(time, value));
        }
        QString successLog = QString("温度历史数据查询成功，共获取%1条数据").arg(result.size());
        emit logGenerated("DBManager", successLog);
        writeLogToFile("DBManager", successLog);
    } else {
        QString errorLog = QString("查询温度历史数据失败：%1").arg(query.lastError().text());
        emit logGenerated("DBManager", errorLog);
        writeLogToFile("DBManager", errorLog);
        qCritical() << errorLog;
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getHumidityHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes)
{
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_mutex);

    // 发送查询开始日志
    QString logMessage = QString("查询湿度历史数据：开始时间=%1, 结束时间=%2, 间隔=%3分钟").arg(startTime.toString("yyyy-MM-dd HH:mm:ss")).arg(endTime.toString("yyyy-MM-dd HH:mm:ss")).arg(intervalMinutes);
    emit logGenerated("DBManager", logMessage);
    writeLogToFile("DBManager", logMessage);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        QString errorLog = "数据库连接失败，无法查询湿度历史数据";
        emit logGenerated("DBManager", errorLog);
        writeLogToFile("DBManager", errorLog);
        return result;
    }

    QSqlQuery query(m_db);

    // 构建SQL查询，按时间间隔分组并计算平均值
    QString sql = R"(
        SELECT
            DATE_FORMAT(DATE_ADD(time, INTERVAL -MINUTE(time) % :interval MINUTE), '%Y-%m-%d %H:%i:00') as time_slot,
            AVG(avg_humidity) as avg_value
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )";

    query.prepare(sql);
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", endTime);
    query.bindValue(":interval", intervalMinutes);

    if (query.exec()) {
        while (query.next()) {
            QDateTime time = QDateTime::fromString(query.value("time_slot").toString(), "yyyy-MM-dd HH:mm:ss");
            double value = query.value("avg_value").toDouble();
            result.append(qMakePair(time, value));
        }
        QString successLog = QString("湿度历史数据查询成功，共获取%1条数据").arg(result.size());
        emit logGenerated("DBManager", successLog);
        writeLogToFile("DBManager", successLog);
    } else {
        QString errorLog = QString("查询湿度历史数据失败：%1").arg(query.lastError().text());
        emit logGenerated("DBManager", errorLog);
        writeLogToFile("DBManager", errorLog);
        qCritical() << errorLog;
    }

    return result;
}

QList<QPair<QDateTime, double>> DBManager::getCleanlinessHistoryData(const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes)
{
    QList<QPair<QDateTime, double>> result;

    QMutexLocker locker(&m_mutex);

    // 发送查询开始日志
    QString logMessage = QString("查询洁净度历史数据：开始时间=%1, 结束时间=%2, 间隔=%3分钟").arg(startTime.toString("yyyy-MM-dd HH:mm:ss")).arg(endTime.toString("yyyy-MM-dd HH:mm:ss")).arg(intervalMinutes);
    emit logGenerated("DBManager", logMessage);
    writeLogToFile("DBManager", logMessage);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        QString errorLog = "数据库连接失败，无法查询洁净度历史数据";
        emit logGenerated("DBManager", errorLog);
        writeLogToFile("DBManager", errorLog);
        return result;
    }

    QSqlQuery query(m_db);

    // 构建SQL查询，按时间间隔分组并计算平均值
    QString sql = R"(
        SELECT
            DATE_FORMAT(DATE_ADD(time, INTERVAL -MINUTE(time) % :interval MINUTE), '%Y-%m-%d %H:%i:00') as time_slot,
            AVG(avg_cleanliness) as avg_value
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        GROUP BY time_slot
        ORDER BY time_slot
    )";

    query.prepare(sql);
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", endTime);
    query.bindValue(":interval", intervalMinutes);

    if (query.exec()) {
        while (query.next()) {
            QDateTime time = QDateTime::fromString(query.value("time_slot").toString(), "yyyy-MM-dd HH:mm:ss");
            double value = query.value("avg_value").toDouble();
            result.append(qMakePair(time, value));
        }
        QString successLog = QString("洁净度历史数据查询成功，共获取%1条数据").arg(result.size());
        emit logGenerated("DBManager", successLog);
        writeLogToFile("DBManager", successLog);
    } else {
        QString errorLog = QString("查询洁净度历史数据失败：%1").arg(query.lastError().text());
        emit logGenerated("DBManager", errorLog);
        writeLogToFile("DBManager", errorLog);
        qCritical() << errorLog;
    }

    return result;
}

QMap<QDateTime, QMap<QString, double>> DBManager::getHistoryChartData()
{
    QMap<QDateTime, QMap<QString, double>> result;

    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return result;
    }

    QSqlQuery query(m_db);

    // 计算开始时间（24小时前）
    QDateTime endTime = QDateTime::currentDateTime();
    QDateTime startTime = endTime.addDays(-1);

    // 发送查询开始日志到textBrowser
    QString logMessage = QString("数据库已开始查询历史图表数据：开始时间=%1, 结束时间=%2").arg(startTime.toString("yyyy-MM-dd HH:mm:ss")).arg(endTime.toString("yyyy-MM-dd HH:mm:ss"));
    emit logGenerated("DBManager", logMessage);
    writeLogToFile("DBManager", logMessage);

    // 构建SQL查询
    QString sql = R"(
        SELECT
            time,
            w_qualified_rate,
            t_qualified_rate,
            e_qualified_rate,
            avg_temperature,
            avg_humidity,
            avg_cleanliness
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        ORDER BY time
    )";

    query.prepare(sql);
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", endTime);

    if (query.exec()) {
        // 存储所有数据点
        QList<QPair<QDateTime, QMap<QString, double>>> allData;

        while (query.next()) {
            QDateTime time = query.value("time").toDateTime();
            double wRate = query.value("w_qualified_rate").toDouble();
            double tRate = query.value("t_qualified_rate").toDouble();
            double eRate = query.value("e_qualified_rate").toDouble();
            double avgTemp = query.value("avg_temperature").toDouble();
            double avgHumidity = query.value("avg_humidity").toDouble();
            double avgCleanliness = query.value("avg_cleanliness").toDouble();

            QMap<QString, double> dataMap;
            dataMap["腕带合格率"] = wRate;
            dataMap["台垫合格率"] = tRate;
            dataMap["设备合格率"] = eRate;
            dataMap["平均温度"] = avgTemp;
            dataMap["平均湿度"] = avgHumidity;
            dataMap["平均洁净度"] = avgCleanliness;
            allData.append(qMakePair(time, dataMap));
        }

        // 每隔5个时间点取一个数据
        for (int i = 0; i < allData.size(); i += 5) {
            result[allData[i].first] = allData[i].second;
        }

        // 确保至少有一个数据点
        if (result.isEmpty() && !allData.isEmpty()) {
            result[allData.last().first] = allData.last().second;
        }
    } else {
        qCritical() << "查询历史图表数据失败：" << query.lastError().text();
    }

    return result;
}

QJsonObject DBManager::getHistoryChartData(const QString& timeRange)
{
    QJsonObject result;

    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return result;
    }

    QSqlQuery query(m_db);

    // 根据时间范围计算开始时间
    QDateTime endTime = QDateTime::currentDateTime();
    QDateTime startTime;

    if (timeRange == "1h") {
        startTime = endTime.addSecs(-1 * 60 * 60); // 1小时 = 3600秒
    } else if (timeRange == "24h") {
        startTime = endTime.addDays(-1);
    } else if (timeRange == "7d") {
        startTime = endTime.addDays(-7);
    } else {
        // 默认24小时
        startTime = endTime.addDays(-1);
    }

    // 发送查询开始日志到textBrowser
    QString logMessage = QString("数据库已开始查询历史图表数据：时间范围=%1, 开始时间=%2, 结束时间=%3").arg(timeRange).arg(startTime.toString("yyyy-MM-dd HH:mm:ss")).arg(endTime.toString("yyyy-MM-dd HH:mm:ss"));
    emit logGenerated("DBManager", logMessage);
    writeLogToFile("DBManager", logMessage);

    // 查询合格表率和环境数据
    QString sql = R"(
        SELECT
            time,
            w_qualified_rate,
            t_qualified_rate,
            e_qualified_rate,
            avg_temperature,
            avg_humidity,
            avg_cleanliness
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
        ORDER BY time
    )";

    query.prepare(sql);
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", endTime);

    if (query.exec()) {
        // 存储所有数据点
        QJsonArray timeArray;
        QJsonArray wRateArray;
        QJsonArray tRateArray;
        QJsonArray eRateArray;
        QJsonArray tempArray;
        QJsonArray humidityArray;
        QJsonArray cleanlinessArray;

        // 计算平均值
        double totalWRate = 0.0;
        double totalTRate = 0.0;
        double totalERate = 0.0;
        double totalTemp = 0.0;
        double totalHumidity = 0.0;
        double totalCleanliness = 0.0;
        int count = 0;

        while (query.next()) {
            QDateTime time = query.value("time").toDateTime();
            double wRate = query.value("w_qualified_rate").toDouble();
            double tRate = query.value("t_qualified_rate").toDouble();
            double eRate = query.value("e_qualified_rate").toDouble();
            double temp = query.value("avg_temperature").toDouble();
            double humidity = query.value("avg_humidity").toDouble();
            double cleanliness = query.value("avg_cleanliness").toDouble();

            // 累加计算
            totalWRate += wRate;
            totalTRate += tRate;
            totalERate += eRate;
            totalTemp += temp;
            totalHumidity += humidity;
            totalCleanliness += cleanliness;
            count++;

            timeArray.append(time.toString("yyyy-MM-dd HH:mm:ss"));
            wRateArray.append(wRate);
            tRateArray.append(tRate);
            eRateArray.append(eRate);
            tempArray.append(temp);
            humidityArray.append(humidity);
            cleanlinessArray.append(cleanliness);
        }

        // 计算平均值
        double avgWRate = count > 0 ? totalWRate / count : 0.0;
        double avgTRate = count > 0 ? totalTRate / count : 0.0;
        double avgERate = count > 0 ? totalERate / count : 0.0;
        double avgTemp = count > 0 ? totalTemp / count : 0.0;
        double avgHumidity = count > 0 ? totalHumidity / count : 0.0;
        double avgCleanliness = count > 0 ? totalCleanliness / count : 0.0;

        // 构建结果对象
        result["time"] = timeArray;
        result["wRate"] = wRateArray;
        result["tRate"] = tRateArray;
        result["eRate"] = eRateArray;
        result["temperature"] = tempArray;
        result["humidity"] = humidityArray;
        result["cleanliness"] = cleanlinessArray;

        // 输出计算结果到日志
        qDebug() << QString("历史数据计算结果：时间范围=%1, 数据点数=%2, 腕带合格率平均值=%3%, 台垫合格率平均值=%4%, 设备合格率平均值=%5%, 平均温度=%6℃, 平均湿度=%7%RH, 平均洁净度=%8")
            .arg(timeRange)
            .arg(count)
            .arg(QString::number(avgWRate, 'f', 2))
            .arg(QString::number(avgTRate, 'f', 2))
            .arg(QString::number(avgERate, 'f', 2))
            .arg(QString::number(avgTemp, 'f', 1))
            .arg(QString::number(avgHumidity, 'f', 1))
            .arg(QString::number(avgCleanliness, 'f', 0));

        // 发送计算结果到textBrowser
        QString logMessage = QString("历史数据计算结果：时间范围=%1, 数据点数=%2, 腕带合格率平均值=%3%, 台垫合格率平均值=%4%, 设备合格率平均值=%5%, 平均温度=%6℃, 平均湿度=%7%RH, 平均洁净度=%8")
            .arg(timeRange)
            .arg(count)
            .arg(QString::number(avgWRate, 'f', 2))
            .arg(QString::number(avgTRate, 'f', 2))
            .arg(QString::number(avgERate, 'f', 2))
            .arg(QString::number(avgTemp, 'f', 1))
            .arg(QString::number(avgHumidity, 'f', 1))
            .arg(QString::number(avgCleanliness, 'f', 0));
        emit logGenerated("DBManager", logMessage);
        writeLogToFile("DBManager", logMessage);
    } else {
        qCritical() << "查询历史图表数据失败：" << query.lastError().text();
    }

    return result;
}

QVector<double> DBManager::getAverageDataFromTimeRange(const QDateTime& startTime, int minutesRange)
{
    QVector<double> result(6, 0.0); // 0: w合格率, 1: t合格率, 2: e合格率, 3: 平均温度, 4: 平均湿度, 5: 平均洁净度

    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return result;
    }

    QSqlQuery query(m_db);

    QDateTime endTime = startTime.addSecs(minutesRange * 60);

    // 发送查询开始日志到textBrowser
    QString logMessage = QString("数据库已开始查询时间范围平均数据：开始时间=%1, 结束时间=%2").arg(startTime.toString("yyyy-MM-dd HH:mm:ss")).arg(endTime.toString("yyyy-MM-dd HH:mm:ss"));
    emit logGenerated("DBManager", logMessage);
    writeLogToFile("DBManager", logMessage);

    // 查询指定时间范围内的平均值
    QString sql = R"(
        SELECT
            AVG(w_qualified_rate) as avg_w_rate,
            AVG(t_qualified_rate) as avg_t_rate,
            AVG(e_qualified_rate) as avg_e_rate,
            AVG(avg_temperature) as avg_temp,
            AVG(avg_humidity) as avg_humidity,
            AVG(avg_cleanliness) as avg_cleanliness
        FROM qualified_rate
        WHERE time >= :startTime AND time <= :endTime
    )";

    query.prepare(sql);
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", endTime);

    if (query.exec()) {
        if (query.next()) {
            result[0] = query.value("avg_w_rate").toDouble();
            result[1] = query.value("avg_t_rate").toDouble();
            result[2] = query.value("avg_e_rate").toDouble();
            result[3] = query.value("avg_temp").toDouble();
            result[4] = query.value("avg_humidity").toDouble();
            result[5] = query.value("avg_cleanliness").toDouble();

            // 发送计算结果到textBrowser
            QString logMessage = QString("时间范围计算结果：开始时间=%1, 结束时间=%2, 腕带合格率平均值=%3%%, 台垫合格率平均值=%4%%, 设备合格率平均值=%5%%, 平均温度=%6℃, 平均湿度=%7%%RH, 平均洁净度=%8").arg(startTime.toString("yyyy-MM-dd HH:mm:ss")).arg(endTime.toString("yyyy-MM-dd HH:mm:ss")).arg(result[0], 0, 'f', 2).arg(result[1], 0, 'f', 2).arg(result[2], 0, 'f', 2).arg(result[3], 0, 'f', 1).arg(result[4], 0, 'f', 1).arg(result[5], 0, 'f', 0);
            emit logGenerated("DBManager", logMessage);
            writeLogToFile("DBManager", logMessage);
        } else {
            // 没有数据返回
            QString logMessage = QString("查询时间范围平均数据：没有找到数据，开始时间=%1, 结束时间=%2").arg(startTime.toString("yyyy-MM-dd HH:mm:ss")).arg(endTime.toString("yyyy-MM-dd HH:mm:ss"));
            emit logGenerated("DBManager", logMessage);
            writeLogToFile("DBManager", logMessage);
            qCritical() << "查询时间范围平均数据：没有找到数据";
        }
    } else {
        // 查询执行失败
        QString logMessage = QString("查询时间范围平均数据失败：%1").arg(query.lastError().text());
        emit logGenerated("DBManager", logMessage);
        writeLogToFile("DBManager", logMessage);
        qCritical() << "查询时间范围平均数据失败：" << query.lastError().text();
    }

    return result;
}

// 获取最近一次轮询的统计数据（用于计算合格率）
QMap<QString, QPair<int, int>> DBManager::getPollingStatistics(const QDateTime& time, int minutesRange)
{
    QMap<QString, QPair<int, int>> result;

    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return result;
    }

    QSqlQuery query(m_db);
    QDateTime startTime = time.addSecs(-minutesRange * 60);

    // 查询W设备统计
    query.prepare(R"(
        SELECT
            COUNT(*) as total_count,
            SUM(CASE WHEN status = '1' THEN 1 ELSE 0 END) as qualified_count
        FROM w_data
        WHERE time >= :startTime AND time <= :endTime
    )");
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", time);

    if (query.exec() && query.next()) {
        int total = query.value("total_count").toInt();
        int qualified = query.value("qualified_count").toInt();
        result["W"] = qMakePair(total, qualified);
    }

    // 查询T设备统计
    query.prepare(R"(
        SELECT
            COUNT(*) as total_count,
            SUM(CASE WHEN status = '1' THEN 1 ELSE 0 END) as qualified_count
        FROM t_data
        WHERE time >= :startTime AND time <= :endTime
    )");
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", time);

    if (query.exec() && query.next()) {
        int total = query.value("total_count").toInt();
        int qualified = query.value("qualified_count").toInt();
        result["T"] = qMakePair(total, qualified);
    }

    // 查询E设备统计
    query.prepare(R"(
        SELECT
            COUNT(*) as total_count,
            SUM(CASE WHEN status = '1' THEN 1 ELSE 0 END) as qualified_count
        FROM e_data
        WHERE time >= :startTime AND time <= :endTime
    )");
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", time);

    if (query.exec() && query.next()) {
        int total = query.value("total_count").toInt();
        int qualified = query.value("qualified_count").toInt();
        result["E"] = qMakePair(total, qualified);
    }

    return result;
}

// 获取最近一次轮询的平均环境数据（温度、湿度、洁净度）
QVector<double> DBManager::getPollingEnvData(const QDateTime& time, int minutesRange)
{
    QVector<double> result(3, 0.0); // 0: 平均温度, 1: 平均湿度, 2: 平均洁净度

    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return result;
    }

    // 暂时返回默认值，不查询dust_data表的数据
    // 后续可以根据需要从qualified_rate表查询历史数据进行统计
    
    // 返回默认值：温度=0, 湿度=0, 洁净度=0
    result[0] = 0.0; // 温度
    result[1] = 0.0; // 湿度
    result[2] = 0.0; // 洁净度

    return result;
}

// AsyncQueryTask 类的实现
DBManager::AsyncQueryTask::AsyncQueryTask(DBManager* manager, QueryType type, const QDateTime& startTime, const QDateTime& endTime, int intervalMinutes)
    : m_manager(manager), m_type(type), m_startTime(startTime), m_endTime(endTime), m_intervalMinutes(intervalMinutes), m_minutesRange(0)
{
    setAutoDelete(true);
}

DBManager::AsyncQueryTask::AsyncQueryTask(DBManager* manager, const QDateTime& startTime, int minutesRange)
    : m_manager(manager), m_type(AverageData), m_startTime(startTime), m_endTime(QDateTime()), m_intervalMinutes(0), m_minutesRange(minutesRange)
{
    setAutoDelete(true);
}

void DBManager::AsyncQueryTask::run()
{
    switch (m_type) {
    case TempHistory: {
        QList<QPair<QDateTime, double>> data = m_manager->getTempHistoryData(m_startTime, m_endTime, m_intervalMinutes);
        emit m_manager->tempHistoryDataReady(data);
        break;
    }
    case HumidityHistory: {
        QList<QPair<QDateTime, double>> data = m_manager->getHumidityHistoryData(m_startTime, m_endTime, m_intervalMinutes);
        emit m_manager->humidityHistoryDataReady(data);
        break;
    }
    case CleanlinessHistory: {
        QList<QPair<QDateTime, double>> data = m_manager->getCleanlinessHistoryData(m_startTime, m_endTime, m_intervalMinutes);
        emit m_manager->cleanlinessHistoryDataReady(data);
        break;
    }
    case AverageData: {
        QVector<double> data = m_manager->getAverageDataFromTimeRange(m_startTime, m_minutesRange);
        emit m_manager->averageDataReady(data);
        break;
    }
    }
}

// 检查并重新连接数据库
bool DBManager::checkAndReconnect()
{
    // 先检查连接状态
    if (m_isConnected && m_db.isOpen()) {
        // 测试连接是否有效
        QSqlQuery testQuery(m_db);
        if (testQuery.exec("SELECT 1")) {
            return true; // 连接正常，直接返回
        }
    }

    // 连接异常，尝试重新连接（最多重试3次）
    const int MAX_RETRY = 3;
    for (int retry = 0; retry < MAX_RETRY; retry++) {
        qDebug() << "数据库连接异常，尝试重新连接... (尝试" << retry + 1 << "/" << MAX_RETRY << ")";
        QString logMessage = QString("数据库连接异常，尝试重新连接... (尝试%1/%2)").arg(retry + 1).arg(MAX_RETRY);
        emit logGenerated("DBManager", logMessage);
        writeLogToFile("DBManager", logMessage);

        // 关闭旧连接
        if (m_db.isOpen()) {
            m_db.close();
        }

        // 重新初始化数据库连接
        m_db = QSqlDatabase::addDatabase("QMYSQL", "dbManagerConnection");
        m_db.setHostName(m_host);
        m_db.setPort(m_port);
        m_db.setUserName(m_user);
        m_db.setPassword(m_password);
        m_db.setDatabaseName(m_dbName);
        m_db.setConnectOptions("MYSQL_OPT_CONNECT_TIMEOUT=5");

        if (m_db.open()) {
            m_isConnected = true;
            logMessage = "数据库重新连接成功";
            emit logGenerated("DBManager", logMessage);
            writeLogToFile("DBManager", logMessage);
            return true;
        } else {
            logMessage = "数据库重新连接失败：" + m_db.lastError().text();
            emit logGenerated("DBManager", logMessage);
            writeLogToFile("DBManager", logMessage);

            // 重试间隔
            if (retry < MAX_RETRY - 1) {
                QThread::msleep(1000); // 等待1秒后重试
            }
        }
    }

    m_isConnected = false;
    return false;
}

// W/T/E数据插入（完整数据：标识+时间+值+状态+状态描述）
bool DBManager::insertWteData(const QString& deviceType, const QString& deviceId,
                             const QDateTime& recordTime, uint32_t value,
                             const QString& status, const QString& statusDesc)
{
    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return false;
    }

    QSqlQuery query(m_db);

    // 确定设备类型对应的表
    QString tableName;
    if (deviceType == "W") {
        tableName = "w_data";
    } else if (deviceType == "T") {
        tableName = "t_data";
    } else if (deviceType == "E") {
        tableName = "e_data";
    } else {
        return false; // 未知设备类型
    }

    // 检查设备是否存在，不存在则添加
    if (!deviceExists(deviceId)) {
        if (!addDevice(deviceId, deviceType)) {
            return false;
        }
    }

    // 插入数据
    query.prepare(QString("INSERT INTO %1 (device_id, record_time, value, status, status_desc) VALUES (:deviceId, :recordTime, :value, :status, :statusDesc)").arg(tableName));
    query.bindValue(":deviceId", deviceId);
    query.bindValue(":recordTime", recordTime);
    query.bindValue(":value", value);
    query.bindValue(":status", status);
    query.bindValue(":statusDesc", statusDesc);

    if (!query.exec()) {
        qCritical() << "插入" << deviceType << "设备数据失败：" << query.lastError().text();
        return false;
    }

    return true;
}

// 尘埃数据插入（完整数据）
bool DBManager::insertDustData(const QString& deviceId, const QDateTime& recordTime,
                             const QString& temp, const QString& humidity,
                             const QString& dust03, const QString& dust05,
                             const QString& dust10, const QString& dust25,
                             const QString& dust50, const QString& dust10Total)
{
    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return false;
    }

    QSqlQuery query(m_db);

    // 检查设备是否存在，不存在则添加
    if (!deviceExists(deviceId)) {
        if (!addDevice(deviceId, "C")) {
            return false;
        }
    }

    // 插入数据
    query.prepare("INSERT INTO dust_data (device_id, record_time, temp, humidity, dust_03um, dust_05um, dust_10um, dust_25um, dust_50um, dust_10um_total) VALUES (:deviceId, :recordTime, :temp, :humidity, :dust03, :dust05, :dust10, :dust25, :dust50, :dust10total)");
    query.bindValue(":deviceId", deviceId);
    query.bindValue(":recordTime", recordTime);
    query.bindValue(":temp", temp);
    query.bindValue(":humidity", humidity);
    query.bindValue(":dust03", dust03);
    query.bindValue(":dust05", dust05);
    query.bindValue(":dust10", dust10);
    query.bindValue(":dust25", dust25);
    query.bindValue(":dust50", dust50);
    query.bindValue(":dust10total", dust10Total);

    if (!query.exec()) {
        qCritical() << "插入尘埃数据失败：" << query.lastError().text();
        return false;
    }

    return true;
}

// 插入合格率数据
bool DBManager::insertQualifiedRateData(const QDateTime& time, double wRate, double tRate, double eRate,
                                      double avgTemp, double avgHumidity, double avgCleanliness)
{
    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return false;
    }

    QSqlQuery query(m_db);

    // 插入合格率数据
    query.prepare("INSERT INTO qualified_rate (time, w_qualified_rate, t_qualified_rate, e_qualified_rate, avg_temperature, avg_humidity, avg_cleanliness) VALUES (:time, :w_rate, :t_rate, :e_rate, :temp, :humidity, :cleanliness)");
    query.bindValue(":time", time);
    query.bindValue(":w_rate", wRate);
    query.bindValue(":t_rate", tRate);
    query.bindValue(":e_rate", eRate);
    query.bindValue(":temp", avgTemp);
    query.bindValue(":humidity", avgHumidity);
    query.bindValue(":cleanliness", avgCleanliness);

    if (!query.exec()) {
        qCritical() << "插入合格率数据失败：" << query.lastError().text();
        return false;
    }

    return true;
}

// 接收WTE数据并异步处理
void DBManager::handleWteData(const QString& deviceType, const QString& deviceId,
                            const QDateTime& recordTime, uint32_t value,
                            const QString& status, const QString& statusDesc)
{
    // 添加日志，确认信号接收
    QString logMessage = QString("收到WTE数据：类型=%1, 设备ID=%2, 值=%3, 状态=%4").arg(deviceType).arg(deviceId).arg(value).arg(status);
    emit logGenerated("DBManager", logMessage);
    writeLogToFile("DBManager", logMessage);

    // 暂时注释掉WTE数据插入操作，只保留qualified_rate表的操作
    // 后续可以根据需要恢复
    /*
    // 使用线程池异步执行数据库操作，控制线程数量
    struct WteDataTask : public QRunnable {
        DBManager* manager;
        QString deviceType;
        QString deviceId;
        QDateTime recordTime;
        uint32_t value;
        QString status;
        QString statusDesc;

        WteDataTask(DBManager* m, const QString& dt, const QString& did, const QDateTime& rt, uint32_t v, const QString& s, const QString& sd)
            : manager(m), deviceType(dt), deviceId(did), recordTime(rt), value(v), status(s), statusDesc(sd) {
            setAutoDelete(true);
        }

        void run() override {
            bool success = manager->insertWteData(deviceType, deviceId, recordTime, value, status, statusDesc);
            if (success) {
                QString successLog = QString("WTE数据插入成功：类型=%1, 设备ID=%2").arg(deviceType).arg(deviceId);
                emit manager->logGenerated("DBManager", successLog);
                manager->writeLogToFile("DBManager", successLog);
            } else {
                QString errorLog = QString("WTE数据插入失败：类型=%1, 设备ID=%2").arg(deviceType).arg(deviceId);
                emit manager->logGenerated("DBManager", errorLog);
                manager->writeLogToFile("DBManager", errorLog);
            }
        }
    };

    WteDataTask* task = new WteDataTask(this, deviceType, deviceId, recordTime, value, status, statusDesc);
    m_threadPool->start(task);
    */
}

// 接收尘埃数据并异步处理
void DBManager::handleDustData(const QString& deviceId, const QDateTime& recordTime,
                             const QString& temp, const QString& humidity,
                             const QString& dust03, const QString& dust05,
                             const QString& dust10, const QString& dust25,
                             const QString& dust50, const QString& dust10Total)
{
    // 添加日志，确认信号接收
    QString logMessage = QString("收到尘埃数据：设备ID=%1, 温度=%2, 湿度=%3").arg(deviceId).arg(temp).arg(humidity);
    emit logGenerated("DBManager", logMessage);
    writeLogToFile("DBManager", logMessage);

    // 暂时注释掉尘埃数据插入操作，只保留qualified_rate表的操作
    // 后续可以根据需要恢复
    /*
    // 使用线程池异步执行数据库操作，控制线程数量
    struct DustDataTask : public QRunnable {
        DBManager* manager;
        QString deviceId;
        QDateTime recordTime;
        QString temp;
        QString humidity;
        QString dust03;
        QString dust05;
        QString dust10;
        QString dust25;
        QString dust50;
        QString dust10Total;

        DustDataTask(DBManager* m, const QString& did, const QDateTime& rt, const QString& t, const QString& h,
                   const QString& d03, const QString& d05, const QString& d10, const QString& d25,
                   const QString& d50, const QString& d10t)
            : manager(m), deviceId(did), recordTime(rt), temp(t), humidity(h), dust03(d03), dust05(d05),
              dust10(d10), dust25(d25), dust50(d50), dust10Total(d10t) {
            setAutoDelete(true);
        }

        void run() override {
            bool success = manager->insertDustData(deviceId, recordTime, temp, humidity, dust03, dust05, dust10, dust25, dust50, dust10Total);
            if (success) {
                QString successLog = QString("尘埃数据插入成功：设备ID=%1").arg(deviceId);
                emit manager->logGenerated("DBManager", successLog);
                manager->writeLogToFile("DBManager", successLog);
            } else {
                QString errorLog = QString("尘埃数据插入失败：设备ID=%1").arg(deviceId);
                emit manager->logGenerated("DBManager", errorLog);
                manager->writeLogToFile("DBManager", errorLog);
            }
        }
    };

    DustDataTask* task = new DustDataTask(this, deviceId, recordTime, temp, humidity, dust03, dust05, dust10, dust25, dust50, dust10Total);
    m_threadPool->start(task);
    */
}

// 接收合格率数据并异步处理
void DBManager::handleQualifiedRateData(const QDateTime& time, double wRate, double tRate, double eRate,
                                     double avgTemp, double avgHumidity, double avgCleanliness)
{
    // 添加日志，确认信号接收
    QString logMessage = QString("收到合格率数据：时间=%1, 腕带=%2%, 台垫=%3%, 设备=%4%, 温度=%5℃, 湿度=%6%RH, 洁净度=%7")
        .arg(time.toString("yyyy-MM-dd HH:mm:ss"))
        .arg(QString::number(wRate * 100, 'f', 2))
        .arg(QString::number(tRate * 100, 'f', 2))
        .arg(QString::number(eRate * 100, 'f', 2))
        .arg(QString::number(avgTemp, 'f', 1))
        .arg(QString::number(avgHumidity, 'f', 1))
        .arg(QString::number(avgCleanliness, 'f', 0));
    emit logGenerated("DBManager", logMessage);
    writeLogToFile("DBManager", logMessage);

    if (qFuzzyIsNull(avgTemp) || qFuzzyIsNull(avgHumidity)) {
        QString skipLog = QString("⏭ 跳过插入：平均温度或平均湿度无效（温度=%1℃, 湿度=%2%RH）")
            .arg(QString::number(avgTemp, 'f', 1))
            .arg(QString::number(avgHumidity, 'f', 1));
        emit logGenerated("DBManager", skipLog);
        writeLogToFile("DBManager", skipLog);
        return;
    }

    // 使用线程池异步执行数据库操作，控制线程数量
    struct QualifiedRateTask : public QRunnable {
        DBManager* manager;
        QDateTime time;
        double wRate;
        double tRate;
        double eRate;
        double avgTemp;
        double avgHumidity;
        double avgCleanliness;

        QualifiedRateTask(DBManager* m, const QDateTime& t, double w, double t_, double e, double temp, double humidity, double cleanliness)
            : manager(m), time(t), wRate(w), tRate(t_), eRate(e), avgTemp(temp), avgHumidity(humidity), avgCleanliness(cleanliness) {
            setAutoDelete(true);
        }

        void run() override {
            // 添加插入前的标志
            emit manager->logGenerated("DBManager", "🔄 开始插入合格率数据到数据库...");
            manager->writeLogToFile("DBManager", "开始插入合格率数据到数据库...");

            bool success = manager->insertQualifiedRateData(time, wRate, tRate, eRate, avgTemp, avgHumidity, avgCleanliness);
            if (success) {
                QString successLog = QString("✅ 合格率数据插入成功：腕带=%1%, 台垫=%2%, 设备=%3%, 温度=%4℃, 湿度=%5%RH, 洁净度=%6")
                    .arg(QString::number(wRate * 100, 'f', 2))
                    .arg(QString::number(tRate * 100, 'f', 2))
                    .arg(QString::number(eRate * 100, 'f', 2))
                    .arg(QString::number(avgTemp, 'f', 1))
                    .arg(QString::number(avgHumidity, 'f', 1))
                    .arg(QString::number(avgCleanliness, 'f', 0));
                emit manager->logGenerated("DBManager", successLog);
                manager->writeLogToFile("DBManager", successLog);
            } else {
                QString errorLog = QString("❌ 合格率数据插入失败");
                emit manager->logGenerated("DBManager", errorLog);
                manager->writeLogToFile("DBManager", errorLog);
            }
        }
    };

    QualifiedRateTask* task = new QualifiedRateTask(this, time, wRate, tRate, eRate, avgTemp, avgHumidity, avgCleanliness);
    m_threadPool->start(task);
}

// 写入日志到文件
void DBManager::writeLogToFile(const QString& workerName, const QString& message)
{
    static QMutex fileMutex;
    QMutexLocker locker(&fileMutex);

    QString appDir = QCoreApplication::applicationDirPath();
    QString logFilePath = appDir + "/logs";

    // 创建logs目录
    QDir logDir(logFilePath);
    if (!logDir.exists()) {
        logDir.mkpath(logFilePath);
    }

    // 创建日志文件名（按日期）
    QString dateStr = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QString logFileName = logFilePath + "/" + dateStr + "_system.log";

    QFile logFile(logFileName);
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&logFile);
        QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        out << "[" << timeStr << "] [" << workerName << "] " << message << "\n";
        logFile.close();
    }
}

bool DBManager::insertAlarmHandling(const QDateTime& handleTime, const QString& handler, const QString& action)
{
    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return false;
    }

    QSqlQuery query(m_db);

    // 插入告警处理记录
    query.prepare("INSERT INTO alarm_handling (handle_time, handler, action) VALUES (:handleTime, :handler, :action)");
    query.bindValue(":handleTime", handleTime);
    query.bindValue(":handler", handler);
    query.bindValue(":action", action);

    if (!query.exec()) {
        qCritical() << "插入告警处理记录失败：" << query.lastError().text();
        return false;
    }

    return true;
}

QList<QMap<QString, QVariant>> DBManager::getAlarmHandlingRecords(const QDateTime& startTime, const QDateTime& endTime)
{
    QList<QMap<QString, QVariant>> result;

    QMutexLocker locker(&m_mutex);

    // 检查并重新连接数据库
    if (!checkAndReconnect()) {
        return result;
    }

    QSqlQuery query(m_db);

    // 查询告警处理记录
    query.prepare("SELECT id, handle_time, handler, action FROM alarm_handling WHERE handle_time >= :startTime AND handle_time <= :endTime ORDER BY handle_time DESC");
    query.bindValue(":startTime", startTime);
    query.bindValue(":endTime", endTime);

    if (query.exec()) {
        while (query.next()) {
            QMap<QString, QVariant> record;
            record["time"] = query.value("handle_time");
            record["person"] = query.value("handler");
            record["thing"] = query.value("action");
            result.append(record);
        }
    } else {
        qCritical() << "查询告警处理记录失败：" << query.lastError().text();
    }

    return result;
}
