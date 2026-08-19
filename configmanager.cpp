#include "configmanager.h"
#include <QCoreApplication>

configmanager::configmanager(QObject *parent) : QObject(parent)
{
    // 配置文件路径：exe同目录/config.json
    QString appDir = QCoreApplication::applicationDirPath();
    m_configPath = appDir + "/config.json";
    qDebug() << "[ConfigManager] 配置文件路径:" << m_configPath;
}

// 保存配置到JSON文件
bool configmanager::saveConfig(const QList<ImageConfig>& imageConfigs, int currentImageIndex,
                               const SerialConfig& serialConfig, bool separateEnvEsd,
                               const QList<DisplayScreenConfig>& displayScreens)
{
    QJsonObject rootObj;

    // 1. 保存当前选中的图片索引
    rootObj["currentImageIndex"] = currentImageIndex;

    // 2. 保存所有图片和设备数据
    QJsonArray imageArray;
    for (const ImageConfig& imgConfig : imageConfigs) {
        QJsonObject imgObj;
        imgObj["imagePath"] = imgConfig.imagePath;
        imgObj["theme"] = imgConfig.theme; // 保存主题

        // 保存该图片的所有设备
        QJsonArray deviceArray;
        for (const DeviceConfig& devConfig : imgConfig.devices) {
            QJsonObject devObj;
            devObj["type"] = devConfig.type;
            devObj["id"] = devConfig.id;
            devObj["x"] = devConfig.x;
            devObj["y"] = devConfig.y;
            deviceArray.append(devObj);
        }
        imgObj["devices"] = deviceArray;
        imageArray.append(imgObj);
    }
    rootObj["images"] = imageArray;

    // 3. 保存串口配置
    QJsonObject serialObj;
    serialObj["port1"] = serialConfig.port1;
    serialObj["port2"] = serialConfig.port2;
    serialObj["baudRate"] = serialConfig.baudRate;
    serialObj["sendInterval"] = serialConfig.sendInterval;
    serialObj["overtime"] = serialConfig.overtime;
    serialObj["maxResend"] = serialConfig.maxResend;
    serialObj["delay"] = serialConfig.delay;
    serialObj["connectionType"] = serialConfig.connectionType;
    serialObj["serialMode"] = serialConfig.serialMode;
    serialObj["tcpServerIp"] = serialConfig.tcpServerIp;
    serialObj["tcpServerPort"] = serialConfig.tcpServerPort;
    rootObj["serialConfig"] = serialObj;
    rootObj["separateEnvEsd"] = separateEnvEsd;

    // 4. 保存显示屏 IP → 背景图映射（支持多图轮换）
    QJsonArray screenArray;
    for (const DisplayScreenConfig& screen : displayScreens) {
        if (screen.ip.trimmed().isEmpty()) {
            continue;
        }
        QJsonObject screenObj;
        screenObj["name"] = screen.name;
        screenObj["ip"] = screen.ip.trimmed();
        QJsonArray pathsArr;
        for (const QString& p : screen.imagePaths) {
            if (!p.trimmed().isEmpty()) {
                pathsArr.append(p);
            }
        }
        screenObj["imagePaths"] = pathsArr;
        // 兼容旧版单字段
        if (!screen.imagePaths.isEmpty()) {
            screenObj["imagePath"] = screen.imagePaths.first();
        }
        screenObj["switchSeconds"] = screen.switchSeconds > 0 ? screen.switchSeconds : 10;
        screenArray.append(screenObj);
    }
    rootObj["displayScreens"] = screenArray;

    // 5. 写入文件
    QFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "[ConfigManager] 保存配置失败：无法打开文件" << file.errorString();
        return false;
    }

    QJsonDocument doc(rootObj);
    file.write(doc.toJson(QJsonDocument::Indented)); // 格式化输出，方便查看
    file.close();
    qDebug() << "[ConfigManager] 配置保存成功！图片数量:" << imageConfigs.size()
             << "，当前选中索引:" << currentImageIndex
             << "，显示屏映射:" << screenArray.size();
    return true;
}

// 从JSON文件读取配置
bool configmanager::loadConfig(QList<ImageConfig>& imageConfigs, int& currentImageIndex,
                               SerialConfig& serialConfig, bool& separateEnvEsd,
                               QList<DisplayScreenConfig>& displayScreens)
{
    // 清空原有数据
    imageConfigs.clear();
    displayScreens.clear();
    currentImageIndex = -1;

    // 检查文件是否存在
    QFile file(m_configPath);
    if (!file.exists()) {
        qDebug() << "[ConfigManager] 配置文件不存在，将创建新配置";
        return false;
    }

    // 打开文件
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "[ConfigManager] 读取配置失败：无法打开文件" << file.errorString();
        return false;
    }

    // 解析JSON
    QByteArray data = file.readAll();
    file.close();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        qDebug() << "[ConfigManager] 读取配置失败：JSON格式错误";
        return false;
    }

    QJsonObject rootObj = doc.object();

    // 1. 读取当前选中的图片索引
    currentImageIndex = rootObj["currentImageIndex"].toInt(-1);

    // 2. 读取所有图片和设备数据
    QJsonArray imageArray = rootObj["images"].toArray();
    for (const QJsonValue& imgVal : imageArray) {
        QJsonObject imgObj = imgVal.toObject();
        ImageConfig imgConfig;
        imgConfig.imagePath = imgObj["imagePath"].toString();
        imgConfig.theme = imgObj["theme"].toString(); // 读取主题

        // 读取该图片的所有设备
        QJsonArray deviceArray = imgObj["devices"].toArray();
        for (const QJsonValue& devVal : deviceArray) {
            QJsonObject devObj = devVal.toObject();
            DeviceConfig devConfig;
            devConfig.type = devObj["type"].toString();
            devConfig.id = devObj["id"].toString();
            devConfig.x = devObj["x"].toInt();
            devConfig.y = devObj["y"].toInt();
            imgConfig.devices.append(devConfig);
        }

        imageConfigs.append(imgConfig);
    }

    // 3. 读取串口配置
    if (rootObj.contains("serialConfig")) {
        QJsonObject serialObj = rootObj["serialConfig"].toObject();
        serialConfig.port1 = serialObj["port1"].toString();
        serialConfig.port2 = serialObj["port2"].toString();
        serialConfig.baudRate = serialObj["baudRate"].toString();
        serialConfig.sendInterval = serialObj["sendInterval"].toString();
        serialConfig.overtime = serialObj["overtime"].toString();
        serialConfig.maxResend = serialObj["maxResend"].toString();
        serialConfig.delay = serialObj["delay"].toString();
        serialConfig.connectionType = serialObj["connectionType"].toInt(0); // 默认0，表示串口通信
        serialConfig.serialMode = serialObj["serialMode"].toInt(0); // 默认0，单串口
        serialConfig.tcpServerIp = serialObj["tcpServerIp"].toString();
        serialConfig.tcpServerPort = serialObj["tcpServerPort"].toString();
    }

    separateEnvEsd = rootObj.contains("separateEnvEsd")
        ? rootObj["separateEnvEsd"].toBool(true)
        : true;

    // 4. 读取显示屏映射
    if (rootObj.contains("displayScreens")) {
        QJsonArray screenArray = rootObj["displayScreens"].toArray();
        for (const QJsonValue& screenVal : screenArray) {
            QJsonObject screenObj = screenVal.toObject();
            DisplayScreenConfig screen;
            screen.name = screenObj["name"].toString();
            screen.ip = screenObj["ip"].toString().trimmed();
            screen.switchSeconds = screenObj["switchSeconds"].toInt(10);
            if (screen.switchSeconds <= 0) {
                screen.switchSeconds = 10;
            }

            if (screenObj.contains("imagePaths") && screenObj["imagePaths"].isArray()) {
                for (const QJsonValue& pv : screenObj["imagePaths"].toArray()) {
                    const QString path = pv.toString().trimmed();
                    if (!path.isEmpty()) {
                        screen.imagePaths.append(path);
                    }
                }
            }
            // 兼容旧字段 imagePath / imageIndex
            if (screen.imagePaths.isEmpty()) {
                const QString single = screenObj["imagePath"].toString().trimmed();
                if (!single.isEmpty()) {
                    screen.imagePaths.append(single);
                } else if (screenObj.contains("imageIndex")) {
                    const int idx = screenObj["imageIndex"].toInt(-1);
                    if (idx >= 0 && idx < imageConfigs.size()) {
                        screen.imagePaths.append(imageConfigs[idx].imagePath);
                    }
                }
            }

            if (!screen.ip.isEmpty() && !screen.imagePaths.isEmpty()) {
                displayScreens.append(screen);
            }
        }
    }

    qDebug() << "[ConfigManager] 配置读取成功！加载图片数量:" << imageConfigs.size()
             << "，上次选中索引:" << currentImageIndex
             << "，显示屏映射:" << displayScreens.size();
    return true;
}
