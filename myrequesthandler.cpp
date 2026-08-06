
#include "myrequesthandler.h"
#include "mainwindow.h"
#include "dbmanager.h"
#include "staticfilecontroller.h"
#include <QApplication>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStringList>
#include <QDebug>
#include <QMutexLocker>
#include <QPixmap>
#include <QDateTime>
#include <QSettings>
#include <QDir>

using namespace stefanfrings;

namespace {

QString loadMesApiKey()
{
    const QString iniPath = QCoreApplication::applicationDirPath() + QStringLiteral("/mes_api.ini");
    QSettings settings(iniPath, QSettings::IniFormat);
    return settings.value(QStringLiteral("MesApi/apiKey")).toString().trimmed();
}

bool isJsonAll(const QJsonValue& value)
{
    return value.isString() && value.toString().compare(QStringLiteral("ALL"), Qt::CaseInsensitive) == 0;
}

QDateTime parseIsoDateTime(const QString& text, bool* okOut = nullptr)
{
    QDateTime dt = QDateTime::fromString(text, Qt::ISODate);
    if (!dt.isValid()) {
        dt = QDateTime::fromString(text, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    if (!dt.isValid()) {
        dt = QDateTime::fromString(text, QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    }
    if (okOut) {
        *okOut = dt.isValid();
    }
    return dt;
}

void writeMesJson(HttpResponse& response, const QJsonObject& body, int statusCode = 200)
{
    response.setStatus(statusCode, statusCode == 401 ? "Unauthorized" : (statusCode == 400 ? "Bad Request" : "OK"));
    response.setHeader("Content-Type", "application/json; charset=utf-8");
    response.write(QJsonDocument(body).toJson(QJsonDocument::Compact), true);
}

bool validateMesApiKey(const HttpRequest& request, QJsonObject& errorObj)
{
    const QString configuredKey = loadMesApiKey();
    if (configuredKey.isEmpty()) {
        return true;
    }

    const QString providedKey = QString::fromUtf8(request.getHeader("X-Api-Key")).trimmed();
    if (providedKey.isEmpty() || providedKey != configuredKey) {
        errorObj.insert(QStringLiteral("success"), false);
        errorObj.insert(QStringLiteral("code"), QStringLiteral("INVALID_API_KEY"));
        errorObj.insert(QStringLiteral("message"), QStringLiteral("API Key 无效或未提供"));
        return false;
    }
    return true;
}

bool parseIntListOrAll(const QJsonValue& value, bool& allOut, QList<int>& valuesOut,
                       QString& errorMessage)
{
    allOut = false;
    valuesOut.clear();

    if (value.isUndefined() || value.isNull()) {
        allOut = true;
        return true;
    }
    if (isJsonAll(value)) {
        allOut = true;
        return true;
    }
    if (!value.isArray()) {
        errorMessage = QStringLiteral("字段必须是 ALL 或整数数组");
        return false;
    }

    const QJsonArray array = value.toArray();
    if (array.isEmpty()) {
        errorMessage = QStringLiteral("数组不能为空");
        return false;
    }

    for (const QJsonValue& item : array) {
        if (!item.isDouble() && !item.isString()) {
            errorMessage = QStringLiteral("数组中包含无效整数");
            return false;
        }
        bool ok = false;
        const int intValue = item.toVariant().toInt(&ok);
        if (!ok) {
            errorMessage = QStringLiteral("数组中包含无效整数");
            return false;
        }
        valuesOut.append(intValue);
    }
    return true;
}

bool parseTypeListOrAll(const QJsonValue& value, bool& allOut, QStringList& valuesOut,
                        QString& errorMessage)
{
    allOut = false;
    valuesOut.clear();

    if (value.isUndefined() || value.isNull()) {
        allOut = true;
        return true;
    }
    if (isJsonAll(value)) {
        allOut = true;
        return true;
    }
    if (!value.isArray()) {
        errorMessage = QStringLiteral("types 必须是 ALL 或类型数组");
        return false;
    }

    const QJsonArray array = value.toArray();
    if (array.isEmpty()) {
        errorMessage = QStringLiteral("types 数组不能为空");
        return false;
    }

    static const QStringList allowedTypes = {
        QStringLiteral("W"), QStringLiteral("T"), QStringLiteral("E"),
        QStringLiteral("C"), QStringLiteral("I")
    };

    for (const QJsonValue& item : array) {
        const QString type = item.toString().trimmed().toUpper();
        if (!allowedTypes.contains(type)) {
            errorMessage = QStringLiteral("不支持的类型: ") + type;
            return false;
        }
        if (!valuesOut.contains(type)) {
            valuesOut.append(type);
        }
    }
    return true;
}

bool parseMesQueryRequest(const QJsonObject& body, MesReadingQueryFilter& filter,
                          QString& errorCode, QString& errorMessage)
{
    filter = MesReadingQueryFilter();

    const QJsonObject timeRange = body.value(QStringLiteral("timeRange")).toObject();
    const QString startText = timeRange.value(QStringLiteral("start")).toVariant().toString().trimmed();
    const QString endText = timeRange.value(QStringLiteral("end")).toVariant().toString().trimmed();

    if (startText.isEmpty() || startText.compare(QStringLiteral("null"), Qt::CaseInsensitive) == 0) {
        // no start bound
    } else {
        bool ok = false;
        const QDateTime startTime = parseIsoDateTime(startText, &ok);
        if (!ok) {
            errorCode = QStringLiteral("INVALID_TIME_RANGE");
            errorMessage = QStringLiteral("timeRange.start 时间格式无效");
            return false;
        }
        filter.hasStartTime = true;
        filter.startTime = startTime;
    }

    if (endText.isEmpty() || endText.compare(QStringLiteral("null"), Qt::CaseInsensitive) == 0) {
        // no end bound
    } else {
        bool ok = false;
        const QDateTime endTime = parseIsoDateTime(endText, &ok);
        if (!ok) {
            errorCode = QStringLiteral("INVALID_TIME_RANGE");
            errorMessage = QStringLiteral("timeRange.end 时间格式无效");
            return false;
        }
        filter.hasEndTime = true;
        filter.endTime = endTime;
    }

    if (filter.hasStartTime && filter.hasEndTime && filter.startTime > filter.endTime) {
        errorCode = QStringLiteral("INVALID_TIME_RANGE");
        errorMessage = QStringLiteral("开始时间不能晚于结束时间");
        return false;
    }

    const QJsonObject devicesObj = body.value(QStringLiteral("devices")).toObject();
    if (!parseIntListOrAll(devicesObj.value(QStringLiteral("modbusAddrs")),
                           filter.allModbusAddrs, filter.modbusAddrs, errorMessage)) {
        errorCode = QStringLiteral("INVALID_DEVICES");
        errorMessage = QStringLiteral("devices.modbusAddrs ") + errorMessage;
        return false;
    }

    const QJsonObject typesObj = body.value(QStringLiteral("types")).toObject();
    if (!parseTypeListOrAll(typesObj.value(QStringLiteral("values")),
                            filter.allTypes, filter.channelTypes, errorMessage)) {
        errorCode = QStringLiteral("INVALID_TYPES");
        return false;
    }

    const QJsonObject channelsObj = body.value(QStringLiteral("channels")).toObject();
    if (!parseIntListOrAll(channelsObj.value(QStringLiteral("values")),
                           filter.allChannels, filter.channelNos, errorMessage)) {
        errorCode = QStringLiteral("INVALID_CHANNELS");
        errorMessage = QStringLiteral("channels.values ") + errorMessage;
        return false;
    }

    filter.page = body.value(QStringLiteral("page")).toInt(1);
    if (filter.page <= 0) {
        filter.page = 1;
    }

    const QJsonValue pageSizeValue = body.value(QStringLiteral("pageSize"));
    if (pageSizeValue.isUndefined() || pageSizeValue.isNull()) {
        filter.pageSize = 1000;
    } else if (isJsonAll(pageSizeValue)) {
        filter.allPageSize = true;
    } else if (pageSizeValue.isDouble()) {
        const int pageSize = pageSizeValue.toInt();
        if (pageSize <= 0) {
            errorCode = QStringLiteral("INVALID_PAGINATION");
            errorMessage = QStringLiteral("pageSize 必须是正整数或 ALL");
            return false;
        }
        filter.pageSize = pageSize;
    } else if (pageSizeValue.isString()) {
        bool ok = false;
        const int pageSize = pageSizeValue.toString().trimmed().toInt(&ok);
        if (!ok || pageSize <= 0) {
            errorCode = QStringLiteral("INVALID_PAGINATION");
            errorMessage = QStringLiteral("pageSize 必须是正整数或 ALL");
            return false;
        }
        filter.pageSize = pageSize;
    } else {
        errorCode = QStringLiteral("INVALID_PAGINATION");
        errorMessage = QStringLiteral("pageSize 必须是正整数或 ALL");
        return false;
    }
    return true;
}

QJsonValue intListToJson(bool allValues, const QList<int>& values)
{
    if (allValues) {
        return QStringLiteral("ALL");
    }
    QJsonArray array;
    for (int value : values) {
        array.append(value);
    }
    return array;
}

QJsonValue stringListToJson(bool allValues, const QStringList& values)
{
    if (allValues) {
        return QStringLiteral("ALL");
    }
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

QJsonObject buildQueryEcho(const MesReadingQueryFilter& filter)
{
    QJsonObject echo;
    QJsonObject timeRange;
    if (filter.hasStartTime) {
        timeRange.insert(QStringLiteral("start"), filter.startTime.toString(Qt::ISODate));
    } else {
        timeRange.insert(QStringLiteral("start"), QJsonValue::Null);
    }
    if (filter.hasEndTime) {
        timeRange.insert(QStringLiteral("end"), filter.endTime.toString(Qt::ISODate));
    } else {
        timeRange.insert(QStringLiteral("end"), QJsonValue::Null);
    }
    echo.insert(QStringLiteral("timeRange"), timeRange);

    QJsonObject devices;
    devices.insert(QStringLiteral("modbusAddrs"), intListToJson(filter.allModbusAddrs, filter.modbusAddrs));
    echo.insert(QStringLiteral("devices"), devices);

    QJsonObject types;
    types.insert(QStringLiteral("values"), stringListToJson(filter.allTypes, filter.channelTypes));
    echo.insert(QStringLiteral("types"), types);

    QJsonObject channels;
    channels.insert(QStringLiteral("values"), intListToJson(filter.allChannels, filter.channelNos));
    echo.insert(QStringLiteral("channels"), channels);

    echo.insert(QStringLiteral("page"), filter.page);
    if (filter.allPageSize) {
        echo.insert(QStringLiteral("pageSize"), QStringLiteral("ALL"));
    } else {
        echo.insert(QStringLiteral("pageSize"), filter.pageSize);
    }
    return echo;
}

} // namespace

// 构造函数删除 TXT 文件路径初始化，不再使用 red.txt/yellow.txt
MyRequestHandler::MyRequestHandler(QObject* parent)
    : HttpRequestHandler(parent)
{
    appDir = QCoreApplication::applicationDirPath();
    currentTheme = "静电管理在线监控系统 ESD-1000.V1.0"; // 默认主题

    // 初始化静态文件控制器
    staticSettings = new QSettings();
    staticSettings->setValue("path", appDir); // 设置静态文件根目录为应用程序目录
    staticSettings->setValue("encoding", "UTF-8");
    staticSettings->setValue("maxAge", "60000");
    staticSettings->setValue("cacheTime", "60000");
    staticSettings->setValue("cacheSize", "1000000");
    staticSettings->setValue("maxCachedFileSize", "65536");
    staticFileController = new StaticFileController(staticSettings, this);

    // 连接信号和槽
    connect(this, &MyRequestHandler::envHistoryDataReady, this, &MyRequestHandler::onEnvHistoryDataReady);
}

// 析构函数补充避免内存泄漏警告
MyRequestHandler::~MyRequestHandler()
{
    if (staticFileController) {
        delete staticFileController;
    }
    if (staticSettings) {
        delete staticSettings;
    }
}

// 核心处理所有HTTP请求删除 TXT 相关接口
void MyRequestHandler::service(HttpRequest& request, HttpResponse& response)
{
    QString path = request.getPath();
    QString method = request.getMethod();
    QMutexLocker locker(&m_mutex); // 线程安全锁避免并发访问冲突

    // 处理静态文件请求
    if (path.startsWith("/static/") || path.startsWith("/symbol/")) {
        staticFileController->service(request, response);
        return;
    }

    if (method == "POST" && path == "/api/mes/readings/query") {
        handleMesReadingsQuery(request, response);
        return;
    }

    // 2. 环境数据历史API GET /env-history-data → 返回温度、湿度、洁净度历史数据
    if (method == "GET" && path == "/env-history-data") {
        QDateTime now = QDateTime::currentDateTime();
        QDateTime start, end;

        // 直接在后端计算8点到8点的时间范围，不依赖前端传递的参数
        // 获取最近的8点作为开始时间
        start = now;
        start.setTime(QTime(8, 0, 0));

        // 如果当前时间早于今天8点，则使用昨天8点作为开始时间
        if (now < start) {
            start = start.addDays(-1);
        }

        // 获取下一个8点的时间作为结束时间
        end = start.addDays(1);

        qDebug() << "计算的时间范围:" << "start=" << start.toString("yyyy-MM-dd HH:mm:ss")
                << ", end=" << end.toString("yyyy-MM-dd HH:mm:ss");

        // 查询环境历史数据，同步查询保持简单可靠
        DBManager* dbManager = DBManager::instance();
        if (!dbManager) {
            QJsonObject errorObj;
            errorObj["success"] = false;
            errorObj["message"] = "数据库连接失败";
            response.setHeader("Content-Type", "application/json; charset=utf-8");
            response.write(QJsonDocument(errorObj).toJson(QJsonDocument::Compact), true);
            return;
        }

        // 查询各环境参数的历史数据 - 使用10分钟间隔
        QList<QPair<QDateTime, double>> tempData = dbManager->getTempHistoryData(start, end, 10);
        QList<QPair<QDateTime, double>> humidityData = dbManager->getHumidityHistoryData(start, end, 10);
        QList<QPair<QDateTime, double>> cleanlinessData = dbManager->getCleanlinessHistoryData(start, end, 10);

        // 转换为QJsonArray
        QJsonArray timeArray;
        QJsonArray tempArray;
        QJsonArray humidityArray;
        QJsonArray cleanlinessArray;

        // 构建时间轴和数据数组
        for (const auto& data : tempData) {
            timeArray.append(data.first.toString("yyyy-MM-dd HH:mm:ss"));
            tempArray.append(data.second);
        }

        // 填充湿度数据
        for (const auto& data : humidityData) {
            humidityArray.append(data.second);
        }

        // 填充洁净度数据
        for (const auto& data : cleanlinessData) {
            cleanlinessArray.append(data.second);
        }

        // 构建响应JSON
        QJsonObject responseObj;
        responseObj["time"] = timeArray;
        responseObj["temperature"] = tempArray;
        responseObj["humidity"] = humidityArray;
        responseObj["cleanliness"] = cleanlinessArray;

        // 设置响应头和响应体
        response.setHeader("Content-Type", "application/json; charset=utf-8");
        response.write(QJsonDocument(responseObj).toJson(QJsonDocument::Compact), true);
        return;
    }

    // 3. 最近10分钟环境数据API GET /latest-env-data → 返回最近10分钟的平均值
    if (method == "GET" && path == "/latest-env-data") {
        QDateTime now = QDateTime::currentDateTime();
        QDateTime tenMinutesAgo = now.addSecs(-10 * 60);

        // 查询最近10分钟的平均值
        QVector<double> avgData = DBManager::instance()->getAverageDataFromTimeRange(tenMinutesAgo, 10);

        // 构建JSON响应
        QJsonObject responseObj;
        responseObj["temperature"] = avgData.size() > 3 ? avgData[3] : 0.0;
        responseObj["humidity"] = avgData.size() > 4 ? avgData[4] : 0.0;
        responseObj["cleanliness"] = avgData.size() > 5 ? avgData[5] : 0.0;
        responseObj["time"] = now.toString("HH:mm");

        QJsonDocument doc(responseObj);
        response.setHeader("Content-Type", "application/json");
        response.write(doc.toJson(), true);
        return;
    }

    // 1. GET / -> static/index.html
    if (method == "GET" && path == "/") {
        QFile htmlFile(QDir(appDir).filePath("static/index.html"));
        if (!htmlFile.open(QIODevice::ReadOnly)) {
            response.setStatus(500, "Internal Error");
            response.setHeader("Content-Type", "text/html; charset=utf-8");
            response.write("Failed to load index page", true);
                            return;
                        }
        QByteArray htmlData = htmlFile.readAll();
        htmlFile.close();
                    response.setHeader("Content-Type", "text/html; charset=utf-8");
        response.write(htmlData, true);
                    return;
                }
    // 2. 背景图接口��GET /showimage → �回Qt中选中的背景图��固定尺寸适配��
    else if (method == "GET" && path == "/showimage") {
        MainWindow* mainWindow = qobject_cast<MainWindow*>(this->parent());
        if (!mainWindow) {
            response.setStatus(500, "Internal Error");
            response.write("无法获取�窗口实例", true);
            return;
        }

        QString imagePath = mainWindow->getCurrentImagePath();
        if (imagePath.isEmpty()) {
            qDebug() << "[/showimage] 未选择背景图片";
            response.setStatus(404, "Image Not Found");
            response.write("未选择背景图片", true);
            return;
        }

        QFile imageFile(imagePath);
        if (!imageFile.exists()) {
            qDebug() << "[/showimage] 背景图不存在��" << imagePath;
            response.setStatus(404, "Image Not Found");
            response.write(QString("背景图不存在��路径��%1��").arg(imagePath).toUtf8(), true);
            return;
        }
        if (!imageFile.open(QIODevice::ReadOnly)) {
            qDebug() << "[/showimage] 图片文�打开失败��" << imageFile.errorString();
            response.setStatus(500, "Internal Error");
            response.write(QString("图片文�打开失败��错误��%1��").arg(imageFile.errorString()).toUtf8(), true);
            return;
        }

        QByteArray imageData = imageFile.readAll();
        imageFile.close();

        // 根据文�后缀设置Content-Type��适配常见图片格式��
        QString suffix = QFileInfo(imagePath).suffix().toLower();
        QString contentType = "image/jpeg";
        if (suffix == "png") contentType = "image/png";
        else if (suffix == "gif") contentType = "image/gif";
        else if (suffix == "bmp") contentType = "image/bmp";
        else if (suffix == "svg") contentType = "image/svg+xml";

        response.setHeader("Content-Type", contentType.toUtf8());
        response.setHeader("Content-Length", QString::number(imageData.size()).toUtf8());
        // 强制图片按固定尺寸显示��前端已设置object-fit: cover��
        response.write(imageData, true);
        return;
    }

    // 3. 串口数据接口��GET /getdata → �MainWindow获取串口采集数据���用你的实际函数 getAddressFuncData��
    else if (method == "GET" && path == "/getdata") {
        MainWindow* mainWindow = qobject_cast<MainWindow*>(this->parent());
        if (!mainWindow) {
            qDebug() << "[/getdata] 获取MainWindow实例失败";
            response.setStatus(500, "Internal Error");
            response.write("无法获取串口数据���窗口实例异常��", true);
            return;
        }

        // 关键��调用你 MainWindow 中实际的串口数据函数 getAddressFuncData()
        QMap<QString, QStringList> serialData = mainWindow->getAddressFuncData();
        QJsonObject dataObj;

        for (auto it = serialData.begin(); it != serialData.end(); ++it) {
            QJsonArray dataArray;
            for (const QString& dataItem : it.value()) {
                dataArray.append(dataItem);
            }
            dataObj[it.key()] = dataArray;
        }

        response.setHeader("Content-Type", "application/json; charset=utf-8");
        response.write(QJsonDocument(dataObj).toJson(QJsonDocument::Compact), true);
        return;
    }

                    // 4. 图片�息接口��GET /currentImageInfo → �回固定适配尺寸�息
                    else if (method == "GET" && path == "/currentImageInfo") {
                        MainWindow* mainWindow = qobject_cast<MainWindow*>(this->parent());
                        if (!mainWindow) {
                            response.setStatus(500, "Internal Error");
                            response.write("无法获取�窗口实例", true);
                            return;
                        }

                        QString imagePath = mainWindow->getCurrentImagePath();
                        QJsonObject imageObj;
                        QString absPath = QFileInfo(imagePath).absoluteFilePath();

                        if (imagePath.isEmpty()) {
                            imageObj["exists"] = false;
                            imageObj["message"] = "未选择背景图片";
                            imageObj["timestamp"] = 0; // 时间戳��0表示无图片
                        } else {
                            QPixmap pixmap(imagePath);
                            if (pixmap.isNull()) {
                                imageObj["exists"] = false;
                                imageObj["message"] = "图片加载失败";
                                imageObj["timestamp"] = 0;
                            } else {
                                imageObj["exists"] = true;
                                imageObj["path"] = absPath; // �回�对路径��确�一致性
                                imageObj["timestamp"] = mainWindow->getCurrentImageTimestamp(); // 关键��图片�改时间戳
                                imageObj["originalWidth"] = pixmap.width();
                                imageObj["originalHeight"] = pixmap.height();
                                imageObj["adaptedWidth"] = 1850;
                                imageObj["adaptedHeight"] = 750;
                                imageObj["fileName"] = QFileInfo(imagePath).fileName();
                            }
                        }

                        response.setHeader("Content-Type", "application/json; charset=utf-8");
                        response.write(QJsonDocument(imageObj).toJson(QJsonDocument::Compact), true);
                        return;
                    }

                       // 5. 设备列表接口��GET /currentDevices → �回设备列表��基于固定尺寸点位��
                    else if (method == "GET" && path == "/currentDevices") {
                            MainWindow* mainWindow = qobject_cast<MainWindow*>(this->parent());
                            if (!mainWindow) {
                                response.setStatus(500, "Internal Error");
                                response.write("无法获取�窗口实例", true);
                                return;
                            }

                            // 调用新增接口��获取当前图片的设备列表
                            QList<QJsonObject> devicesData = mainWindow->getCurrentImageDevices();
                            QJsonArray devicesArray;
                            for (const QJsonObject& devObj : devicesData) {
                                devicesArray.append(devObj);
                            }

                            QJsonObject responseObj;
                            responseObj["count"] = devicesArray.size();
                            responseObj["devices"] = devicesArray;
                            responseObj["currentImagePath"] = mainWindow->getCurrentImagePath();
                            responseObj["message"] = devicesArray.isEmpty() ? "当前图片暂无�定设备" : "成功获取当前图片设备";

                            response.setHeader("Content-Type", "application/json; charset=utf-8");
                            response.write(QJsonDocument(responseObj).toJson(QJsonDocument::Compact), true);
                            return;
                        }

                        // 6. 报警处理记录接口��POST /api/alarm-handling → 存储报警处理记录
                        else if (method == "POST" && path == "/api/alarm-handling") {
                            // 解析请求体
                            QByteArray requestBody = request.getBody();
                            QJsonDocument doc = QJsonDocument::fromJson(requestBody);
                            QJsonObject jsonObj = doc.object();

                            // 获取参数
                            QString handler = jsonObj["handler"].toString();
                            QString action = jsonObj["action"].toString();
                            QDateTime handleTime = QDateTime::currentDateTime();

                            // 参数验证
                            if (handler.isEmpty() || action.isEmpty()) {
                                QJsonObject errorObj;
                                errorObj["success"] = false;
                                errorObj["message"] = "处理人员和处理方式不能为空";
                                response.setHeader("Content-Type", "application/json; charset=utf-8");
                                response.write(QJsonDocument(errorObj).toJson(QJsonDocument::Compact), true);
                                return;
                            }

                            // 调用DBManager插入数据
                            DBManager* dbManager = DBManager::instance();
                            if (!dbManager) {
                                QJsonObject errorObj;
                                errorObj["success"] = false;
                                errorObj["message"] = "数据库�接失败";
                                response.setHeader("Content-Type", "application/json; charset=utf-8");
                                response.write(QJsonDocument(errorObj).toJson(QJsonDocument::Compact), true);
                                return;
                            }

                            // 插入报警处理记录
                            bool success = dbManager->insertAlarmHandling(handleTime, handler, action);

                            QJsonObject responseObj;
                            if (success) {
                                responseObj["success"] = true;
                                responseObj["message"] = "报警处理记录存储成功";
                            } else {
                                responseObj["success"] = false;
                                responseObj["message"] = "报警处理记录存储失败";
                            }

                            response.setHeader("Content-Type", "application/json; charset=utf-8");
                            response.write(QJsonDocument(responseObj).toJson(QJsonDocument::Compact), true);
                            return;
                        }

//                        // 【�改】2. 图片�息接口���回当前图片的时间戳��用于前端检测变化��
//                        else if (method == "GET" && path == "/currentImageInfo") {
//                            MainWindow* mainWindow = qobject_cast<MainWindow*>(this->parent());
//                            if (!mainWindow) {
//                                response.setStatus(500, "Internal Error");
//                                response.write("无法获取�窗口实例", true);
//                                return;
//                            }

//                            QString imagePath = mainWindow->getCurrentImagePath();
//                            QJsonObject imageObj;

//                            if (imagePath.isEmpty()) {
//                                imageObj["exists"] = false;
//                                imageObj["message"] = "未选择背景图片";
//                                imageObj["timestamp"] = 0;
//                            } else {
//                                QPixmap pixmap(imagePath);
//                                if (pixmap.isNull()) {
//                                    imageObj["exists"] = false;
//                                    imageObj["message"] = "图片加载失败";
//                                    imageObj["timestamp"] = 0;
//                                } else {
//                                    imageObj["exists"] = true;
//                                    imageObj["path"] = imagePath;
//                                    imageObj["timestamp"] = mainWindow->getCurrentImageTimestamp(); // 关键��时间戳
//                                    imageObj["originalWidth"] = pixmap.width();
//                                    imageObj["originalHeight"] = pixmap.height();
//                                    imageObj["fileName"] = QFileInfo(imagePath).fileName();
//                                }
//                            }

//                            response.setHeader("Content-Type", "application/json; charset=utf-8");
//                            response.write(QJsonDocument(imageObj).toJson(QJsonDocument::Compact), true);
//                            return;
//                        }

                       // 6. 图片尺寸接口��GET /getImageSize → �回固定尺寸
                       else if (method == "GET" && path == "/getImageSize") {
                           MainWindow* mainWindow = qobject_cast<MainWindow*>(this->parent());
                           if (!mainWindow) {
                               qDebug() << "[/getImageSize] 获取MainWindow实例失败";
                               response.setStatus(500, "Internal Error");
                               response.write("无法获取图片尺寸���窗口实例异常��", true);
                               return;
                           }

                           // 强制�回1920*1080适配的固定尺寸
                           QJsonObject sizeObj;
                           sizeObj["w"] = 1850;          // 地图容器固定宽度��1920-35*2边距��
                           sizeObj["h"] = 750;
                           sizeObj["originalW"] = 1850;
                           sizeObj["originalH"] = 750;
                           sizeObj["message"] = "已强制适配1920*1080分辨率，使用固定尺寸1850x750";
                           sizeObj["screenResolution"] = "1920x1080��固定布局��";

                           response.setHeader("Content-Type", "application/json; charset=utf-8");
                           response.write(QJsonDocument(sizeObj).toJson(QJsonDocument::Compact), true);
                           return;
                       }

                       // 7. 历史折�图页面��GET /history-chart → �回历史折�图页面
                       else if (method == "GET" && path == "/history-chart") {
                           QString historyHtml = generateHistoryChartPage();
                           response.setHeader("Content-Type", "text/html; charset=utf-8");
                           response.write(historyHtml.toUtf8(), true);
                           return;
                       }
                       // 8.1 合格率API��GET /api/qualified-rate/{timeRange} → �回JSON格式的合格率数据
                       else if (method == "GET" && path.startsWith("/api/qualified-rate/")) {
                           QString timeRange = path.split("/").last();
                           QString jsonData = generateQualifiedRateDataJson(timeRange);
                           response.setHeader("Content-Type", "application/json; charset=utf-8");
                           response.write(jsonData.toUtf8(), true);
                           return;
                       }
                       // 8.2 腕带合格率API��GET /api/w-qualified-rate/{timeRange}
                       else if (method == "GET" && path.startsWith("/api/w-qualified-rate/")) {
                           QString timeRange = path.split("/").last();
                           QString jsonData = generateWQualifiedRateDataJson(timeRange);
                           response.setHeader("Content-Type", "application/json; charset=utf-8");
                           response.write(jsonData.toUtf8(), true);
                           return;
                       }
                       // 8.3 台垫合格率API��GET /api/t-qualified-rate/{timeRange}
                       else if (method == "GET" && path.startsWith("/api/t-qualified-rate/")) {
                           QString timeRange = path.split("/").last();
                           QString jsonData = generateTQualifiedRateDataJson(timeRange);
                           response.setHeader("Content-Type", "application/json; charset=utf-8");
                           response.write(jsonData.toUtf8(), true);
                           return;
                       }
                       // 8.4 设备合格率API��GET /api/e-qualified-rate/{timeRange}
                       else if (method == "GET" && path.startsWith("/api/e-qualified-rate/")) {
                           QString timeRange = path.split("/").last();
                           QString jsonData = generateEQualifiedRateDataJson(timeRange);
                           response.setHeader("Content-Type", "application/json; charset=utf-8");
                           response.write(jsonData.toUtf8(), true);
                           return;
                       }
                       // 8.5 平均温度API��GET /api/avg-temperature/{timeRange}
                       else if (method == "GET" && path.startsWith("/api/avg-temperature/")) {
                           QString timeRange = path.split("/").last();
                           QString jsonData = generateTempDataJson(timeRange);
                           response.setHeader("Content-Type", "application/json; charset=utf-8");
                           response.write(jsonData.toUtf8(), true);
                           return;
                       }
                       // 8.6 平均�度API��GET /api/avg-humidity/{timeRange}
                       else if (method == "GET" && path.startsWith("/api/avg-humidity/")) {
                           QString timeRange = path.split("/").last();
                           QString jsonData = generateHumidityDataJson(timeRange);
                           response.setHeader("Content-Type", "application/json; charset=utf-8");
                           response.write(jsonData.toUtf8(), true);
                           return;
                       }
                       // 8.7 平均洁净度API��GET /api/avg-cleanliness/{timeRange}
                       else if (method == "GET" && path.startsWith("/api/avg-cleanliness/")) {
                           QString timeRange = path.split("/").last();
                           QString jsonData = generateCleanlinessDataJson(timeRange);
                           response.setHeader("Content-Type", "application/json; charset=utf-8");
                           response.write(jsonData.toUtf8(), true);
                           return;
                       }
                       // 8.8 历史图表数据API��GET /history-chart-data → �回所有历史数据
                       else if (method == "GET" && path == "/history-chart-data") {
                           QString timeRange = request.getParameter("timeRange");
                           if (timeRange.isEmpty()) {
                               timeRange = "day";
                           }

                           // 发送查询开始日�到textBrowser
                           DBManager::instance()->logGenerated("MyRequestHandler", QString("正在获取历史图表数据��时间范围��%1").arg(timeRange));

                           QJsonObject responseObj;
                           responseObj["success"] = true;
                           responseObj["timeRange"] = timeRange;

                           // 获取各种历史数据
                           QJsonDocument wristDoc = QJsonDocument::fromJson(generateWQualifiedRateDataJson(timeRange).toUtf8());
                           responseObj["wristData"] = wristDoc.isObject() && wristDoc.object().contains("data") ? wristDoc.object().value("data").toArray() : QJsonArray();

                           QJsonDocument deviceDoc = QJsonDocument::fromJson(generateEQualifiedRateDataJson(timeRange).toUtf8());
                           responseObj["deviceData"] = deviceDoc.isObject() && deviceDoc.object().contains("data") ? deviceDoc.object().value("data").toArray() : QJsonArray();

                           QJsonDocument matDoc = QJsonDocument::fromJson(generateTQualifiedRateDataJson(timeRange).toUtf8());
                           responseObj["matData"] = matDoc.isObject() && matDoc.object().contains("data") ? matDoc.object().value("data").toArray() : QJsonArray();

                           QJsonDocument tempDoc = QJsonDocument::fromJson(generateTempDataJson(timeRange).toUtf8());
                           responseObj["tempData"] = tempDoc.isObject() && tempDoc.object().contains("data") ? tempDoc.object().value("data").toArray() : QJsonArray();

                           QJsonDocument humidityDoc = QJsonDocument::fromJson(generateHumidityDataJson(timeRange).toUtf8());
                           responseObj["humidityData"] = humidityDoc.isObject() && humidityDoc.object().contains("data") ? humidityDoc.object().value("data").toArray() : QJsonArray();

                           QJsonDocument cleanlinessDoc = QJsonDocument::fromJson(generateCleanlinessDataJson(timeRange).toUtf8());
                           responseObj["cleanlinessData"] = cleanlinessDoc.isObject() && cleanlinessDoc.object().contains("data") ? cleanlinessDoc.object().value("data").toArray() : QJsonArray();

                           // 获取报警处理记录
                           QDateTime endTime = QDateTime::currentDateTime();
                           QDateTime startTime;
                           if (timeRange == "day") {
                               startTime = endTime.addDays(-1);
                           } else if (timeRange == "90days") {
                               startTime = endTime.addDays(-90);
                           } else {
                               startTime = endTime.addDays(-1);
                           }
                           QList<QMap<QString, QVariant>> alarmRecords = DBManager::instance()->getAlarmHandlingRecords(startTime, endTime);
                           QJsonArray alarmDataArray;
                           for (const QMap<QString, QVariant>& record : alarmRecords) {
                               QJsonObject recordObj;
                               recordObj["time"] = record["time"].toString();
                               recordObj["person"] = record["person"].toString();
                               recordObj["thing"] = record["thing"].toString();
                               alarmDataArray.append(recordObj);
                           }
                           responseObj["alarmData"] = alarmDataArray;

                           // 发送查询完成日�到textBrowser
                           DBManager::instance()->logGenerated("MyRequestHandler", "历史图表数据获取完成");

                           response.setHeader("Content-Type", "application/json; charset=utf-8");
                           response.write(QJsonDocument(responseObj).toJson(QJsonDocument::Compact), true);
                           return;
                       }
                       // 9. �题API��GET /api/theme → �回当前�题
                       else if (method == "GET" && path == "/api/theme") {
                           QJsonObject themeObj;
                           themeObj["theme"] = currentTheme;
                           themeObj["separateEnvEsd"] = m_separateEnvEsd;
                           QJsonDocument doc(themeObj);
                           response.setHeader("Content-Type", "application/json; charset=utf-8");
                           response.write(doc.toJson(QJsonDocument::Compact), true);
                           return;
                       }


                       // 10. 健康检查接口��GET /health → 服务可用性检测
                       else if (method == "GET" && path == "/health") {
                           QJsonObject healthObj;
                           healthObj["status"] = "ok";
                           healthObj["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
                           healthObj["service"] = "ESD-1000.V1.0 RequestHandler��1920x1080固定尺寸版��";
                           healthObj["screenResolution"] = "1920x1080��强制固定布局��";
                           healthObj["layoutInfo"] = QJsonObject({
                               {"topBarHeight", 60},
                               {"statsBarHeight", 50},
                               {"mapContainerSize", "1850x750"},
                               {"chartPanelSize", "1850x210"},
                               {"totalHeight", 1080},
                               {"totalWidth", 1920}
                           });
                           healthObj["supportInterfaces"] = QJsonArray::fromStringList({
                               "/ (�页��1920x1080固定布局)",
                               "/showimage (背景图��按1850x720填充)",
                               "/getdata (串口数据采集)",
                               "/currentDevices (设备列表及状态)",
                               "/currentImageInfo (图片详情+固定适配尺寸)",
                               "/getImageSize (�回固定1850x720尺寸)",
                               "/health (服务健康检查)"
                           });
                           healthObj["optimization"] = "强制固定布局��图表区高度220px��X轴标签优化��无响应式折行";

                           response.setHeader("Content-Type", "application/json; charset=utf-8");
                           response.write(QJsonDocument(healthObj).toJson(QJsonDocument::Compact), true);
                           return;
                       }
        // 9. 静态资源��symbol文�夹下的设备图标��放在health接口后、404前��
        else if (method == "GET" && path.startsWith("/symbol/")) {
            // 提取图片文�名��如 /symbol/E-green.png → E-green.png��
            QString fileName = path.mid(QString("/symbol/").length());
            if (fileName.isEmpty()) {
                response.setStatus(404, "Not Found");
                response.write("图片文�名不能为空", true);
                return;
            }
            // 拼接�行目录下的symbol文�夹路径
            QString symbolFilePath = appDir + "/symbol/" + fileName;
            QFile symbolFile(symbolFilePath);

            // 检查文�是否存在
            if (!symbolFile.exists()) {
                qDebug() << "[/symbol] 图片不存在��" << symbolFilePath;
                response.setStatus(404, "Not Found");
                response.write(QString("图片不存在��%1").arg(fileName).toUtf8(), true);
                return;
            }
            // 打开并�取图片
            if (!symbolFile.open(QIODevice::ReadOnly)) {
                qDebug() << "[/symbol] 图片打开失败��" << symbolFile.errorString();
                response.setStatus(500, "Internal Error");
                response.write(QString("图片打开失败��%1").arg(symbolFile.errorString()).toUtf8(), true);
                return;
            }
            QByteArray imageData = symbolFile.readAll();
            symbolFile.close();

            // 设置图片Content-Type
            QString suffix = QFileInfo(symbolFilePath).suffix().toLower();
            QString contentType = "image/png"; // �认PNG格式
            if (suffix == "jpg" || suffix == "jpeg") {
                contentType = "image/jpeg";
            } else if (suffix == "gif") {
                contentType = "image/gif";
            } else if (suffix == "bmp") {
                contentType = "image/bmp";
            } else if (suffix == "svg") {
                contentType = "image/svg+xml";
            }

            response.setHeader("Content-Type", contentType.toUtf8());
            response.setHeader("Content-Length", QString::number(imageData.size()).toUtf8());
            response.write(imageData, true);
            return;
        }
                       // �认接口��404未找到
                       else {
                           qDebug() << "[404] 未匹配的请求��" << method << path;
                           response.setStatus(404, "Not Found");
                           response.setHeader("Content-Type", "text/plain; charset=utf-8");
                           QString errorMsg = QString("请求路径不存在��%1 %2\n"
                                                     "当前服务已强制适配1920x1080固定分辨率\n"
                                                     "支持的接口请�问 /health 查询详��息").arg(method).arg(path);
                           response.write(errorMsg.toUtf8(), true);
                           return;
                       }
}

// 根据时间范围计算开始时间和�束时间
QPair<QDateTime, QDateTime> MyRequestHandler::calculateTimeRange(const QString& timeRange, bool useFixedDayRange)
{
    QDateTime now = QDateTime::currentDateTime();
    QDateTime startTime;
    QDateTime endTime;

    if (useFixedDayRange) {
        // 环境数据页面��固定�早上8点到早上8点
        if (timeRange == "day") {
            startTime = now;
            startTime.setTime(QTime(8, 0, 0));
            if (now < startTime) {
                startTime = startTime.addDays(-1);
            }
            endTime = startTime.addDays(1);
        } else if (timeRange == "week") {
            startTime = now.addDays(-7);
            endTime = now;
        } else if (timeRange == "month") {
            startTime = now.addDays(-30);
            endTime = now;
        } else {
            startTime = now;
            startTime.setTime(QTime(8, 0, 0));
            if (now < startTime) {
                startTime = startTime.addDays(-1);
            }
            endTime = startTime.addDays(1);
        }
    } else {
        // 历史记录界面��最�N小时/天
        if (timeRange == "day") {
            endTime = now;
            startTime = endTime.addDays(-1);
        } else if (timeRange == "week") {
            endTime = now;
            startTime = endTime.addDays(-7);
        } else if (timeRange == "month") {
            endTime = now;
            startTime = endTime.addDays(-30);
        } else if (timeRange == "90days") {
            endTime = now;
            startTime = endTime.addDays(-90);
        } else {
            endTime = now;
            startTime = endTime.addDays(-1);
        }
    }

    return qMakePair(startTime, endTime);
}

// 生成历史折�图页面
QString MyRequestHandler::generateHistoryChartPage()
{
    QString html = R"html(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=1920, height=1080, initial-scale=1.0">
    <title>静电管理在线监控 ESD-1000.V1.0 - 历史数据折线图</title>
    <script src="/static/echarts.min.js"></script>
    <style>
        :root {
            --primary: #165DFF;
            --secondary: #0E2F56;
            --accent: #36CFC9;
            --text-light: #FFFFFF;
            --text-secondary: #86909C;
            --bg-primary: #0F172A;
            --bg-card: #0F1E35;
            --border-glow: linear-gradient(135deg, rgba(22, 93, 255, 0.6), rgba(54, 207, 205, 0.6));
            --shadow: 0 0 15px rgba(22, 93, 255, 0.3);
            --transition: all 0.3s ease;
        }
        * { margin: 0; padding: 0; box-sizing: border-box; }
        html, body {
            width: 1920px;
            height: 1080px;
            font-family: "Segoe UI", "Microsoft YaHei", sans-serif;
            overflow: hidden;
            background-color: var(--bg-primary);
            color: var(--text-light);
            background-image: radial-gradient(circle at center, rgba(22, 93, 255, 0.1) 0%, transparent 60%);
            margin: 0 auto;
        }
        .top-bar {
            height: 60px;
            background: var(--secondary);
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 0 40px;
            box-shadow: var(--shadow);
            position: relative;
            z-index: 10;
        }
        .title {
            font-size: 28px;
            font-weight: 680;
            text-align: center;
            width: 100%;
            position: absolute;
            left: 0;
            letter-spacing: 2px;
        }
        .back-btn {
            padding: 8px 16px;
            background: linear-gradient(135deg, #165DFF, #0A42E6);
            color: white;
            border: none;
            border-radius: 6px;
            font-size: 14px;
            font-weight: 500;
            cursor: pointer;
            transition: all 0.3s ease;
            box-shadow: 0 2px 8px rgba(22, 93, 255, 0.3);
            z-index: 11;
            position: relative;
        }
        .back-btn:hover {
            background: linear-gradient(135deg, #0A42E6, #0836CC);
            transform: translateY(-1px);
            box-shadow: 0 4px 12px rgba(22, 93, 255, 0.4);
        }
        .content {
            display: flex;
            flex-direction: column;
            height: calc(100vh - 60px);
            max-height: 1020px;
            padding: 15px;
            gap: 15px;
            overflow: hidden;
        }
        .chart-container {
            flex: 1;
            min-height: 0;
            background: var(--bg-card);
            border-radius: 12px;
            box-shadow: var(--shadow);
            padding: 15px;
            border: 1px solid rgba(22, 93, 255, 0.2);
            position: relative;
            overflow: hidden;
        }
        .chart-title {
            font-size: 20px;
            font-weight: 600;
            margin-bottom: 15px;
            color: var(--text-light);
            display: flex;
            align-items: center;
            gap: 10px;
        }
        .chart {
            width: 100%;
            height: calc(100% - 40px);
        }
        .loading {
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            font-size: 16px;
            color: var(--text-secondary);
        }
        .time-controls {
            display: flex;
            justify-content: center;
            gap: 20px;
            margin-bottom: 30px;
            padding: 20px;
            background: var(--bg-card);
            border-radius: 10px;
            box-shadow: var(--shadow);
        }
        .time-btn {
            padding: 12px 24px;
            background: transparent;
            border: 2px solid var(--primary);
            color: var(--primary);
            border-radius: 25px;
            cursor: pointer;
            font-size: 14px;
            font-weight: 500;
            transition: var(--transition);
            outline: none;
        }
        .time-btn:hover {
            background: var(--primary);
            color: var(--text-light);
            transform: translateY(-2px);
            box-shadow: 0 5px 15px rgba(22, 93, 255, 0.4);
        }
        .time-btn.active {
            background: var(--primary);
            color: var(--text-light);
            box-shadow: 0 0 15px rgba(22, 93, 255, 0.5);
        }
        .charts-row {
            display: flex;
            gap: 15px;
            flex: 1;
            min-height: 0;
        }
        .charts-column {
            flex: 1;
            display: flex;
            flex-direction: column;
            gap: 15px;
            min-height: 0;
        }
    </style>
</head>
<body>
    <div class="top-bar">
        <button class="back-btn" onclick="goBack()">返回首页</button>
        <div class="title">历史数据折线图</div>
    </div>
    <div class="content">
        <div class="time-controls">
            <button class="time-btn active" data-range="day" onclick="selectTimeRange('day')">最近一天</button>
            <button class="time-btn" data-range="90days" onclick="selectTimeRange('90days')">最近九十天</button>
        </div>

        <div class="charts-row">
            <!-- 左列��合格率图表 -->
            <div class="charts-column">
                <!-- 腕带合格率图表 -->
                <div class="chart-container">
                    <div class="chart-title">
                        <span>✅</span>
                        <span>腕带合格率趋势</span>
                    </div>
                    <div id="wristChart" class="chart"></div>
                    <div id="wristLoading" class="loading">正在加载数据...</div>
                </div>

                <!-- 设备合格率图表 -->
                <div class="chart-container">
                    <div class="chart-title">
                        <span>✅</span>
                        <span>设备合格率趋势</span>
                    </div>
                    <div id="deviceChart" class="chart"></div>
                    <div id="deviceLoading" class="loading">正在加载数据...</div>
                </div>

                <!-- 台垫合格率图表 -->
                <div class="chart-container">
                    <div class="chart-title">
                        <span>✅</span>
                        <span>台垫合格率趋势</span>
                    </div>
                    <div id="matChart" class="chart"></div>
                    <div id="matLoading" class="loading">正在加载数据...</div>
                </div>
            </div>

            <!-- 右列��环境数据图表 -->
            <div class="charts-column">
                <!-- 平均温度图表 -->
                <div class="chart-container">
                    <div class="chart-title">
                        <span>🌡</span>
                        <span>平均温度趋势</span>
                    </div>
                    <div id="tempChart" class="chart"></div>
                    <div id="tempLoading" class="loading">正在加载数据...</div>
                </div>

                <!-- 平均湿度图表 -->
                <div class="chart-container">
                    <div class="chart-title">
                        <span>💧</span>
                        <span>平均湿度趋势</span>
                    </div>
                    <div id="humidityChart" class="chart"></div>
                    <div id="humidityLoading" class="loading">正在加载数据...</div>
                </div>

                <!-- 平均洁净度图表 -->
                <div class="chart-container">
                    <div class="chart-title">
                        <span>✨</span>
                        <span>平均洁净度趋势</span>
                    </div>
                    <div id="cleanlinessChart" class="chart"></div>
                    <div id="cleanlinessLoading" class="loading">正在加载数据...</div>
                </div>
            </div>
        </div>
    </div>

    <script>
        let wristChart = null;
        let deviceChart = null;
        let matChart = null;
        let tempChart = null;
        let humidityChart = null;
        let cleanlinessChart = null;

        let currentTimeRange = 'day';

        function getHistoryXAxisBounds() {
            const now = Date.now();
            if (currentTimeRange === '90days') {
                return { min: now - 90 * 24 * 60 * 60 * 1000, max: now };
            }
            return { min: now - 24 * 60 * 60 * 1000, max: now };
        }

        function formatHistoryAxisTime(axisValue) {
            if (axisValue == null || axisValue === '') return '';
            const d = new Date(axisValue);
            return isNaN(d.getTime()) ? String(axisValue) : d.toLocaleString('zh-CN', { hour12: false });
        }

        /** 时间轴折线：[t,y] 时取 y；保证提示为当前轴上的数据点而非错误类型 */
        function historyAxisTooltipFormatter(params, valueSuffix) {
            valueSuffix = valueSuffix || '';
            if (!params || params.length === 0) return '';
            const p = params[0];
            if (!p) return '';
            let y = p.value != null ? p.value : p.data;
            if (Array.isArray(y)) y = y[y.length - 1];
            const t = (p.axisValueLabel != null && p.axisValueLabel !== '') ? p.axisValueLabel : formatHistoryAxisTime(p.axisValue);
            if (y === null || y === undefined || (typeof y === 'number' && isNaN(y))) {
                return '时间：' + t + '<br/>数值：无数据';
            }
            const n = Number(y);
            const vs = isNaN(n) ? String(y) : n.toFixed(2);
            return '时间：' + t + '<br/>数值：' + vs + valueSuffix;
        }

        // 初始化图表
        function initCharts() {
            const xb = getHistoryXAxisBounds();
            // 初始化腕带合格率图表
            const wristDom = document.getElementById('wristChart');
            wristChart = echarts.init(wristDom);

            const wristOption = {
                backgroundColor: 'var(--bg-card)',
                textStyle: { color: 'var(--text-light)', fontSize: 10 },
                title: {
                    text: '腕带合格率',
                    textStyle: {
                        color: '#FFFFFF',
                        fontSize: 16
                    }
                },
                tooltip: {
                    trigger: 'axis',
                    triggerOn: 'mousemove',
                    backgroundColor: 'rgba(15, 30, 53, 0.9)',
                    borderColor: '#36CFC9',
                    borderWidth: 1,
                    textStyle: {
                        color: '#FFFFFF',
                        fontSize: 11
                    },
                    formatter: function(params) { return historyAxisTooltipFormatter(params, ''); },
                    axisPointer: {
                        type: 'cross',
                        snap: true,
                        triggerOn: 'mousemove',
                        lineStyle: {
                            color: '#36CFC9',
                            type: 'dashed',
                            width: 1
                        },
                        crossStyle: {
                            color: '#36CFC9'
                        }
                    }
                },
                grid: {
                    left: '4%',
                    right: '4%',
                    top: '15%',
                    bottom: '15%',
                    containLabel: true
                },
                xAxis: {
                    type: 'time',
                    boundaryGap: false,
                    min: xb.min,
                    max: xb.max,
                    axisLine: { lineStyle: { color: 'rgba(255,255,255,0.3)', width: 1 } },
                    axisTick: { show: true, interval: 5 },
                    axisLabel: {
                        fontSize: 8,
                        rotate: 0,
                        color: 'var(--text-secondary)',
                        margin: 8,
                        interval: 5,
                        formatter: function(value) {
                            const date = new Date(value);
                            const time = date.toLocaleTimeString('zh-CN', {
                                hour: '2-digit',
                                minute: '2-digit'
                            });
                            const dateStr = date.toLocaleDateString('zh-CN', {
                                month: '2-digit',
                                day: '2-digit'
                            });
                            return time + '\n' + dateStr;
                        }
                    }
                },
                yAxis: {
                    type: 'value',
                    name: '合格率(%)',
                    nameTextStyle: {
                        color: '#86909C'
                    },
                    axisLine: { lineStyle: { color: 'rgba(255,255,255,0.3)', width: 1 } },
                    axisLabel: {
                        fontSize: 10,
                        color: '#FFFFFF',
                        formatter: '{value}%'
                    },
                    splitLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.1)', width: 1 } },
                    min: 0,
                    max: 100
                },
                series: [
                    {
                        name: '腕带合格率',
                        type: 'line',
                        data: [],
                        smooth: true,
                        connectNulls: false, // 不�接null值���免没有数据的点�接到0
                        itemStyle: { color: '#36CFC9', borderWidth: 2 },
                        lineStyle: { color: '#36CFC9', width: 1.5 },
                        areaStyle: {
                            color: {
                                type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
                                colorStops: [
                                    { offset: 0, color: '#36CFC980' },
                                    { offset: 1, color: '#36CFC900' }
                                ]
                            }
                        },
                        symbol: 'circle',
                        symbolSize: 3,
                        showSymbol: true,
                        emphasis: { symbolSize: 5, itemStyle: { color: '#36CFC9' } },
                        markLine: {
                            silent: true,
                            data: [
                                {
                                    yAxis: 90,
                                    name: '合格�',
                                    lineStyle: {
                                        color: '#EF4444',
                                        type: 'dashed',
                                        width: 1.5
                                    },
                                    label: {
                                        formatter: '90%',
                                        color: '#EF4444'
                                    }
                                }
                            ]
                        }
                    }
                ]
            };

            wristChart.setOption(wristOption);

            // 初始化设备合格率图表
            const deviceDom = document.getElementById('deviceChart');
            deviceChart = echarts.init(deviceDom);

            const deviceOption = {
                backgroundColor: 'var(--bg-card)',
                textStyle: { color: 'var(--text-light)', fontSize: 10 },
                title: {
                    text: '设备合格率',
                    textStyle: {
                        color: '#FFFFFF',
                        fontSize: 16
                    }
                },
                tooltip: {
                    trigger: 'axis',
                    triggerOn: 'mousemove',
                    backgroundColor: 'rgba(15, 30, 53, 0.9)',
                    borderColor: '#FFA940',
                    borderWidth: 1,
                    textStyle: {
                        color: '#FFFFFF',
                        fontSize: 11
                    },
                    formatter: function(params) { return historyAxisTooltipFormatter(params, ''); },
                    axisPointer: {
                        type: 'cross',
                        snap: true,
                        triggerOn: 'mousemove',
                        lineStyle: {
                            color: '#FFA940',
                            type: 'dashed',
                            width: 1
                        },
                        crossStyle: {
                            color: '#FFA940'
                        }
                    }
                },
                grid: {
                    left: '4%',
                    right: '4%',
                    top: '15%',
                    bottom: '15%',
                    containLabel: true
                },
                xAxis: {
                    type: 'time',
                    boundaryGap: false,
                    min: xb.min,
                    max: xb.max,
                    axisLine: { lineStyle: { color: 'rgba(255,255,255,0.3)', width: 1 } },
                    axisTick: { show: true, interval: 5 },
                    axisLabel: {
                        fontSize: 8,
                        rotate: 0,
                        color: 'var(--text-secondary)',
                        margin: 8,
                        interval: 5,
                        formatter: function(value) {
                            const date = new Date(value);
                            const time = date.toLocaleTimeString('zh-CN', {
                                hour: '2-digit',
                                minute: '2-digit'
                            });
                            const dateStr = date.toLocaleDateString('zh-CN', {
                                month: '2-digit',
                                day: '2-digit'
                            });
                            return time + '\n' + dateStr;
                        }
                    }
                },
                yAxis: {
                    type: 'value',
                    name: '合格率(%)',
                    nameTextStyle: {
                        color: '#86909C'
                    },
                    axisLine: { lineStyle: { color: 'rgba(255,255,255,0.3)', width: 1 } },
                    axisLabel: {
                        fontSize: 10,
                        color: '#FFFFFF',
                        formatter: '{value}%'
                    },
                    splitLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.1)', width: 1 } },
                    min: 0,
                    max: 100
                },
                series: [
                    {
                        name: '设备合格率',
                        type: 'line',
                        data: [],
                        smooth: true,
                        connectNulls: false, // 不�接null值���免没有数据的点�接到0
                        itemStyle: { color: '#FFA940', borderWidth: 2 },
                        lineStyle: { color: '#FFA940', width: 1.5 },
                        areaStyle: {
                            color: {
                                type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
                                colorStops: [
                                    { offset: 0, color: '#FFA94080' },
                                    { offset: 1, color: '#FFA94000' }
                                ]
                            }
                        },
                        symbol: 'circle',
                        symbolSize: 3,
                        showSymbol: true,
                        emphasis: { symbolSize: 5, itemStyle: { color: '#FFA940' } },
                        markLine: {
                            silent: true,
                            data: [
                                {
                                    yAxis: 90,
                                    name: '合格�',
                                    lineStyle: {
                                        color: '#EF4444',
                                        type: 'dashed',
                                        width: 1.5
                                    },
                                    label: {
                                        formatter: '90%',
                                        color: '#EF4444'
                                    }
                                }
                            ]
                        }
                    }
                ]
            };

            deviceChart.setOption(deviceOption);

            // 初始化台垫合格率图表
            const matDom = document.getElementById('matChart');
            matChart = echarts.init(matDom);

            const matOption = {
                backgroundColor: 'var(--bg-card)',
                textStyle: { color: 'var(--text-light)', fontSize: 10 },
                title: {
                    text: '台垫合格率',
                    textStyle: {
                        color: '#FFFFFF',
                        fontSize: 16
                    }
                },
                tooltip: {
                    trigger: 'axis',
                    triggerOn: 'mousemove',
                    backgroundColor: 'rgba(15, 30, 53, 0.9)',
                    borderColor: '#FF6B6B',
                    borderWidth: 1,
                    textStyle: {
                        color: '#FFFFFF',
                        fontSize: 11
                    },
                    formatter: function(params) { return historyAxisTooltipFormatter(params, ''); },
                    axisPointer: {
                        type: 'cross',
                        snap: true,
                        triggerOn: 'mousemove',
                        lineStyle: {
                            color: '#FF6B6B',
                            type: 'dashed',
                            width: 1
                        },
                        crossStyle: {
                            color: '#FF6B6B'
                        }
                    }
                },
                grid: {
                    left: '4%',
                    right: '4%',
                    top: '15%',
                    bottom: '15%',
                    containLabel: true
                },
                xAxis: {
                    type: 'time',
                    boundaryGap: false,
                    min: xb.min,
                    max: xb.max,
                    axisLine: { lineStyle: { color: 'rgba(255,255,255,0.3)', width: 1 } },
                    axisTick: { show: true, interval: 5 },
                    axisLabel: {
                        fontSize: 8,
                        rotate: 0,
                        color: 'var(--text-secondary)',
                        margin: 8,
                        interval: 5,
                        formatter: function(value) {
                            const date = new Date(value);
                            const time = date.toLocaleTimeString('zh-CN', {
                                hour: '2-digit',
                                minute: '2-digit'
                            });
                            const dateStr = date.toLocaleDateString('zh-CN', {
                                month: '2-digit',
                                day: '2-digit'
                            });
                            return time + '\n' + dateStr;
                        }
                    }
                },
                yAxis: {
                    type: 'value',
                    name: '合格率(%)',
                    nameTextStyle: {
                        color: '#86909C'
                    },
                    axisLine: { lineStyle: { color: 'rgba(255,255,255,0.3)', width: 1 } },
                    axisLabel: {
                        fontSize: 10,
                        color: '#FFFFFF',
                        formatter: '{value}%'
                    },
                    splitLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.1)', width: 1 } },
                    min: 0,
                    max: 100
                },
                series: [
                    {
                        name: '台垫合格率',
                        type: 'line',
                        data: [],
                        smooth: true,
                        connectNulls: false, // 不�接null值���免没有数据的点�接到0
                        itemStyle: { color: '#FF6B6B', borderWidth: 2 },
                        lineStyle: { color: '#FF6B6B', width: 1.5 },
                        areaStyle: {
                            color: {
                                type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
                                colorStops: [
                                    { offset: 0, color: '#FF6B6B80' },
                                    { offset: 1, color: '#FF6B6B00' }
                                ]
                            }
                        },
                        symbol: 'circle',
                        symbolSize: 3,
                        showSymbol: true,
                        emphasis: { symbolSize: 5, itemStyle: { color: '#FF6B6B' } },
                        markLine: {
                            silent: true,
                            data: [
                                {
                                    yAxis: 90,
                                    name: '合格�',
                                    lineStyle: {
                                        color: '#EF4444',
                                        type: 'dashed',
                                        width: 1.5
                                    },
                                    label: {
                                        formatter: '90%',
                                        color: '#EF4444'
                                    }
                                }
                            ]
                        }
                    }
                ]
            };

            matChart.setOption(matOption);
        }

        // 初始化平均温度图表（与合格率图共用时间轴边界，使用数值避免 max 每次求值导致 tooltip 异常）
        const xbEnv = getHistoryXAxisBounds();
        const tempDom = document.getElementById('tempChart');
        tempChart = echarts.init(tempDom);

        const tempOption = {
            backgroundColor: 'var(--bg-card)',
            textStyle: { color: 'var(--text-light)', fontSize: 10 },
            title: {
                text: '平均温度',
                textStyle: {
                    color: '#FFFFFF',
                    fontSize: 16
                }
            },
            tooltip: {
                trigger: 'axis',
                triggerOn: 'mousemove',
                backgroundColor: 'rgba(15, 30, 53, 0.9)',
                borderColor: '#FF6B6B',
                borderWidth: 1,
                textStyle: {
                    color: '#FFFFFF',
                    fontSize: 11
                },
                formatter: function(params) { return historyAxisTooltipFormatter(params, ' °C'); },
                axisPointer: {
                    type: 'cross',
                    snap: true,
                    triggerOn: 'mousemove',
                    lineStyle: {
                        color: '#FF6B6B',
                        type: 'dashed',
                        width: 1
                    },
                    crossStyle: {
                        color: '#FF6B6B'
                    }
                }
            },
            grid: {
                left: '4%',
                right: '4%',
                top: '15%',
                bottom: '15%',
                containLabel: true
            },
            xAxis: {
                type: 'time',
                boundaryGap: false,
                min: xbEnv.min,
                max: xbEnv.max,
                axisLine: { lineStyle: { color: 'rgba(255,255,255,0.3)', width: 1 } },
                axisTick: { show: true, interval: 5 },
                axisLabel: {
                    fontSize: 8,
                    rotate: 0,
                    color: 'var(--text-secondary)',
                    margin: 8,
                    interval: 5,
                    formatter: function(value) {
                        const date = new Date(value);
                        const time = date.toLocaleTimeString('zh-CN', {
                            hour: '2-digit',
                            minute: '2-digit'
                        });
                        const dateStr = date.toLocaleDateString('zh-CN', {
                            month: '2-digit',
                            day: '2-digit'
                        });
                        return time + '\n' + dateStr;
                    }
                }
            },
            yAxis: {
                type: 'value',
                name: '温度(°C)',
                nameTextStyle: {
                    color: '#86909C'
                },
                axisLine: { lineStyle: { color: 'rgba(255,255,255,0.3)', width: 1 } },
                axisLabel: {
                    fontSize: 10,
                    color: '#FFFFFF',
                    formatter: '{value}°C'
                },
                splitLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.1)', width: 1 } },
                min: 0,
                max: 50,
                interval: 10
            },
            series: [
                {
                    name: '平均温度',
                    type: 'line',
                    data: [],
                    smooth: true,
                    connectNulls: true, // �接null值��确�非0点之间直接��
                    itemStyle: { color: '#FF6B6B', borderWidth: 2 },
                    lineStyle: { color: '#FF6B6B', width: 1.5 },
                    areaStyle: {
                        color: {
                            type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
                            colorStops: [
                                { offset: 0, color: '#FF6B6B80' },
                                { offset: 1, color: '#FF6B6B00' }
                            ]
                        }
                    },
                    symbol: 'circle',
                    symbolSize: 3,
                    showSymbol: true,
                    emphasis: { symbolSize: 5, itemStyle: { color: '#FF6B6B' } },
                    markLine: {
                        silent: true,
                        symbol: 'none',
                        data: [
                            {
                                yAxis: 28,
                                name: '上阈值 28°C',
                                lineStyle: {
                                    color: '#EF4444',
                                    type: 'dashed',
                                    width: 2
                                },
                                label: {
                                    show: true,
                                    formatter: '28°C',
                                    color: '#EF4444',
                                    fontSize: 10,
                                    fontWeight: 'bold',
                                    position: 'end'
                                }
                            },
                            {
                                yAxis: 20,
                                name: '下阈值 20°C',
                                lineStyle: {
                                    color: '#EF4444',
                                    type: 'dashed',
                                    width: 2
                                },
                                label: {
                                    show: true,
                                    formatter: '20°C',
                                    color: '#EF4444',
                                    fontSize: 10,
                                    fontWeight: 'bold',
                                    position: 'end'
                                }
                            }
                        ]
                    }
                }
            ]
        };

        tempChart.setOption(tempOption);

        // 初始化平均�度图表
        const humidityDom = document.getElementById('humidityChart');
        humidityChart = echarts.init(humidityDom);

        const humidityOption = {
        backgroundColor: 'var(--bg-card)',
        textStyle: { color: 'var(--text-light)', fontSize: 10 },
        title: {
            text: '平均湿度',
            textStyle: {
                color: '#FFFFFF',
                fontSize: 16
            }
        },
        tooltip: {
            trigger: 'axis',
            triggerOn: 'mousemove',
            backgroundColor: 'rgba(15, 30, 53, 0.9)',
            borderColor: '#4ECDC4',
            borderWidth: 1,
            textStyle: {
                color: '#FFFFFF',
                fontSize: 11
            },
            formatter: function(params) { return historyAxisTooltipFormatter(params, ' %'); },
            axisPointer: {
                type: 'cross',
                snap: true,
                triggerOn: 'mousemove',
                lineStyle: {
                    color: '#4ECDC4',
                    type: 'dashed',
                    width: 1
                },
                crossStyle: {
                    color: '#4ECDC4'
                }
            }
        },
        grid: {
            left: '4%',
            right: '4%',
            top: '15%',
            bottom: '15%',
            containLabel: true
        },
        xAxis: {
            type: 'time',
            boundaryGap: false,
            min: xbEnv.min,
            max: xbEnv.max,
            axisLine: { lineStyle: { color: 'rgba(255,255,255,0.3)', width: 1 } },
            axisTick: { show: true, interval: 5 },
                axisLabel: {
                    fontSize: 8,
                    rotate: 0,
                    color: 'var(--text-secondary)',
                    margin: 8,
                    interval: 5,
                    formatter: function(value) {
                        const date = new Date(value);
                        const time = date.toLocaleTimeString('zh-CN', {
                            hour: '2-digit',
                            minute: '2-digit'
                        });
                        const dateStr = date.toLocaleDateString('zh-CN', {
                            month: '2-digit',
                            day: '2-digit'
                        });
                        return time + '\n' + dateStr;
                    }
                }
            },
            yAxis: {
                type: 'value',
                name: '湿度(%)',
                nameTextStyle: {
                    color: '#86909C'
                },
                axisLine: { lineStyle: { color: 'rgba(255,255,255,0.3)', width: 1 } },
                axisLabel: {
                    fontSize: 10,
                    color: '#FFFFFF',
                    formatter: '{value}%'
                },
                splitLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.1)', width: 1 } },
                min: 0,
                max: 100,
                interval: 20
            },
            series: [
                {
                    name: '平均湿度',
                    type: 'line',
                    data: [],
                    smooth: true,
                    connectNulls: true, // �接null值��确�非0点之间直接��
                    itemStyle: { color: '#4ECDC4', borderWidth: 2 },
                    lineStyle: { color: '#4ECDC4', width: 1.5 },
                    areaStyle: {
                        color: {
                            type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
                            colorStops: [
                                { offset: 0, color: '#4ECDC480' },
                                { offset: 1, color: '#4ECDC400' }
                            ]
                        }
                    },
                    symbol: 'circle',
                    symbolSize: 3,
                    showSymbol: true,
                    emphasis: { symbolSize: 5, itemStyle: { color: '#4ECDC4' } },
                    markLine: {
                        silent: true,
                        symbol: 'none',
                        data: [
                            {
                                yAxis: 60,
                                name: '上阈值 60%',
                                lineStyle: {
                                    color: '#EF4444',
                                    type: 'dashed',
                                    width: 2
                                },
                                label: {
                                    show: true,
                                    formatter: '60%',
                                    color: '#EF4444',
                                    fontSize: 10,
                                    fontWeight: 'bold',
                                    position: 'end'
                                }
                            },
                            {
                                yAxis: 40,
                                name: '下阈值 40%',
                                lineStyle: {
                                    color: '#EF4444',
                                    type: 'dashed',
                                    width: 2
                                },
                                label: {
                                    show: true,
                                    formatter: '40%',
                                    color: '#EF4444',
                                    fontSize: 10,
                                    fontWeight: 'bold',
                                    position: 'end'
                                }
                            }
                        ]
                    }
                }
            ]
        };

        humidityChart.setOption(humidityOption);

        // 初始化平均洁净度图表
        const cleanlinessDom = document.getElementById('cleanlinessChart');
        cleanlinessChart = echarts.init(cleanlinessDom);

        const cleanlinessOption = {
        backgroundColor: 'var(--bg-card)',
        textStyle: { color: 'var(--text-light)', fontSize: 10 },
        title: {
            text: '平均洁净度',
            textStyle: {
                color: '#FFFFFF',
                fontSize: 16
            }
        },
        tooltip: {
        trigger: 'axis',
        triggerOn: 'mousemove',
        backgroundColor: 'rgba(15, 30, 53, 0.9)',
        borderColor: '#A78BFA',
        borderWidth: 1,
        textStyle: {
            color: '#FFFFFF',
            fontSize: 11
        },
        formatter: function(params) { return historyAxisTooltipFormatter(params, ' 个/m³'); },
        axisPointer: {
            type: 'cross',
            snap: true,
            triggerOn: 'mousemove',
            lineStyle: {
                color: '#A78BFA',
                type: 'dashed',
                width: 1
            },
            crossStyle: {
                color: '#A78BFA'
            }
        }
    },
    grid: {
            left: '4%',
            right: '4%',
            top: '15%',
            bottom: '15%',
            containLabel: true
        },
        xAxis: {
            type: 'time',
            boundaryGap: false,
            min: xbEnv.min,
            max: xbEnv.max,
            axisLine: { lineStyle: { color: 'rgba(255,255,255,0.3)', width: 1 } },
            axisTick: { show: true, interval: 5 },
                axisLabel: {
                    fontSize: 8,
                    rotate: 0,
                    color: 'var(--text-secondary)',
                    margin: 8,
                    interval: 5,
                    formatter: function(value) {
                        const date = new Date(value);
                        const time = date.toLocaleTimeString('zh-CN', {
                            hour: '2-digit',
                            minute: '2-digit'
                        });
                        const dateStr = date.toLocaleDateString('zh-CN', {
                            month: '2-digit',
                            day: '2-digit'
                        });
                        return time + '\n' + dateStr;
                    }
                }
            },
            yAxis: {
                type: 'value',
                name: '洁净度(个/m³)',
                nameTextStyle: {
                    color: '#86909C'
                },
                axisLine: { lineStyle: { color: 'rgba(255,255,255,0.3)', width: 1 } },
                axisLabel: {
                    fontSize: 10,
                    color: '#FFFFFF',
                    formatter: '{value}'
                },
                splitLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.1)', width: 1 } }
            },
            series: [
                {
                    name: '平均洁净度',
                    type: 'line',
                    data: [],
                    smooth: true,
                    connectNulls: true, // �接null值��确�非0点之间直接��
                    itemStyle: { color: '#A78BFA', borderWidth: 2 },
                    lineStyle: { color: '#A78BFA', width: 1.5 },
                    areaStyle: {
                        color: {
                            type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
                            colorStops: [
                                { offset: 0, color: '#A78BFA80' },
                                { offset: 1, color: '#A78BFA00' }
                            ]
                        }
                    },
                    symbol: 'circle',
                    symbolSize: 3,
                    showSymbol: true,
                    emphasis: { symbolSize: 5, itemStyle: { color: '#A78BFA' } },
                    markLine: {
                        silent: true,
                        data: [
                            {
                                yAxis: 250000,
                                name: '上阈值',
                                lineStyle: {
                                    color: '#A78BFA',
                                    type: 'dashed',
                                    width: 1.5
                                },
                                label: {
                                    formatter: '250000',
                                    color: '#A78BFA'
                                }
                            }
                        ]
                    }
                }
            ]
        };

        cleanlinessChart.setOption(cleanlinessOption);

        // 向后端发起请求获取历史数据
        async function fetchHistoryData(timeRange = 'day') {
            try {
                // 显示加载提示
                document.getElementById('wristLoading').style.display = 'block';
                document.getElementById('deviceLoading').style.display = 'block';
                document.getElementById('matLoading').style.display = 'block';
                document.getElementById('tempLoading').style.display = 'block';
                document.getElementById('humidityLoading').style.display = 'block';
                document.getElementById('cleanlinessLoading').style.display = 'block';

                // �用新的API接口获取数据
                const response = await fetch(`/history-chart-data?timeRange=${timeRange}`);
                if (!response.ok) {
                    throw new Error('历史数据请求失败');
                }

                const data = await response.json();

                // 更新各个图表
                if (data.success) {
                    updateWristChart({success: true, data: data.wristData, alarmData: data.alarmData});
                    updateDeviceChart({success: true, data: data.deviceData, alarmData: data.alarmData});
                    updateMatChart({success: true, data: data.matData, alarmData: data.alarmData});
                    updateTempChart({success: true, data: data.tempData, alarmData: data.alarmData});
                    updateHumidityChart({success: true, data: data.humidityData, alarmData: data.alarmData});
                    updateCleanlinessChart({success: true, data: data.cleanlinessData, alarmData: data.alarmData});
                } else {
                    updateWristChart({success: false, data: []});
                    updateDeviceChart({success: false, data: []});
                    updateMatChart({success: false, data: []});
                    updateTempChart({success: false, data: []});
                    updateHumidityChart({success: false, data: []});
                    updateCleanlinessChart({success: false, data: []});
                }

                // 隐藏加载提示
                document.getElementById('wristLoading').style.display = 'none';
                document.getElementById('deviceLoading').style.display = 'none';
                document.getElementById('matLoading').style.display = 'none';
                document.getElementById('tempLoading').style.display = 'none';
                document.getElementById('humidityLoading').style.display = 'none';
                document.getElementById('cleanlinessLoading').style.display = 'none';

            } catch (error) {
                updateWristChart({success: false, data: []});
                updateDeviceChart({success: false, data: []});
                updateMatChart({success: false, data: []});
                updateTempChart({success: false, data: []});
                updateHumidityChart({success: false, data: []});
                updateCleanlinessChart({success: false, data: []});
                document.getElementById('wristLoading').style.display = 'none';
                document.getElementById('deviceLoading').style.display = 'none';
                document.getElementById('matLoading').style.display = 'none';
                document.getElementById('tempLoading').style.display = 'none';
                document.getElementById('humidityLoading').style.display = 'none';
                document.getElementById('cleanlinessLoading').style.display = 'none';
            }
        }

        // 更新腕带合格率图表
        function updateWristChart(responseData) {
            if (!wristChart) return;

            const wristData = [];

            if (responseData.data && Array.isArray(responseData.data)) {
                responseData.data.forEach(point => {
                    // 将0值视为100%（百分之百）
                    const value = point[1] !== 0 ? point[1] : 100;
                    const date = new Date(point[0]);
                    wristData.push([+date, value]);
                });
            }

            // 处理报警数据��转换为markPoint
            const markPoints = [];
            if (responseData.alarmData && Array.isArray(responseData.alarmData)) {
                responseData.alarmData.forEach(alarm => {
                    const alarmTime = new Date(alarm.time);
                    markPoints.push({
                        name: alarm.person + ': ' + alarm.thing,
                        value: alarm.person + ': ' + alarm.thing,
                        xAxis: alarmTime,
                        yAxis: 'max',
                        itemStyle: {
                            color: '#f53f3f'
                        },
                        symbolSize: 6
                    });
                });
            }

            if (wristChart) {
                const xb = getHistoryXAxisBounds();
                wristChart.setOption({
                    xAxis: {
                        min: xb.min,
                        max: xb.max
                    },
                    series: [{
                        data: wristData.length > 0 ? wristData : [],
                        markPoint: {
                            silent: true,
                            data: markPoints,
                            symbol: 'pin',
                            label: {
                                show: true,
                                position: 'top',
                                fontSize: 10,
                                color: '#ffffff',
                                formatter: function(params) {
                                    return params.name.length > 15 ? params.name.substring(0, 15) + '...' : params.name;
                                }
                            }
                        }
                    }]
                });
                wristChart.resize();
            }
        }

        // 更新设备合格率图表
        function updateDeviceChart(responseData) {
            if (!deviceChart) return;

            const deviceData = [];

            if (responseData.data && Array.isArray(responseData.data)) {
                responseData.data.forEach(point => {
                    // 将0值视为100%（百分之百）
                    const value = point[1] !== 0 ? point[1] : 100;
                    const date = new Date(point[0]);
                    deviceData.push([+date, value]);
                });
            }

            // 处理报警数据��转换为markPoint
            const markPoints = [];
            if (responseData.alarmData && Array.isArray(responseData.alarmData)) {
                responseData.alarmData.forEach(alarm => {
                    const alarmTime = new Date(alarm.time);
                    markPoints.push({
                        name: alarm.person + ': ' + alarm.thing,
                        value: alarm.person + ': ' + alarm.thing,
                        xAxis: alarmTime,
                        yAxis: 'max',
                        itemStyle: {
                            color: '#f53f3f'
                        },
                        symbolSize: 6
                    });
                });
            }

            if (deviceChart) {
                const xb = getHistoryXAxisBounds();
                deviceChart.setOption({
                    xAxis: {
                        min: xb.min,
                        max: xb.max
                    },
                    series: [{
                        data: deviceData.length > 0 ? deviceData : [],
                        markPoint: {
                            silent: true,
                            data: markPoints,
                            symbol: 'pin',
                            label: {
                                show: true,
                                position: 'top',
                                fontSize: 10,
                                color: '#ffffff',
                                formatter: function(params) {
                                    return params.name.length > 15 ? params.name.substring(0, 15) + '...' : params.name;
                                }
                            }
                        }
                    }]
                });
                deviceChart.resize();
            }
        }

        // 更新台垫合格率图表
        function updateMatChart(responseData) {
            if (!matChart) return;

            const matData = [];

            if (responseData.data && Array.isArray(responseData.data)) {
                responseData.data.forEach(point => {
                    // 将0值视为100%（百分之百）
                    const value = point[1] !== 0 ? point[1] : 100;
                    const date = new Date(point[0]);
                    matData.push([+date, value]);
                });
            }

            // 处理报警数据��转换为markPoint
            const markPoints = [];
            if (responseData.alarmData && Array.isArray(responseData.alarmData)) {
                responseData.alarmData.forEach(alarm => {
                    const alarmTime = new Date(alarm.time);
                    markPoints.push({
                        name: alarm.person + ': ' + alarm.thing,
                        value: alarm.person + ': ' + alarm.thing,
                        xAxis: alarmTime,
                        yAxis: 'max',
                        itemStyle: {
                            color: '#f53f3f'
                        },
                        symbolSize: 6
                    });
                });
            }

            if (matChart) {
                const xb = getHistoryXAxisBounds();
                matChart.setOption({
                    xAxis: {
                        min: xb.min,
                        max: xb.max
                    },
                    series: [{
                        data: matData.length > 0 ? matData : [],
                        markPoint: {
                            silent: true,
                            data: markPoints,
                            symbol: 'pin',
                            label: {
                                show: true,
                                position: 'top',
                                fontSize: 10,
                                color: '#ffffff',
                                formatter: function(params) {
                                    return params.name.length > 15 ? params.name.substring(0, 15) + '...' : params.name;
                                }
                            }
                        }
                    }]
                });
                matChart.resize();
            }
        }

        // 更新平均温度图表
        function updateTempChart(responseData) {
            if (!tempChart) return;

            const tempData = [];

            if (responseData.data && Array.isArray(responseData.data)) {
                responseData.data.forEach(point => {
                    // 将0值视为100%（百分之百）
                    const value = point[1] !== 0 ? point[1] : 100;
                    const date = new Date(point[0]);
                    tempData.push([+date, value]);
                });
            }

            // 处理报警数据��转换为markPoint
            const markPoints = [];
            if (responseData.alarmData && Array.isArray(responseData.alarmData)) {
                responseData.alarmData.forEach(alarm => {
                    const alarmTime = new Date(alarm.time);
                    markPoints.push({
                        name: alarm.person + ': ' + alarm.thing,
                        value: alarm.person + ': ' + alarm.thing,
                        xAxis: alarmTime,
                        yAxis: 'max',
                        itemStyle: {
                            color: '#f53f3f'
                        },
                        symbolSize: 6
                    });
                });
            }

            if (tempChart) {
                const xb = getHistoryXAxisBounds();
                tempChart.setOption({
                    xAxis: {
                        min: xb.min,
                        max: xb.max
                    },
                    series: [{
                        data: tempData.length > 0 ? tempData : [],
                        markPoint: {
                            silent: true,
                            data: markPoints,
                            symbol: 'pin',
                            label: {
                                show: true,
                                position: 'top',
                                fontSize: 10,
                                color: '#ffffff',
                                formatter: function(params) {
                                    return params.name.length > 15 ? params.name.substring(0, 15) + '...' : params.name;
                                }
                            }
                        }
                    }]
                });
                tempChart.resize();
            }
        }

        // 更新平均湿度图表
        function updateHumidityChart(responseData) {
            if (!humidityChart) return;

            const humidityData = [];

            if (responseData.data && Array.isArray(responseData.data)) {
                responseData.data.forEach(point => {
                    // 将0值视为100%（百分之百）
                    const value = point[1] !== 0 ? point[1] : 100;
                    const date = new Date(point[0]);
                    humidityData.push([+date, value]);
                });
            }

            // 处理报警数据��转换为markPoint
            const markPoints = [];
            if (responseData.alarmData && Array.isArray(responseData.alarmData)) {
                responseData.alarmData.forEach(alarm => {
                    const alarmTime = new Date(alarm.time);
                    markPoints.push({
                        name: alarm.person + ': ' + alarm.thing,
                        value: alarm.person + ': ' + alarm.thing,
                        xAxis: alarmTime,
                        yAxis: 'max',
                        itemStyle: {
                            color: '#f53f3f'
                        },
                        symbolSize: 6
                    });
                });
            }

            if (humidityChart) {
                const xb = getHistoryXAxisBounds();
                humidityChart.setOption({
                    xAxis: {
                        min: xb.min,
                        max: xb.max
                    },
                    series: [{
                        data: humidityData.length > 0 ? humidityData : [],
                        markPoint: {
                            silent: true,
                            data: markPoints,
                            symbol: 'pin',
                            label: {
                                show: true,
                                position: 'top',
                                fontSize: 10,
                                color: '#ffffff',
                                formatter: function(params) {
                                    return params.name.length > 15 ? params.name.substring(0, 15) + '...' : params.name;
                                }
                            }
                        }
                    }]
                });
                humidityChart.resize();
            }
        }

        // 更新平均洁净度图表
        function updateCleanlinessChart(responseData) {
            if (!cleanlinessChart) return;

            const cleanlinessData = [];

            if (responseData.data && Array.isArray(responseData.data)) {
                responseData.data.forEach(point => {
                    // 将0值视为100%（百分之百）
                    const value = point[1] !== 0 ? point[1] : 100;
                    const date = new Date(point[0]);
                    cleanlinessData.push([+date, value]);
                });
            }

            // 处理报警数据��转换为markPoint
            const markPoints = [];
            if (responseData.alarmData && Array.isArray(responseData.alarmData)) {
                responseData.alarmData.forEach(alarm => {
                    const alarmTime = new Date(alarm.time);
                    markPoints.push({
                        name: alarm.person + ': ' + alarm.thing,
                        value: alarm.person + ': ' + alarm.thing,
                        xAxis: alarmTime,
                        yAxis: 'max',
                        itemStyle: {
                            color: '#f53f3f'
                        },
                        symbolSize: 6
                    });
                });
            }

            if (cleanlinessChart) {
                const xb = getHistoryXAxisBounds();
                cleanlinessChart.setOption({
                    xAxis: {
                        min: xb.min,
                        max: xb.max
                    },
                    series: [{
                        data: cleanlinessData.length > 0 ? cleanlinessData : [],
                        markPoint: {
                            silent: true,
                            data: markPoints,
                            symbol: 'pin',
                            label: {
                                show: true,
                                position: 'top',
                                fontSize: 10,
                                color: '#ffffff',
                                formatter: function(params) {
                                    return params.name.length > 15 ? params.name.substring(0, 15) + '...' : params.name;
                                }
                            }
                        }
                    }]
                });
                cleanlinessChart.resize();
            }
        }

        // 选择时间范围
        function selectTimeRange(timeRange) {
            // 更新当前时间范围
            currentTimeRange = timeRange;

            // 更新按钮状态
            document.querySelectorAll('.time-btn').forEach(btn => {
                btn.classList.remove('active');
            });
            document.querySelector(`[data-range="${timeRange}"]`).classList.add('active');

            // 重新获取数据
            fetchHistoryData(timeRange);
        }

        // �回�页
        function goBack() {
            window.location.href = '/';
        }

        // 获取并更新�题
        async function updateTheme() {
            try {
                const response = await fetch('/api/theme');
                const data = await response.json();
                const titleElement = document.querySelector('.title');
                if (titleElement) {
                    titleElement.textContent = '历史数据折线图';
                }
            } catch (error) {
            }
        }

        // 定期更新�题��每1秒��提高响应速度��
        // 当页面重新获得焦点时立即更新�题
        document.addEventListener('visibilitychange', function() {
            if (!document.hidden) {
                updateTheme();
            }
        });

        // 当窗口获得焦点时立即更新�题
        window.addEventListener('focus', function() {
            updateTheme();
        });

        // 页面加载完成后初始化
        document.addEventListener('DOMContentLoaded', function() {
            setTimeout(updateTheme, 500); // 减少��到500毫秒��确�页面�速加载
            initCharts();
            fetchHistoryData('day');
        });

        // 窗口大小改变时重新调整图表
        window.addEventListener('resize', function() {
            if (wristChart) {
                wristChart.resize();
            }
            if (deviceChart) {
                deviceChart.resize();
            }
            if (matChart) {
                matChart.resize();
            }
            if (tempChart) {
                tempChart.resize();
            }
            if (humidityChart) {
                humidityChart.resize();
            }
            if (cleanlinessChart) {
                cleanlinessChart.resize();
            }
        });
    </script>
</body>
</html>
)html";
    return html;
}

QString MyRequestHandler::generateQualifiedRateDataJson(const QString& timeRange)
{
    QJsonObject responseObj;

    // 计算时间范围��历史记录界面�用最�N小时/天
    QPair<QDateTime, QDateTime> timePair = calculateTimeRange(timeRange, false);
    QDateTime startTime = timePair.first;
    QDateTime endTime = timePair.second;

    int intervalMinutes = 60; // �认1小时

    if (timeRange == "day") {
        intervalMinutes = 10; // 每10分钟一个数据点
    } else if (timeRange == "week") {
        intervalMinutes = 60; // 每1小时一个数据点
    } else if (timeRange == "month") {
        intervalMinutes = 12 * 60; // 每12小时一个数据点
    } else if (timeRange == "90days") {
        intervalMinutes = 24 * 60; // 每24小时一个数据点
    } else {
        intervalMinutes = 10;
    }

    // 获取数据库�接
    DBManager* dbManager = DBManager::instance();
    if (!dbManager) {
        responseObj["success"] = false;
        responseObj["message"] = "数据库�接失败";
        return QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
    }

    // 查询合格率数据
    QList<QPair<QDateTime, double>> qualifiedRateData = dbManager->getQualifiedRateData(startTime, endTime, intervalMinutes);

    // �加QDebug输出��显示�数据库提取的数据�息
    qDebug() << "[�合格率历史数据] 时间范围:" << startTime.toString("yyyy-MM-dd hh:mm:ss") << "至" << endTime.toString("yyyy-MM-dd hh:mm:ss");
    qDebug() << "[�合格率历史数据] 数据点个数:" << qualifiedRateData.size();
    if (!qualifiedRateData.isEmpty()) {
        qDebug() << "[�合格率历史数据] 第一个数据点: 时间=" << qualifiedRateData.first().first.toString("yyyy-MM-dd hh:mm:ss") << ", 合格率=" << qualifiedRateData.first().second << "%";
        if (qualifiedRateData.size() > 1) {
            qDebug() << "[�合格率历史数据] 最后一个数据点: 时间=" << qualifiedRateData.last().first.toString("yyyy-MM-dd hh:mm:ss") << ", 合格率=" << qualifiedRateData.last().second << "%";
        }
    }

    // 构��回数据
    QJsonArray rateArray;
    for (const auto& data : qualifiedRateData) {
        QJsonArray point;
        point.append(data.first.toString("yyyy-MM-dd hh:mm:ss"));
        point.append(data.second); // 合格率��百分比��
        rateArray.append(point);
    }

    responseObj["success"] = true;
    responseObj["timeRange"] = timeRange;
    responseObj["startTime"] = startTime.toString("yyyy-MM-dd hh:mm:ss");
    responseObj["endTime"] = endTime.toString("yyyy-MM-dd hh:mm:ss");
    responseObj["data"] = rateArray;

    return QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
}

QString MyRequestHandler::generateWQualifiedRateDataJson(const QString& timeRange)
{
    QJsonObject responseObj;

    // 计算时间范围��历史记录界面�用最�N小时/天
    QPair<QDateTime, QDateTime> timePair = calculateTimeRange(timeRange, false);
    QDateTime startTime = timePair.first;
    QDateTime endTime = timePair.second;

    int intervalMinutes = 60;

    if (timeRange == "day") {
        intervalMinutes = 10;
    } else if (timeRange == "week") {
        intervalMinutes = 60;
    } else if (timeRange == "month") {
        intervalMinutes = 12 * 60;
    } else if (timeRange == "90days") {
        intervalMinutes = 24 * 60;
    } else {
        intervalMinutes = 10;
    }

    DBManager* dbManager = DBManager::instance();
    if (!dbManager) {
        responseObj["success"] = false;
        responseObj["message"] = "数据库�接失败";
        return QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
    }

    QList<QPair<QDateTime, double>> qualifiedRateData = dbManager->getWQualifiedRateData(startTime, endTime, intervalMinutes);

    QJsonArray rateArray;
    for (const auto& data : qualifiedRateData) {
        QJsonArray point;
        point.append(data.first.toString("yyyy-MM-dd hh:mm:ss"));
        point.append(data.second);
        rateArray.append(point);
    }

    responseObj["success"] = true;
    responseObj["timeRange"] = timeRange;
    responseObj["startTime"] = startTime.toString("yyyy-MM-dd hh:mm:ss");
    responseObj["endTime"] = endTime.toString("yyyy-MM-dd hh:mm:ss");
    responseObj["data"] = rateArray;

    return QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
}

QString MyRequestHandler::generateTQualifiedRateDataJson(const QString& timeRange)
{
    QJsonObject responseObj;

    // 计算时间范围��历史记录界面�用最�N小时/天
    QPair<QDateTime, QDateTime> timePair = calculateTimeRange(timeRange, false);
    QDateTime startTime = timePair.first;
    QDateTime endTime = timePair.second;

    int intervalMinutes = 60;

    if (timeRange == "day") {
        intervalMinutes = 10;
    } else if (timeRange == "week") {
        intervalMinutes = 60;
    } else if (timeRange == "month") {
        intervalMinutes = 12 * 60;
    } else if (timeRange == "90days") {
        intervalMinutes = 24 * 60;
    } else {
        intervalMinutes = 10;
    }

    DBManager* dbManager = DBManager::instance();
    if (!dbManager) {
        responseObj["success"] = false;
        responseObj["message"] = "数据库�接失败";
        return QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
    }

    QList<QPair<QDateTime, double>> qualifiedRateData = dbManager->getTQualifiedRateData(startTime, endTime, intervalMinutes);

    QJsonArray rateArray;
    for (const auto& data : qualifiedRateData) {
        QJsonArray point;
        point.append(data.first.toString("yyyy-MM-dd hh:mm:ss"));
        point.append(data.second);
        rateArray.append(point);
    }

    responseObj["success"] = true;
    responseObj["timeRange"] = timeRange;
    responseObj["startTime"] = startTime.toString("yyyy-MM-dd hh:mm:ss");
    responseObj["endTime"] = endTime.toString("yyyy-MM-dd hh:mm:ss");
    responseObj["data"] = rateArray;

    return QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
}

QString MyRequestHandler::generateEQualifiedRateDataJson(const QString& timeRange)
{
    QJsonObject responseObj;

    // 计算时间范围��历史记录界面�用最�N小时/天
    QPair<QDateTime, QDateTime> timePair = calculateTimeRange(timeRange, false);
    QDateTime startTime = timePair.first;
    QDateTime endTime = timePair.second;

    int intervalMinutes = 60;

    if (timeRange == "day") {
        intervalMinutes = 10;
    } else if (timeRange == "week") {
        intervalMinutes = 60;
    } else if (timeRange == "month") {
        intervalMinutes = 12 * 60;
    } else if (timeRange == "90days") {
        intervalMinutes = 24 * 60;
    } else {
        intervalMinutes = 10;
    }

    DBManager* dbManager = DBManager::instance();
    if (!dbManager) {
        responseObj["success"] = false;
        responseObj["message"] = "数据库�接失败";
        return QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
    }

    QList<QPair<QDateTime, double>> qualifiedRateData = dbManager->getEQualifiedRateData(startTime, endTime, intervalMinutes);

    QJsonArray rateArray;
    for (const auto& data : qualifiedRateData) {
        QJsonArray point;
        point.append(data.first.toString("yyyy-MM-dd hh:mm:ss"));
        point.append(data.second);
        rateArray.append(point);
    }

    responseObj["success"] = true;
    responseObj["timeRange"] = timeRange;
    responseObj["startTime"] = startTime.toString("yyyy-MM-dd hh:mm:ss");
    responseObj["endTime"] = endTime.toString("yyyy-MM-dd hh:mm:ss");
    responseObj["data"] = rateArray;

    return QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
}

QString MyRequestHandler::generateTempDataJson(const QString& timeRange)
{
    QJsonObject responseObj;

    // 计算时间范围��历史记录界面�用最�N小时/天
    QPair<QDateTime, QDateTime> timePair = calculateTimeRange(timeRange, false);
    QDateTime startTime = timePair.first;
    QDateTime endTime = timePair.second;

    int intervalMinutes = 60;

    if (timeRange == "day") {
        intervalMinutes = 10;
    } else if (timeRange == "week") {
        intervalMinutes = 60;
    } else if (timeRange == "month") {
        intervalMinutes = 12 * 60;
    } else if (timeRange == "90days") {
        intervalMinutes = 24 * 60;
    } else {
        intervalMinutes = 10;
    }

    DBManager* dbManager = DBManager::instance();
    if (!dbManager) {
        responseObj["success"] = false;
        responseObj["message"] = "数据库�接失败";
        return QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
    }

    QList<QPair<QDateTime, double>> tempData = dbManager->getTempHistoryData(startTime, endTime, intervalMinutes);

    QJsonArray dataArray;
    for (const auto& data : tempData) {
        QJsonArray point;
        point.append(data.first.toString("yyyy-MM-dd hh:mm:ss"));
        point.append(data.second);
        dataArray.append(point);
    }

    responseObj["success"] = true;
    responseObj["timeRange"] = timeRange;
    responseObj["startTime"] = startTime.toString("yyyy-MM-dd hh:mm:ss");
    responseObj["endTime"] = endTime.toString("yyyy-MM-dd hh:mm:ss");
    responseObj["data"] = dataArray;

    return QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
}

QString MyRequestHandler::generateHumidityDataJson(const QString& timeRange)
{
    QJsonObject responseObj;

    // 计算时间范围��历史记录界面�用最�N小时/天
    QPair<QDateTime, QDateTime> timePair = calculateTimeRange(timeRange, false);
    QDateTime startTime = timePair.first;
    QDateTime endTime = timePair.second;

    int intervalMinutes = 60;

    if (timeRange == "day") {
        intervalMinutes = 10;
    } else if (timeRange == "week") {
        intervalMinutes = 60;
    } else if (timeRange == "month") {
        intervalMinutes = 12 * 60;
    } else if (timeRange == "90days") {
        intervalMinutes = 24 * 60;
    } else {
        intervalMinutes = 10;
    }

    DBManager* dbManager = DBManager::instance();
    if (!dbManager) {
        responseObj["success"] = false;
        responseObj["message"] = "数据库�接失败";
        return QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
    }

    QList<QPair<QDateTime, double>> humidityData = dbManager->getHumidityHistoryData(startTime, endTime, intervalMinutes);

    QJsonArray dataArray;
    for (const auto& data : humidityData) {
        QJsonArray point;
        point.append(data.first.toString("yyyy-MM-dd hh:mm:ss"));
        point.append(data.second);
        dataArray.append(point);
    }

    responseObj["success"] = true;
    responseObj["timeRange"] = timeRange;
    responseObj["startTime"] = startTime.toString("yyyy-MM-dd hh:mm:ss");
    responseObj["endTime"] = endTime.toString("yyyy-MM-dd hh:mm:ss");
    responseObj["data"] = dataArray;

    return QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
}

QString MyRequestHandler::generateCleanlinessDataJson(const QString& timeRange)
{
    QJsonObject responseObj;

    // 计算时间范围��历史记录界面�用最�N小时/天
    QPair<QDateTime, QDateTime> timePair = calculateTimeRange(timeRange, false);
    QDateTime startTime = timePair.first;
    QDateTime endTime = timePair.second;

    int intervalMinutes = 60;

    if (timeRange == "day") {
        intervalMinutes = 10;
    } else if (timeRange == "week") {
        intervalMinutes = 60;
    } else if (timeRange == "month") {
        intervalMinutes = 12 * 60;
    } else if (timeRange == "90days") {
        intervalMinutes = 24 * 60;
    } else {
        intervalMinutes = 10;
    }

    DBManager* dbManager = DBManager::instance();
    if (!dbManager) {
        responseObj["success"] = false;
        responseObj["message"] = "数据库�接失败";
        return QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
    }

    QList<QPair<QDateTime, double>> cleanlinessData = dbManager->getCleanlinessHistoryData(startTime, endTime, intervalMinutes);

    QJsonArray dataArray;
    for (const auto& data : cleanlinessData) {
        QJsonArray point;
        point.append(data.first.toString("yyyy-MM-dd hh:mm:ss"));
        point.append(data.second);
        dataArray.append(point);
    }

    responseObj["success"] = true;
    responseObj["timeRange"] = timeRange;
    responseObj["startTime"] = startTime.toString("yyyy-MM-dd hh:mm:ss");
    responseObj["endTime"] = endTime.toString("yyyy-MM-dd hh:mm:ss");
    responseObj["data"] = dataArray;

    return QJsonDocument(responseObj).toJson(QJsonDocument::Compact);
}

// 异步查询�务�实现
MyRequestHandler::EnvDataQueryTask::EnvDataQueryTask(MyRequestHandler* handler, const QDateTime& start, const QDateTime& end, int interval)
    : m_handler(handler), m_start(start), m_end(end), m_interval(interval)
{
    // 自动删除�务对象
    setAutoDelete(true);
}

void MyRequestHandler::EnvDataQueryTask::run()
{
    if (!m_handler) return;

    // 查询环境历史数据
    DBManager* dbManager = DBManager::instance();
    if (!dbManager) return;

    // 查询各环境参数的历史数据
    QList<QPair<QDateTime, double>> tempData = dbManager->getTempHistoryData(m_start, m_end, m_interval);
    QList<QPair<QDateTime, double>> humidityData = dbManager->getHumidityHistoryData(m_start, m_end, m_interval);
    QList<QPair<QDateTime, double>> cleanlinessData = dbManager->getCleanlinessHistoryData(m_start, m_end, m_interval);

    // 转换为QJsonArray
    QJsonArray timeArray;
    QJsonArray tempArray;
    QJsonArray humidityArray;
    QJsonArray cleanlinessArray;

    // 构�时间轴和数据数�
    for (const auto& data : tempData) {
        timeArray.append(data.first.toString("yyyy-MM-dd HH:mm:ss"));
        tempArray.append(data.second);
    }

    // 填充�度数据
    for (const auto& data : humidityData) {
        humidityArray.append(data.second);
    }

    // 填充洁净度数据
    for (const auto& data : cleanlinessData) {
        cleanlinessArray.append(data.second);
    }

    // 发送�号通知数据准备完成
    emit m_handler->envHistoryDataReady(timeArray, tempArray, humidityArray, cleanlinessArray);
}

// 处理异步响应的函数
void MyRequestHandler::handleAsyncResponse(HttpResponse* response, const QJsonArray& timeArray, const QJsonArray& tempArray, const QJsonArray& humidityArray, const QJsonArray& cleanlinessArray)
{
    if (!response) return;

    // 构�响应JSON
    QJsonObject responseObj;
    responseObj["time"] = timeArray;
    responseObj["temperature"] = tempArray;
    responseObj["humidity"] = humidityArray;
    responseObj["cleanliness"] = cleanlinessArray;

    // 设置响应头和响应体
    response->setHeader("Content-Type", "application/json; charset=utf-8");
    response->write(QJsonDocument(responseObj).toJson(QJsonDocument::Compact), true);

    // 发送�号通知事�循环可�退出
    emit envHistoryDataReady(timeArray, tempArray, humidityArray, cleanlinessArray);
}

// 处理环境历史数据准备完成的槽函数
void MyRequestHandler::onEnvHistoryDataReady(const QJsonArray& timeArray, const QJsonArray& tempArray, const QJsonArray& humidityArray, const QJsonArray& cleanlinessArray)
{
    // 由于stefanfrings框架的限制��无法直接在槽函数中�问response对象
    // 需要�用事�循环等待异步查询完成
    emit envHistoryDataReady(timeArray, tempArray, humidityArray, cleanlinessArray);
}

// 通知�题变化
void MyRequestHandler::notifyThemeChange(const QString& theme)
{
    currentTheme = theme;
}

void MyRequestHandler::notifyDisplayModeChange(bool separateEnvEsd)
{
    m_separateEnvEsd = separateEnvEsd;
}

void MyRequestHandler::handleMesReadingsQuery(HttpRequest& request, HttpResponse& response)
{
    QJsonObject errorObj;
    if (!validateMesApiKey(request, errorObj)) {
        writeMesJson(response, errorObj, 401);
        return;
    }

    const QByteArray requestBody = request.getBody();
    const QJsonDocument doc = QJsonDocument::fromJson(requestBody);
    if (!doc.isObject()) {
        errorObj.insert(QStringLiteral("success"), false);
        errorObj.insert(QStringLiteral("code"), QStringLiteral("INVALID_JSON"));
        errorObj.insert(QStringLiteral("message"), QStringLiteral("请求体必须是 JSON 对象"));
        writeMesJson(response, errorObj, 400);
        return;
    }

    const QJsonObject body = doc.object();
    const QString requestId = body.value(QStringLiteral("requestId")).toString();

    MesReadingQueryFilter filter;
    QString errorCode;
    QString errorMessage;
    if (!parseMesQueryRequest(body, filter, errorCode, errorMessage)) {
        errorObj.insert(QStringLiteral("success"), false);
        errorObj.insert(QStringLiteral("code"), errorCode);
        errorObj.insert(QStringLiteral("message"), errorMessage);
        if (!requestId.isEmpty()) {
            errorObj.insert(QStringLiteral("requestId"), requestId);
        }
        writeMesJson(response, errorObj, 400);
        return;
    }

    DBManager* dbManager = DBManager::instance();
    if (!dbManager) {
        errorObj.insert(QStringLiteral("success"), false);
        errorObj.insert(QStringLiteral("code"), QStringLiteral("DB_UNAVAILABLE"));
        errorObj.insert(QStringLiteral("message"), QStringLiteral("数据库不可用"));
        if (!requestId.isEmpty()) {
            errorObj.insert(QStringLiteral("requestId"), requestId);
        }
        writeMesJson(response, errorObj, 500);
        return;
    }

    const MesReadingQueryResult queryResult = dbManager->queryMesReadings(filter);
    if (!queryResult.success) {
        errorObj.insert(QStringLiteral("success"), false);
        errorObj.insert(QStringLiteral("code"), queryResult.errorCode);
        errorObj.insert(QStringLiteral("message"), queryResult.errorMessage);
        if (!requestId.isEmpty()) {
            errorObj.insert(QStringLiteral("requestId"), requestId);
        }
        writeMesJson(response, errorObj, 500);
        return;
    }

    QJsonArray items;
    for (const MesReadingRow& row : queryResult.items) {
        QJsonObject item;
        item.insert(QStringLiteral("pointId"), row.pointId);
        item.insert(QStringLiteral("recordTime"), row.recordTime.toString(Qt::ISODate));
        if (row.hasValueNum) {
            item.insert(QStringLiteral("valueNum"), row.valueNum);
        } else {
            item.insert(QStringLiteral("valueNum"), QJsonValue::Null);
        }
        item.insert(QStringLiteral("statusDesc"), row.statusDesc);
        items.append(item);
    }

    QJsonObject responseObj;
    responseObj.insert(QStringLiteral("success"), true);
    if (!requestId.isEmpty()) {
        responseObj.insert(QStringLiteral("requestId"), requestId);
    }
    responseObj.insert(QStringLiteral("query"), buildQueryEcho(queryResult.appliedFilter));
    responseObj.insert(QStringLiteral("total"), queryResult.total);
    responseObj.insert(QStringLiteral("page"), queryResult.appliedFilter.page);
    if (queryResult.appliedFilter.allPageSize) {
        responseObj.insert(QStringLiteral("pageSize"), QStringLiteral("ALL"));
    } else {
        responseObj.insert(QStringLiteral("pageSize"), queryResult.appliedFilter.pageSize);
    }
    responseObj.insert(QStringLiteral("items"), items);
    writeMesJson(response, responseObj, 200);
}

