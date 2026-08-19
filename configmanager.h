#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

// 设备数据结构体（和 MainWindow 中的 DeviceWidget 对应）
struct DeviceConfig {
    QString type;       // 设备类型（W/T/E）
    QString id;         // 设备ID（W1/T2/E3）
    int x;              // 设备绝对X坐标（像素）
    int y;              // 设备绝对Y坐标（像素）
};

// 串口配置结构体
struct SerialConfig {
    QString port1;         // 线程一COM口
    QString port2;         // 线程二COM口
    QString baudRate;      // 波特率
    QString sendInterval;  // 发送间隔（lineEdit）
    QString overtime;      // 超时时间（lineEdit_2）
    QString maxResend;     // 最大重发次数（lineEdit_3）
    QString delay;         // 延迟时间（lineEdit_4）
    int connectionType;    // 连接类型：0=串口通信，1=网络通信(Tcp Server)
    int serialMode;        // 串口模式：0=单串口，1=双串口
    QString tcpServerIp;   // TCP Server IP地址
    QString tcpServerPort; // TCP Server 端口
};

// 图片+设备配置结构体
struct ImageConfig {
    QString imagePath;  // 图片路径
    QString theme;      // 图片主题
    QList<DeviceConfig> devices; // 该图片对应的所有设备
};

// 显示屏映射：客户端 IP → 一张或多张背景图（多张时按间隔轮换）
struct DisplayScreenConfig {
    QString name;              // 备注名称（如「二楼大屏」）
    QString ip;                // 显示机固定 IP
    QStringList imagePaths;    // 绑定的背景图路径（可多张轮换）
    int switchSeconds = 10;    // 多图轮换间隔（秒），仅 1 张时忽略
};

class configmanager : public QObject
{
    Q_OBJECT
public:
    explicit configmanager(QObject *parent = nullptr);

    // 保存配置（图片列表+所有设备+当前选中索引+串口参数+显示模式+显示屏映射）
    bool saveConfig(const QList<ImageConfig>& imageConfigs, int currentImageIndex,
                    const SerialConfig& serialConfig, bool separateEnvEsd = true,
                    const QList<DisplayScreenConfig>& displayScreens = QList<DisplayScreenConfig>());

    // 读取配置（返回图片列表+上次选中的图片索引+串口参数+显示模式+显示屏映射）
    bool loadConfig(QList<ImageConfig>& imageConfigs, int& currentImageIndex,
                    SerialConfig& serialConfig, bool& separateEnvEsd,
                    QList<DisplayScreenConfig>& displayScreens);

private:
    QString m_configPath; // 配置文件路径（exe同目录/config.json）
};

#endif // CONFIGMANAGER_H
