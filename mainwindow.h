#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QString>
#include <QDateTime>
// 原有头文件包含不变（记得保留 QMouseEvent、QPainter、QPaintEvent）
#include <QMainWindow>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QDebug>
#include <QInputDialog>
#include <QMessageBox>
#include <QAction>
#include <QDialog>
#include <QTimer>
#include <QQueue>
#include <QMutex>
#include <QVector>
#include <QJsonObject>
#include <QPoint>
#include <QList>
#include <QtMath>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QFileDialog>
#include "pollconfig.h"
#include "uistyle.h"
#include <QPixmap>
#include <QLabel>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSqlDatabase>  // 新增
#include <QSqlQuery>     // 新增
#include <QSqlError>
#include <QSystemTrayIcon>  // 新增：托盘图标
#include <QMenu>           // 新增：菜单
#include <QCloseEvent>     // 新增：关闭事件
#include <QtCharts>
#include <QChartView>
#include <QLineSeries>
#include <QDateTimeAxis>
#include <QValueAxis>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
using namespace cv;
#include "newdialog.h"
#include "myrequesthandler.h"
#include "serialworker.h"
#include "configmanager.h" // 引入小写配置管理头文件
#include "dbmanager.h"

// 自定义可拖动设备控件（新增位置变化信号，用于触发保存配置）
class DeviceWidget : public QWidget {
    Q_OBJECT
public:
    QString simpleEncrypt(const QString& content);
    QDateTime getFirstRunTime();
    bool checkTrialExpired();

    DeviceWidget(const QString& type, const QString& id, QWidget* parent = nullptr)
        : QWidget(parent), m_type(type), m_id(id), m_isSelected(false) {
        setFixedSize(40, 40);
        setCursor(Qt::PointingHandCursor);
    }
    QString getType() const { return m_type; }
    QString getId() const { return m_id; }
    QPoint getPos() const { return m_pos; }
    bool isSelected() const { return m_isSelected; }
    bool isOnline() const { return m_isOnline; }
    void setSelected(bool selected) {
        m_isSelected = selected;
        update(); // 触发重绘以显示选中状态
    }
    void setOnline(bool online) {
        m_isOnline = online;
        update(); // 触发重绘以显示在线状态
    }
    void setPos(const QPoint& pos) {
        m_pos = pos;
        move(pos - QPoint(20, 20)); // 调整控件位置，使中心对齐pos
    }

    QString getInfo() { return QString("%1,%2,%3,%4").arg(m_type).arg(m_id).arg(m_pos.x()).arg(m_pos.y()); }
    QMap<QString, QList<QString>> getAddressFuncData() const { return addressFuncData; }
signals:
    void posChanged(); // 设备位置改变时触发
    void deviceDoubleClicked(DeviceWidget* device); // 新增：双击事件信号

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) m_offset = e->pos();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (e->buttons() & Qt::LeftButton) {
            move(parentWidget()->mapFromGlobal(QCursor::pos()) - m_offset);
            m_pos = pos() + QPoint(20, 20);
        }
    }
    void mouseReleaseEvent(QMouseEvent *event) override { // 新增：鼠标释放时发射信号
        Q_UNUSED(event);
        emit posChanged();
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        Q_UNUSED(event);
        emit deviceDoubleClicked(this); // 发射双击信号
    }
    void paintEvent(QPaintEvent* e) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // 根据设备类型设置颜色
        if (m_type == "W") p.setBrush(Qt::blue);
        else if (m_type == "T") p.setBrush(Qt::green);
        else if (m_type == "E") p.setBrush(Qt::red);
        else if (m_type == "I" || m_type == "C") {
            // 离子风机和尘埃粒子计数器根据在线状态设置颜色
            p.setBrush(m_isOnline ? Qt::cyan : Qt::gray);
        }

        // 如果被选中，绘制选中边框
        if (m_isSelected) {
            QPen selectedPen(Qt::yellow, 3);
            p.setPen(selectedPen);
        } else {
            p.setPen(Qt::NoPen);
        }

        p.drawEllipse(2, 2, 36, 36);

        // 绘制设备ID文字
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter, m_id);
    }

private:
    QMap<QString, QList<QString>> addressFuncData;
    QString m_type;
    QString m_id;
    QPoint m_pos;
    QPoint m_offset;
    bool m_isSelected; // 新增：选中状态
    bool m_isOnline = true; // 新增：在线状态，默认在线

};
struct ImageDeviceData {
    QString imagePath;       // 图片路径
    QString theme;           // 图片主题
    QList<DeviceWidget*> devices; // 该图片对应的设备
};

class SerialWorker;

enum class ToolCommandContext {
    None,
    DeviceTestQuery,
    DeviceTestApply,
    AddrQuery,
    AddrApply
};

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:

    QString simpleEncrypt(const QString& content); // 加密函数声明
        QDateTime getFirstRunTime(); // 首次运行时间函数声明
        bool checkTrialExpired(); // 试用期检查函数声明

    void setImageDevices(const QString& imagePath, const QList<QJsonObject>& devices);
    QList<QJsonObject> getCurrentImageDevices();
    qint64 getCurrentImageTimestamp();
    QMap<QString, QStringList> getAddressFuncData();
    MainWindow(QWidget *parent = nullptr);
    MyRequestHandler* requestHandler;
    QByteArray getLatestSerialData() const { return m_latestSerialData; }
    QMap<QString, QByteArray> getAllData() const { return alldata; }
    QMap<QString, QList<QString>> getAddressFuncData() const { return addressFuncData; }
    QMap<QString, QList<QString>> getSerialData() const {
            QMutexLocker locker(&dataMutex);
            return addressFuncData;
        }
        QJsonObject getImageSize() const ;
    QString getCurrentImagePath() const {
        if (m_currentImageIndex == -1 || m_currentImageIndex >= m_imageDeviceList.size()) {
            return "";
        }
        return m_imageDeviceList[m_currentImageIndex].imagePath;
    }

    QString getCurrentImageTheme() const {
        if (m_currentImageIndex == -1 || m_currentImageIndex >= m_imageDeviceList.size()) {
            return "静电管理在线监控系统 ESD-1000.V1.0"; // 默认主题
        }
        const QString& theme = m_imageDeviceList[m_currentImageIndex].theme;
        return theme.isEmpty() ? "静电管理在线监控系统 ESD-1000.V1.0" : theme;
    }

    // 获取当前图片的所有设备数据（类型、ID、相对坐标）- 已修复未声明问题
    QList<QJsonObject> getCurrentDevicesData() const {
        QList<QJsonObject> devicesData;
        QSet<QString> uniqueIds; // 关键：声明去重集合（必须加这行）

        if (m_currentImageIndex == -1 || m_currentImageIndex >= m_imageDeviceList.size()) {
            return devicesData;
        }

        const ImageDeviceData& currentData = m_imageDeviceList[m_currentImageIndex];
        QPixmap pixmap(currentData.imagePath);
        QSize imageSize = pixmap.size();
        if (imageSize.width() == 0 || imageSize.height() == 0) {
            return devicesData;
        }

        for (DeviceWidget* dev : currentData.devices) {
            if (!dev) continue;

            QString devId = dev->getId();
            if (uniqueIds.contains(devId)) continue; // 现在能找到 uniqueIds 了
            uniqueIds.insert(devId); // 现在能找到 uniqueIds 了

            QPoint absolutePos = dev->getPos();
            if (absolutePos.x() < 0 || absolutePos.x() > imageSize.width()
                || absolutePos.y() < 0 || absolutePos.y() > imageSize.height()) {
                continue;
            }

            float relativeX = (float)absolutePos.x() / imageSize.width() * 100;
            float relativeY = (float)absolutePos.y() / imageSize.height() * 100;
            if (relativeX < 0 || relativeX > 100 || relativeY < 0 || relativeY > 100) {
                continue;
            }

            QJsonObject devObj;
            devObj["type"] = dev->getType();
            devObj["id"] = devId;
            devObj["x"] = QString::number(relativeX, 'f', 1);
            devObj["y"] = QString::number(relativeY, 'f', 1);
            devicesData.append(devObj);
        }
        return devicesData;
    }
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    QSerialPort *serialBtn;
    QMutex serialBtnMutex;
    bool m_isBtnSerialConnected = false;
    QSerialPort *serial;
    QTimer *overtime;
    QTimer* sendTimer;
    QTimer *cleanLogTimer;
    SerialWorker* m_worker1;
    SerialWorker* m_worker2;
    bool m_worker2Waiting = false;  // 线程2是否等待线程1完成一轮轮询
    QTimer* m_imageCycleTimer = nullptr;
    QMutex m_newarrangeMutex;        // 对应 processModbusData 中使用的锁
        QMutex m_configuredIdsMutex;     // 对应 processModbusData 中使用的锁
        QMutex m_addressFuncDataMutex;   // 对应 parsingdata 中使用的锁
        int pollCount = 0; // 轮询计数，初始为0
    int m_currentImageIndex=-1;
    bool m_isAllConnected = false;
    bool backupDevices(); // 备份设备点（核心）
    bool restoreDevices(); // 恢复设备点（核心）
    QMap<QString, QList<QJsonObject>> m_imageDevicesMap;
    QString m_currentImageAbsPath;
    QMap<QString, QStringList> m_serialParsedData;
    QMutex m_dataMutex;
    bool isPollingActive;
    bool isSingleTest;
    QMutex queueMutex;
    QMutex logMutex;
    QString yellowTxtPath;
    QString redTxtPath;
    int maxResendCount;
    QByteArray lastSentData;
    QSet<QString> configuredIds;
    QString currentExpectedAddrFunc;
    struct ColorPoints {
        QVector<QPointF> yellow;
        QVector<QPointF> blue;
    };
    QSet<QString> processedIds;
    int sendnum;
    mutable QMutex dataMutex;
    QByteArray senddata;
    QQueue<QByteArray> sendQueue;
    QByteArray recvBuffer;
    QByteArray m_latestSerialData;
    QMap<QString, QByteArray> alldata;
    QString m_currentSendAddress;
    QMap<QString, QList<QString>> addressFuncData;
    QVector<QStringList> newarrange;
    DeviceWidget* m_selectedDevice = nullptr;
    QList<ImageDeviceData> m_imageDeviceList;
    QMap<QString, int> m_devIdCounters;
    QMap<QString, QSet<int>> m_deletedDeviceIds; // 新增：按类型存储已删除的设备ID
    configmanager *m_configManager;
    QString m_btn6SendKey;

    // 轮询统计数据
    int m_wTotalCount;
    int m_wQualifiedCount;
    int m_tTotalCount;
    int m_tQualifiedCount;
    int m_eTotalCount;
    int m_eQualifiedCount;
    QList<double> m_temperatureValues;
    QList<double> m_humidityValues;
    QList<double> m_cleanlinessValues;
    int m_finishedWorkersCount;
    bool m_singleLinkPolling = false;
    QVector<QStringList> m_fullPollConfig;

    // 存储每个线程的合格率数据
    struct WorkerRateData {
        double wRate;
        double tRate;
        double eRate;
        double avgTemp;
        double avgHumidity;
        double avgCleanliness;
    };
    QMap<QString, WorkerRateData> m_workerRateDataMap;

    // 托盘图标相关成员变量
    QSystemTrayIcon *m_trayIcon;
    QMenu *m_trayMenu;
    QAction *m_showAction;
    QAction *m_hideAction;
    QAction *m_exitAction;

    // 开机自启动相关
    bool m_autoStart = false;

    // 图表相关成员变量
    QChartView* m_wristbandChartView;
    QChartView* m_deviceChartView;
    QChartView* m_matChartView;
    QChart* m_wristbandChart;
    QChart* m_deviceChart;
    QChart* m_matChart;
    QLineSeries* m_wristbandSeries;
    QLineSeries* m_deviceSeries;
    QLineSeries* m_matSeries;
    QDateTimeAxis* m_wristbandAxisX;
    QDateTimeAxis* m_deviceAxisX;
    QDateTimeAxis* m_matAxisX;
    QValueAxis* m_wristbandAxisY;
    QValueAxis* m_deviceAxisY;
    QValueAxis* m_matAxisY;

    QByteArray btnRecvBuffer;       // 专用串口接收缓冲区（和原有 recvBuffer 隔离）
    QMutex btnRecvBufferMutex;      // 专用缓冲区锁
    QString btnCurrentExpectedAddrFunc; // 专用串口预期响应（和原有 currentExpectedAddrFunc 隔离）
    QString btn5DeviceType; // pushButton_5的设备类型
    int btn5DeviceAddr; // pushButton_5的设备地址
    int btn5DeviceReg; // pushButton_5的寄存器地址
    ToolCommandContext m_toolContext = ToolCommandContext::None;
    SerialWorker* m_activeToolWorker = nullptr;
    QPushButton* m_btnDevModifyAddr = nullptr;
    QPushButton* m_btnDevModifyChannel = nullptr;
    QStackedWidget* m_stackDevModify = nullptr;
    QLineEdit* m_leDisplayAddress = nullptr;
    QLineEdit* m_leInputAddress = nullptr;
    QPushButton* m_btnAddrQuery = nullptr;
    QPushButton* m_btnAddrApply = nullptr;
    QTextBrowser* m_tbAddrLog = nullptr;
    void parsePushButton5Response(const QString& type, int addr, int reg = -1); // 解析pushButton_5响应
    void updateCycleButtonText();
    void addImageToHistory(const QString& imagePath); // 添加图片到历史列表
    void switchToImage(int index); // 切换到指定图片
    void setImageTheme(const QString& imagePath, const QString& theme); // 设置图片主题
    int findImageIndex(const QString& imagePath); // 查找图片在列表中的索引
    void clearCurrentDevices(); // 隐藏所有图片的设备点
    void applyGlobalUiStyle();
    void applyConnectionSettingsStyle();
    void refreshSerialPortList();
    void setupSharedSerialUi();
    void setupDeviceModifyUi();
    void updateDeviceModifyModeButtons();
    void applyDeviceModifyPageStyle();
    SerialWorker* getToolSerialWorker() const;
    QByteArray buildModbusHexFrame(const QString& hexPayload) const;
    bool sendToolFrame(const QString& hexPayload, const QString& expectedFunc, ToolCommandContext context);
    void updateMapPageToolbarsPosition(); // 将地图页工具栏定位到地图下方
    bool m_separateEnvEsd = true;

    QVector<QStringList> getPollConfigRows();
    QSet<QString> collectPlacedDeviceIds() const;
    QList<QPoint> collectCurrentMapDevicePositions() const;
    QPoint computeDevicePlacement(const QList<QPoint>& occupied, int slotIndex) const;
    DeviceWidget* createMapDevice(const QString& type, const QString& devId, const QPoint& pos);
    QVector<PollDeviceRef> getUnplacedPollDevices(const QString& typeFilter = QString());

    // 托盘图标相关函数
    void initTrayIcon();
    void setAutoStart(bool autoStart);
    bool getAutoStart();

    // 重写关闭事件，实现最小化到托盘
    void closeEvent(QCloseEvent *event) override;

    // 图表相关函数声明
    void initCharts(); // 初始化图表
    void updateWristbandChart(const QList<QPair<QDateTime, double>>& data); // 更新腕带电阻图表
    void updateDeviceChart(const QList<QPair<QDateTime, double>>& data); // 更新设备电阻图表
    void updateMatChart(const QList<QPair<QDateTime, double>>& data); // 更新台垫电阻图表
    void loadChartDataFromDatabase(); // 从数据库加载图表数据
    void loadMockData(); // 加载虚拟数据作为备用方案

    // 新增：配置相关函数
    void restoreFromConfig(); // 从配置恢复图片和设备
    void saveToConfig();      // 保存当前配置到文件
    void saveSerialConfig();  // 保存串口配置
    void loadSerialConfig();  // 加载串口配置
    void autoConnect();       // 自动连接串口并启动轮询

    // 原有函数声明不变...
    uint16_t calcrc(const QByteArray &data) const;
    void recv();
    void sendmess();
    void changemenu();
    void sendNextData();
    void onTimeout();
    void processModbusData(const QVector<QStringList>& tableData);
    void processSingleModbusData(const QString& address, const QString& funcCode, const QString& data,bool isOptional = false);
    void processCompleteModbusFrame(const QByteArray& frame);
    void parsingdata(const QByteArray& frame);
    void timerTriggerSend();
    void detectAndSaveColors(const QString& imagePath);
    void resetUnprocessedTo3(const QString& txtPath, const QSet<QString>& processedIds);
    void resetUnprocessedTo2(const QString& txtPath, const QSet<QString>& processedIds);
    void updateSingleIdInTxt(const QString& txtPath, const QString& targetId, const QString& newNum);
    void updateYellowTxt(const QString& funcCode, const QString& hexAddr, const QList<QString>& dataBits);
    void resetUnprocessedByConfig(const QString& txtPath, const QSet<QString>& processedIds);
    void writeCrashLog(const QString& logContent);
    void cleanLogFile();

    void onPushButtonDeleteClicked();
    void onBtnAddDeviceClicked();
    void onBtnAutoGenerateClicked();
    void onDeviceDoubleClicked(DeviceWidget* device); // 新增：双击设备处理函数
    void onImageListCurrentIndexChanged(int index); // 新增：图片列表切换槽函数
    void onBtnDeleteClicked();
    void updateIonizerStatus(const QString& deviceId, bool isOnline); // 新增：更新离子风机状态
    void loadPollConfig(); // 新增：程序启动时自动加载轮询配置
    void syncPollConfigToWorkers(const QVector<QStringList>& tableData);
    int activePollWorkerCount() const { return m_singleLinkPolling ? 1 : 2; }
    // 新增：pushButton_5发送Modbus（自定义地址/寄存器/功能码）
    void onPushButton5Clicked();
    void onPushButton6Clicked();

    // 计算并插入合格率数据
    void calculateAndInsertQualifiedRate();
    void onPollingCycleFinished();
    void resetPollingStats();
    void calculateAndInsertCombinedQualifiedRate();
private slots:
    void openBtnSerial();
    void recvBtnData();
    void openuart();
    void onBtnSelectImageClicked();
    void onBtnApplySizeClicked();
    void on_btnDelete_2_clicked();
    void onPushButton3Clicked();
    void onSingleTestClicked();
    void switchToNextImage();
    void onPushButton4Clicked();
    void onBtnApplyThemeClicked();
    void onChkSeparateEnvEsdChanged(int state);
    void onBtnSaveDevicePositionsClicked();
    void onConnectionTypeChanged(int index);
    void onSerialModeChanged(int index);
    void updateSerialPortVisibility();
    void onRefreshSerialPortsClicked();
    void onToolCommandCompleted(const QByteArray& frame, const QString& expectedFuncCode);
    void onToolCommandFailed(const QString& reason);
    void onDevModifyAddrQuery();
    void onDevModifyAddrApply();

    // 托盘图标相关槽函数
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void onShowActionTriggered();
    void onHideActionTriggered();
    void onExitActionTriggered();
    // 背景图切换时间相关槽函数
    void onBtnApplyBgSwitchTimeClicked();
};

#endif // MAINWINDOW_H
