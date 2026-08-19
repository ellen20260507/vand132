#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "esdeditpanel.h"
#include "pollconfig.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QSettings>
#include <QCoreApplication>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpSocket>
#include <QProcess>
#include <QSqlDatabase>
#include <QDir>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QPushButton>
#include <QAbstractItemView>
#include <QListWidget>
#include <QSpinBox>
#include <QDateTime>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , serial(new QSerialPort(this))
    , overtime(new QTimer(this))
    , sendTimer(new QTimer(this))
    , cleanLogTimer(new QTimer(this))
    , requestHandler(new MyRequestHandler(this))
    , m_configManager(new configmanager(this)) // 初始化配置管理器
    , m_imageCycleTimer(nullptr)
    , serialBtn(new QSerialPort(this))
    , btn5DeviceAddr(0) // 初始化pushButton_5设备地址
    , btn5DeviceReg(0) // 初始化pushButton_5寄存器地址
{
// if (checkTrialExpired()) {
//            QMessageBox::critical(this, "请联系作者", "请联系作者后解锁。");
//            #include <stdlib.h>
//            _exit(0);
//            return;
//        }
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("静电管理在线监控系统 ESD-1000.V1.0"));
    setupSharedSerialUi();
    setupDeviceModifyUi();
    applyGlobalUiStyle();
    ui->leImageWidth->setText("1280");   // 宽度输入框：leImageWidth
    ui->leImageHeight->setText("720");  // 高度输入框：leImageHeight
    isPollingActive = false;
    isSingleTest = false;
    maxResendCount = 3;
    sendnum = 0;

    // 初始化设备ID计数器
    m_devIdCounters["W"] = 1;
    m_devIdCounters["T"] = 1;
    m_devIdCounters["E"] = 1;
    m_devIdCounters["C"] = 1; // 新增C类型计数器

    // 原有定时器初始化（仅保留日志清理定时器，串口相关定时器移到线程内）
    overtime->setInterval(1000);
    sendTimer->setInterval(500);
    cleanLogTimer->setInterval(300000); // 5分钟清理一次日志
    cleanLogTimer->start();

    ui->pushButton_3->setText("启用轮询");

  // ===================== 新增：双线程初始化 =====================
  // 1. 创建两个线程实例（线程名称用于日志区分）
  m_worker1 = new SerialWorker("线程一",TaskType::C_TYPE, this);  // 线程一：处理尘埃粒子计数器（功能码1030）
    m_worker2 = new SerialWorker("线程二",TaskType::WTE_TYPE, this);  // 线程二：处理WTEI（非1030功能码）
    
    // 初始化轮询统计数据
    m_wTotalCount = 0;
    m_wQualifiedCount = 0;
    m_tTotalCount = 0;
    m_tQualifiedCount = 0;
    m_eTotalCount = 0;
    m_eQualifiedCount = 0;
    m_finishedWorkersCount = 0;

    // 2. 绑定线程信号到UI（区分线程标识，复用textBrowser显示）
    auto bindWorkerSignals = [this](SerialWorker* worker) {
        // 线程日志显示（标注线程名称）
        connect(worker, &SerialWorker::logGenerated, this, [=](const QString& workerName, const QString& log){
            ui->textBrowser->append(QString("[%1] %2").arg(workerName).arg(log));
            ui->textBrowser->moveCursor(QTextCursor::End);
            writeCrashLog(QString("[%1] %2").arg(workerName).arg(log)); // 写入日志文件
        });
        // 线程接收数据显示（标注线程名称）
        connect(worker, &SerialWorker::dataReceived, this, [=](const QString& workerName, const QString& data){
            ui->textBrowser->append(QString("[%1] %2").arg(workerName).arg(data));
            ui->textBrowser->moveCursor(QTextCursor::End);
        });
        // 串口打开失败提示
        connect(worker, &SerialWorker::serialOpenFailed, this, [=](const QString& workerName, const QString& reason){
            QMessageBox::critical(this, "串口打开失败", QString("%1：%2").arg(workerName).arg(reason));
            writeCrashLog(QString("[%1] 串口打开失败：%2").arg(workerName).arg(reason));
        });
        // 连接轮询完成信号
        connect(worker, &SerialWorker::pollingCycleFinished, this, &MainWindow::onPollingCycleFinished);
    };
    // 绑定两个线程的信号
    bindWorkerSignals(m_worker1);
    bindWorkerSignals(m_worker2);
    connect(m_worker1, &SerialWorker::toolCommandCompleted, this, &MainWindow::onToolCommandCompleted);
    connect(m_worker2, &SerialWorker::toolCommandCompleted, this, &MainWindow::onToolCommandCompleted);
    connect(m_worker1, &SerialWorker::toolCommandFailed, this, &MainWindow::onToolCommandFailed);
    connect(m_worker2, &SerialWorker::toolCommandFailed, this, &MainWindow::onToolCommandFailed);

    // 线程一（WTE类型）数据接收
    connect(m_worker1, &SerialWorker::parsedDataReady, this, [this](const QMap<QString, QStringList>& data) {
        QMutexLocker locker(&m_dataMutex); // 加锁保证线程安全
        for (auto it = data.begin(); it != data.end(); ++it) {
            m_serialParsedData[it.key()] = it.value(); // 逐个覆盖相同键的旧值
        }
    });

    // 线程二（C类型）数据接收 - 新增这行代码，确保尘埃粒子数据能被处理
    connect(m_worker2, &SerialWorker::parsedDataReady, this, [this](const QMap<QString, QStringList>& data) {
        QMutexLocker locker(&m_dataMutex); // 加锁保证线程安全
        for (auto it = data.begin(); it != data.end(); ++it) {
            m_serialParsedData[it.key()] = it.value(); // 逐个覆盖相同键的旧值
        }
    });

    // 连接线程的合格率数据信号
    connect(m_worker1, &SerialWorker::qualifiedRateDataReady, this, [this](const QDateTime& time, double wRate, double tRate, double eRate, double avgTemp, double avgHumidity, double avgCleanliness) {
        WorkerRateData data;
        data.wRate = wRate;
        data.tRate = tRate;
        data.eRate = eRate;
        data.avgTemp = avgTemp;
        data.avgHumidity = avgHumidity;
        data.avgCleanliness = avgCleanliness;
        m_workerRateDataMap["worker1"] = data;
    });

    connect(m_worker2, &SerialWorker::qualifiedRateDataReady, this, [this](const QDateTime& time, double wRate, double tRate, double eRate, double avgTemp, double avgHumidity, double avgCleanliness) {
        WorkerRateData data;
        data.wRate = wRate;
        data.tRate = tRate;
        data.eRate = eRate;
        data.avgTemp = avgTemp;
        data.avgHumidity = avgHumidity;
        data.avgCleanliness = avgCleanliness;
        m_workerRateDataMap["worker2"] = data;
    });

    DBManager* dbManager = DBManager::instance();
    auto connectWorkerDb = [dbManager](SerialWorker* worker) {
        if (!worker || !dbManager) {
            return;
        }
        connect(worker, &SerialWorker::wteDataReady, dbManager, &DBManager::handleWteData, Qt::QueuedConnection);
        connect(worker, &SerialWorker::dustDataReady, dbManager, &DBManager::handleDustData, Qt::QueuedConnection);
        connect(worker, &SerialWorker::channelReadingReady, dbManager, &DBManager::handleChannelReading, Qt::QueuedConnection);
    };
    connectWorkerDb(m_worker1);
    connectWorkerDb(m_worker2);

    // 连接DBManager的logGenerated信号到textBrowser
    connect(DBManager::instance(), &DBManager::logGenerated, this, [=](const QString& workerName, const QString& log){
        ui->textBrowser->append(QString("[%1] %2").arg(workerName).arg(log));
        ui->textBrowser->moveCursor(QTextCursor::End);
        writeCrashLog(QString("[%1] %2").arg(workerName).arg(log)); // 写入日志文件
    });

    // 连接查询10分钟平均值按钮的点击事件
    connect(ui->pushButton_query_10min_avg, &QPushButton::clicked, this, [=]() {
        // 计算10分钟前的时间
        QDateTime tenMinutesAgo = QDateTime::currentDateTime().addSecs(-10 * 60);
        // 调用DBManager的函数查询最近10分钟的平均值
        DBManager::instance()->getAverageDataFromTimeRange(tenMinutesAgo, 10);
    });

    // 初始化数据库连接并输出状态
    ui->textBrowser->append("[数据库] 开始初始化数据库连接...");

    // 输出程序运行目录
    QString appDir = QCoreApplication::applicationDirPath();
    ui->textBrowser->append("[数据库] 程序运行目录: " + appDir);

    // ===== 检查必要的环境文件 =====
    ui->textBrowser->append("[数据库] 检查必要的环境文件:");

    // 1. 检查 libmysql.dll 及其 OpenSSL 依赖（MySQL 8 客户端必需）
    QString libmysqlPath = appDir + "/libmysql.dll";
    QFileInfo libmysqlFile(libmysqlPath);
    if (libmysqlFile.exists()) {
        ui->textBrowser->append("[数据库]   ✓ libmysql.dll 存在");
    } else {
        ui->textBrowser->append("[数据库]   ✗ libmysql.dll 缺失！");
    }

    QString libsslPath = appDir + "/libssl-3-x64.dll";
    QString libcryptoPath = appDir + "/libcrypto-3-x64.dll";
    QFileInfo libsslFile(libsslPath);
    QFileInfo libcryptoFile(libcryptoPath);
    if (libsslFile.exists()) {
        ui->textBrowser->append("[数据库]   ✓ libssl-3-x64.dll 存在");
    } else {
        ui->textBrowser->append("[数据库]   ✗ libssl-3-x64.dll 缺失！（libmysql 依赖，常见导致 Driver not loaded）");
    }
    if (libcryptoFile.exists()) {
        ui->textBrowser->append("[数据库]   ✓ libcrypto-3-x64.dll 存在");
    } else {
        ui->textBrowser->append("[数据库]   ✗ libcrypto-3-x64.dll 缺失！（libmysql 依赖，常见导致 Driver not loaded）");
    }

    // 2. 检查 Qt5Sql.dll
    QString qt5SqlPath = appDir + "/Qt5Sql.dll";
    QFileInfo qt5SqlFile(qt5SqlPath);
    if (qt5SqlFile.exists()) {
        ui->textBrowser->append("[数据库]   ✓ Qt5Sql.dll 存在");
    } else {
        ui->textBrowser->append("[数据库]   ✗ Qt5Sql.dll 缺失！");
    }

    // 3. 检查 MySQL 驱动插件
    QString mysqlDriverPath = appDir + "/plugins/sqldrivers/qsqlmysql.dll";
    QFileInfo mysqlDriverFile(mysqlDriverPath);
    if (mysqlDriverFile.exists()) {
        ui->textBrowser->append("[数据库]   ✓ plugins/sqldrivers/qsqlmysql.dll 存在");
    } else {
        ui->textBrowser->append("[数据库]   ✗ plugins/sqldrivers/qsqlmysql.dll 缺失！");
    }

    // 4. 检查配置文件
    QString configPath = appDir + "/mysql_config.ini";
    ui->textBrowser->append("[数据库]   配置文件路径: " + configPath);
    QFileInfo configFile(configPath);
    if (configFile.exists()) {
        ui->textBrowser->append("[数据库]   ✓ mysql_config.ini 存在");
    } else {
        ui->textBrowser->append("[数据库]   ✗ mysql_config.ini 缺失！");
    }

    // 检查是否有缺失的文件
    bool allFilesPresent = libmysqlFile.exists() && libsslFile.exists() && libcryptoFile.exists()
            && qt5SqlFile.exists() && mysqlDriverFile.exists() && configFile.exists();
    if (allFilesPresent) {
        ui->textBrowser->append("[数据库] ✓ 所有必要文件检查通过");
    } else {
        ui->textBrowser->append("[数据库] ✗ 存在缺失的必要文件！");
    }

    // ===== 手动设置 Qt 插件路径 =====
    ui->textBrowser->append("[数据库] 设置 Qt 插件路径:");
    QString pluginsPath = appDir + "/plugins";
    QCoreApplication::addLibraryPath(pluginsPath);
    ui->textBrowser->append("[数据库]   添加插件路径: " + pluginsPath);
    
    // 检查 plugins/sqldrivers 目录是否存在
    QString sqlDriversPath = pluginsPath + "/sqldrivers";
    QDir sqlDriversDir(sqlDriversPath);
    if (sqlDriversDir.exists()) {
        ui->textBrowser->append("[数据库]   ✓ sqldrivers 目录存在");
    } else {
        ui->textBrowser->append("[数据库]   ✗ sqldrivers 目录不存在！");
    }

    // ===== 检查 Qt 插件路径 =====
    ui->textBrowser->append("[数据库] 当前 Qt 插件路径列表:");
    QStringList pluginPaths = QCoreApplication::libraryPaths();
    for (const QString& path : pluginPaths) {
        ui->textBrowser->append("[数据库]   " + path);
    }

    // ===== 检查可用的 SQL 驱动 =====
    ui->textBrowser->append("[数据库] 检查可用的 SQL 驱动:");
    QStringList drivers = QSqlDatabase::drivers();
    if (drivers.isEmpty()) {
        ui->textBrowser->append("[数据库]   ✗ 没有可用的 SQL 驱动！");
    } else {
        for (const QString& driver : drivers) {
            ui->textBrowser->append("[数据库]   ✓ 可用驱动: " + driver);
        }
    }
    
    if (!drivers.contains("QMYSQL")) {
        ui->textBrowser->append("[数据库]   ✗ QMYSQL 驱动不可用！");
        ui->textBrowser->append("[数据库]     - 可能原因: qsqlmysql.dll 与 Qt 版本不匹配");
        ui->textBrowser->append("[数据库]     - 可能原因: libmysql.dll 缺少 OpenSSL 依赖（libssl-3-x64.dll / libcrypto-3-x64.dll）");
        ui->textBrowser->append("[数据库]     - 解决方法: 从 MySQL 安装目录 bin 复制 libssl-3-x64.dll、libcrypto-3-x64.dll 到 exe 同级");
        ui->textBrowser->append("[数据库]     - 解决方法: 确保 qsqlmysql.dll 在 plugins/sqldrivers/ 目录");
    }

    // 从配置文件读取数据库连接参数
    QSettings dbSettings(configPath, QSettings::IniFormat);
    QString dbHost = dbSettings.value("MySQL/host", "127.0.0.1").toString();
    int dbPort = dbSettings.value("MySQL/port", 3306).toInt();
    QString dbUser = dbSettings.value("MySQL/username", "root").toString();
    QString dbPassword = dbSettings.value("MySQL/password", "").toString();
    QString dbName = dbSettings.value("MySQL/database", "sensor_db").toString();

    // 输出读取到的配置参数
    ui->textBrowser->append("[数据库] 读取到的连接参数:");
    ui->textBrowser->append("[数据库]   host: " + dbHost);
    ui->textBrowser->append("[数据库]   port: " + QString::number(dbPort));
    ui->textBrowser->append("[数据库]   user: " + dbUser);
    QString passwordDisplay = dbPassword.isEmpty() ? QString("(空)") : QString("******");
    ui->textBrowser->append(QString("[数据库]   password: ") + passwordDisplay);
    ui->textBrowser->append("[数据库]   database: " + dbName);

    // ===== 测试网络连接 =====
    ui->textBrowser->append("[数据库] 测试网络连接:");
    
    // 测试端口是否可访问
    QHostAddress hostAddr(dbHost);
    QTcpSocket socket;
    socket.connectToHost(hostAddr, dbPort);
    
    if (socket.waitForConnected(3000)) {
        ui->textBrowser->append("[数据库]   ✓ 端口 " + QString::number(dbPort) + " 可访问");
        socket.disconnectFromHost();
    } else {
        ui->textBrowser->append("[数据库]   ✗ 端口 " + QString::number(dbPort) + " 不可访问！");
        ui->textBrowser->append("[数据库]     - 可能原因: MySQL服务未启动 / 端口配置错误 / 防火墙阻止");
    }
    
    // ===== 检查MySQL服务状态（Windows）=====
    ui->textBrowser->append("[数据库] 检查MySQL服务状态:");
    QProcess process;
    process.start("sc", QStringList() << "query" << "MySQL80");
    process.waitForFinished(2000);
    
    QString output = process.readAllStandardOutput();
    QString error = process.readAllStandardError();
    
    if (output.contains("RUNNING")) {
        ui->textBrowser->append("[数据库]   ✓ MySQL服务正在运行");
    } else if (output.contains("STOPPED")) {
        ui->textBrowser->append("[数据库]   ✗ MySQL服务已停止！");
        ui->textBrowser->append("[数据库]     - 请运行: net start MySQL80");
    } else if (output.contains("FAILED")) {
        ui->textBrowser->append("[数据库]   ✗ MySQL服务查询失败: " + error);
    } else {
        ui->textBrowser->append("[数据库]   ? 无法确定MySQL服务状态");
    }
    
    // ===== 初始化DBManager的数据库连接 =====
    ui->textBrowser->append("[数据库] 正在尝试连接数据库...");
    bool dbManagerInit = DBManager::instance()->initDB(dbHost, dbPort, dbUser, dbPassword, dbName);
    if (dbManagerInit) {
        ui->textBrowser->append("[数据库] ✓ DBManager连接初始化成功！");
    } else {
        ui->textBrowser->append("[数据库] ✗ DBManager连接初始化失败！");
        ui->textBrowser->append("[数据库] 可能的原因:");
        ui->textBrowser->append("[数据库]   1. MySQL服务未启动");
        ui->textBrowser->append("[数据库]   2. 端口配置不正确（当前: " + QString::number(dbPort) + "）");
        ui->textBrowser->append("[数据库]   3. 用户凭证错误");
        ui->textBrowser->append("[数据库]   4. 目标数据库不存在");
        ui->textBrowser->append("[数据库]   5. libmysql.dll 缺少 OpenSSL 依赖（需 libssl-3-x64.dll / libcrypto-3-x64.dll 与 exe 同级）");
    }

    if (dbManagerInit) {
        ui->textBrowser->append("[数据库] DBManager数据库连接初始化成功！");
    } else {
        ui->textBrowser->append("[数据库] DBManager数据库连接初始化失败！");
    }

        // 离子风机状态变化处理
        connect(m_worker1, &SerialWorker::ionizerStatusChanged, this, [this](const QString& deviceId, bool isOnline) {
            updateIonizerStatus(deviceId, isOnline);
        });
    // ===================== 原有信号连接（仅保留非串口相关，修改串口相关连接） =====================
    connect(ui->pushButton_2, &QPushButton::clicked, this, &MainWindow::onSingleTestClicked);
    connect(ui->pushButton_delete, &QPushButton::clicked, this, &MainWindow::onPushButtonDeleteClicked);
    // 注释原有串口接收连接（移到线程内）
    // connect(serial, &QSerialPort::readyRead, this, &MainWindow::recv);
    // 注释原有定时器连接（移到线程内）
    // connect(sendTimer, &QTimer::timeout, this, &MainWindow::tSend);
    // connect(overtime, &QTimer::timeout, this, &MainWindow::onTimeout);
    connect(cleanLogTimer, &QTimer::timeout, this, &MainWindow::cleanLogFile);
    // 连接串口控制按钮（改为双线程控制）
    connect(ui->pushButton, &QPushButton::clicked, this, &MainWindow::openuart);
    // 连接轮询配置按钮（改为双线程同步配置）
    connect(ui->pushButton_lunxun, &QPushButton::clicked, this, &MainWindow::changemenu);
    // 连接方式下拉条信号
    connect(ui->comboBox_connection_type, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &MainWindow::onConnectionTypeChanged);
    connect(ui->comboBox_serial_mode, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &MainWindow::onSerialModeChanged);
    connect(ui->pushButton_refreshSerial, &QPushButton::clicked, this, &MainWindow::onRefreshSerialPortsClicked);
    refreshSerialPortList();
    // 初始化连接方式相关控件的显示状态
    onConnectionTypeChanged(0);
    onSerialModeChanged(0);
    // 菜单信号连接（保留不变）
    connect(ui->action1, &QAction::triggered, this, &MainWindow::changemenu);
    connect(ui->action2, &QAction::triggered, this, &MainWindow::changemenu);
    connect(ui->actionmap, &QAction::triggered, this, &MainWindow::changemenu);
    connect(ui->action96, &QAction::triggered, this, &MainWindow::changemenu);
    m_prevStackedPageIndex = ui->stackedWidget->currentIndex();
    connect(ui->stackedWidget, &QStackedWidget::currentChanged,
            this, &MainWindow::onStackedWidgetCurrentChanged);
    connect(ui->pushButton_3, &QPushButton::clicked, this, &MainWindow::onPushButton3Clicked);
    // UI控件连接（保留不变）
    connect(ui->btnSelectImage, &QPushButton::clicked, this, &MainWindow::onBtnSelectImageClicked);
    connect(ui->btnAddDevice, &QPushButton::clicked, this, &MainWindow::onBtnAddDeviceClicked);
    connect(ui->btnAutoGenerate, &QPushButton::clicked, this, &MainWindow::onBtnAutoGenerateClicked);
    connect(ui->btnApplySize, &QPushButton::clicked, this, &MainWindow::onBtnApplySizeClicked);
    connect(ui->btnApplyTheme, &QPushButton::clicked, this, &MainWindow::onBtnApplyThemeClicked);
    connect(ui->chkSeparateEnvEsd, &QCheckBox::stateChanged, this, &MainWindow::onChkSeparateEnvEsdChanged);
    connect(ui->btnSaveDevicePositions, &QPushButton::clicked, this, &MainWindow::onBtnSaveDevicePositionsClicked);
    connect(ui->pushButton_4, &QPushButton::clicked, this, &MainWindow::onPushButton4Clicked);
    // 地图页工具栏增加「显示屏设置」
    if (ui->horizontalLayout_131) {
        QPushButton *btnDisplayScreens = new QPushButton(QStringLiteral("显示屏设置"), this);
        btnDisplayScreens->setObjectName(QStringLiteral("btnDisplayScreens"));
        ui->horizontalLayout_131->addWidget(btnDisplayScreens);
        connect(btnDisplayScreens, &QPushButton::clicked, this, &MainWindow::onBtnDisplayScreensClicked);
    }
    // 新增：连接pushButton_5点击信号
    connect(ui->pushButton_5, &QPushButton::clicked, this, &MainWindow::onPushButton5Clicked);
    // 连接pushButton_6点击信号
    connect(ui->pushButton_6, &QPushButton::clicked, this, &MainWindow::onPushButton6Clicked);
    connect(serialBtn, &QSerialPort::readyRead, this, &MainWindow::recvBtnData);
    // 连接背景图切换时间应用按钮
    connect(ui->btnApplyBgSwitchTime, &QPushButton::clicked, this, &MainWindow::onBtnApplyBgSwitchTimeClicked);
    // 初始化临时变量
    m_btn6SendKey.clear();

    if (ui->cmbImageList) {
        connect(ui->cmbImageList, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::onImageListCurrentIndexChanged);
    }
    connect(ui->btnDelete, &QPushButton::clicked, this, &MainWindow::onBtnDeleteClicked);
    m_imageCycleTimer = new QTimer(this);
       m_imageCycleTimer->setInterval(10000); // 10秒切换（固定，不搞复杂逻辑）

       // 2. 绑定信号槽（用 Qt5 兼容语法，避免 lambda 启动时崩溃）
       connect(m_imageCycleTimer, SIGNAL(timeout()), this, SLOT(switchToNextImage()));
    // 程序启动时从配置恢复图片和设备
    restoreFromConfig(); // 恢复图片和设备
    updateMapPageToolbarsPosition();
    // 延迟加载轮询配置，确保UI完全初始化
    QTimer::singleShot(100, this, [this]() {
        loadPollConfig(); // 自动加载轮询配置
    });
        ui->pushButton_3->setText("启用轮询");

        // 3. 有图片则启动定时器（无冗余日志）
        if (!m_imageDeviceList.isEmpty() && ui->labelImage) {
            m_imageCycleTimer->start();
        }
        updateCycleButtonText();
        QTimer *cleanCacheTimer = new QTimer(this);
            cleanCacheTimer->setInterval(1800000); // 1小时（可调整）
            connect(cleanCacheTimer, &QTimer::timeout, this, [this](){
                QMutexLocker locker(&m_dataMutex);
                m_serialParsedData.clear(); // 清空解析数据缓存
                QMutexLocker locker2(&m_addressFuncDataMutex);
                addressFuncData.clear(); // 清空设备数据缓存
                writeCrashLog("定时清理缓存：已清空解析数据和设备数据");
            });
            cleanCacheTimer->start();

    // 初始化托盘图标
    initTrayIcon();

    // 读取并设置开机自启动状态
    setAutoStart(getAutoStart());

    writeCrashLog("程序启动成功！");
}
MainWindow::~MainWindow()
{
    if (m_imageCycleTimer) {
            m_imageCycleTimer->stop();
            delete m_imageCycleTimer;
        }
    // 新增：关闭并释放两个线程
    if (m_worker1) {
        m_worker1->closeSerial();
        delete m_worker1;
    }
    if (m_worker2) {
        m_worker2->closeSerial();
        delete m_worker2;
    }

    // 原有析构逻辑（保留）；设备点位仅通过「保存点位」按钮写入文件，关闭时不自动保存
    sendTimer->stop();
    overtime->stop();
    cleanLogTimer->stop();
    serial->close();
    delete ui;
    delete m_configManager; // 释放配置管理器
    writeCrashLog("程序正常关闭！");
}
// mainwindow.cpp
QMap<QString, QStringList> MainWindow::getAddressFuncData()
{
    QMutexLocker locker(&m_dataMutex); // 加锁保证线程安全
    return m_serialParsedData;
}
void MainWindow::onPushButton3Clicked()
{
    // 2. 根据当前轮询状态切换功能
    if (!isPollingActive) {
        if (m_worker1) {
            m_worker1->startPolling();
            writeCrashLog("[轮询控制] 启动线程一轮询");
        }
        if (!m_singleLinkPolling && m_worker2) {
            m_worker2->startPolling();
            writeCrashLog("[轮询控制] 启动线程二轮询");
        }

        isPollingActive = true;
        ui->pushButton_3->setText("停止轮询");
        writeCrashLog(m_singleLinkPolling
                          ? "[轮询控制] 启动单链路轮询成功"
                          : "[轮询控制] 启动双线程轮询成功");
    } else {
        if (m_worker1) {
            m_worker1->stopPolling();
            writeCrashLog("[轮询控制] 停止线程一轮询");
        }
        if (m_worker2) {
            m_worker2->stopPolling();
            writeCrashLog("[轮询控制] 停止线程二轮询");
        }

        isPollingActive = false;
        ui->pushButton_3->setText("启用轮询");
        writeCrashLog("[轮询控制] 停止轮询成功");
    }
}
QJsonObject MainWindow::getImageSize() const
{
    QJsonObject sizeObj;

    // 直接读取你指定的控件值（leImageWidth / leImageHeight）
    int width = ui->leImageWidth->text().toInt();
    int height = ui->leImageHeight->text().toInt();

    // 过滤非法值（避免输入非数字或太小）
    sizeObj["w"] = (width >= 300 && width <= 2000) ? width : 800;
    sizeObj["h"] = (height >= 300 && height <= 2000) ? height : 600;

    return sizeObj;
}

void MainWindow::applyGlobalUiStyle()
{
    const int fontPx = 16;
    const int controlHeight = 44;
    const int labelMinWidth = 100;
    const int inputMinWidth = 300;
    const int formVSpacing = 22;
    const int formHSpacing = 18;
    const int formMargin = 24;
    const int formPanelWidth = 720;
    const int sideBtnWidth = 200;
    const int sideBtnX = 48 + formPanelWidth + 24;

    setStyleSheet(buildAdminPanelStyleSheet(fontPx, labelMinWidth, controlHeight));
    applyAdminPanelFont(ui->centralwidget, fontPx);

    const QList<QWidget*> formPages = {
        ui->page_2, ui->page_5, ui->page_6
    };
    for (QWidget* page : formPages) {
        if (!page) {
            continue;
        }
        applyAdminPanelControlSizes(page, inputMinWidth, controlHeight);
        applyAdminPanelLayoutSpacing(page, formVSpacing, formMargin);
        applyFormRowLayout(page, formHSpacing, 96);
    }

    if (ui->page_4) {
        applyMapPageCompactStyle(ui->page_4);
    }

    applyConnectionSettingsStyle();

    // 地图页工具栏（紧凑）
    ui->horizontalLayout_12->setSpacing(6);
    ui->horizontalLayout_131->setSpacing(8);
    ui->horizontalLayout_12->setContentsMargins(4, 0, 4, 0);
    ui->horizontalLayout_131->setContentsMargins(4, 0, 4, 0);
    ui->cmbImageList->setMinimumWidth(140);
    ui->cmbImageList->setMaximumWidth(200);

    // 地图尺寸页
    ui->btnApplySize->setMinimumSize(150, controlHeight);
    ui->btnAddDevice_2->setMinimumSize(150, controlHeight);
    ui->leImageWidth->setMinimumSize(160, controlHeight);
    ui->leImageHeight->setMinimumSize(160, controlHeight);
    ui->leImageWidth_2->setMinimumSize(160, controlHeight);
    ui->leImageHeight_2->setMinimumSize(160, controlHeight);
    ui->leImageWidth_3->setMinimumSize(160, controlHeight);

    updateMapPageToolbarsPosition();
    applyDeviceModifyPageStyle();
}

void MainWindow::applyConnectionSettingsStyle()
{
    const int fontPx = 16;
    const int controlHeight = 44;
    const int labelMinWidth = 150;
    const int inputMinWidth = 260;

    QFont pageFont(QStringLiteral("Microsoft YaHei UI"), fontPx);
    ui->page->setFont(pageFont);

    ui->page->setStyleSheet(QString(
        "QWidget#page { background-color: #f5f7fb; }"
        "QLabel { color: #1d2129; font-size: %1px; min-width: %2px; }"
        "QComboBox, QLineEdit {"
        "  background: #ffffff;"
        "  border: 1px solid #c9cdd4;"
        "  border-radius: 6px;"
        "  color: #1d2129;"
        "  font-size: %1px;"
        "  min-height: %3px;"
        "  padding: 4px 12px;"
        "}"
        "QComboBox:focus, QLineEdit:focus { border: 1px solid #165dff; }"
        "QComboBox::drop-down {"
        "  subcontrol-origin: padding;"
        "  subcontrol-position: center right;"
        "  width: 34px;"
        "  border-left: 1px solid #e5e6eb;"
        "}"
        "QComboBox QAbstractItemView {"
        "  font-size: %1px;"
        "  min-height: %3px;"
        "  selection-background-color: #e8f3ff;"
        "  selection-color: #1d2129;"
        "}"
        "QPushButton {"
        "  background-color: #165dff;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 6px;"
        "  font-size: %1px;"
        "  min-height: %3px;"
        "  min-width: 150px;"
        "  padding: 6px 18px;"
        "}"
        "QPushButton:hover { background-color: #4080ff; }"
        "QPushButton:pressed { background-color: #0e42d2; }"
        "QTextBrowser {"
        "  background: #ffffff;"
        "  border: 1px solid #e5e6eb;"
        "  border-radius: 8px;"
        "  font-size: 14px;"
        "  padding: 10px;"
        "}"
    ).arg(fontPx).arg(labelMinWidth).arg(controlHeight));

    ui->layoutWidget->setGeometry(40, 20, 560, 540);
    ui->layoutWidget_btnPanel->setGeometry(620, 30, 180, 300);

    ui->verticalLayout_2->setSpacing(14);
    ui->verticalLayout_2->setContentsMargins(16, 16, 16, 16);
    ui->verticalLayout_5->setSpacing(14);

    ui->label->setMinimumWidth(labelMinWidth);
    ui->label_17->setMinimumWidth(labelMinWidth);

    const QSize expandSize(16777215, 16777215);
    ui->lineEdit_2->setMaximumSize(expandSize);
    ui->lineEdit_3->setMaximumSize(expandSize);
    ui->lineEdit->setMaximumSize(expandSize);
    ui->lineEdit_4->setMaximumSize(expandSize);

    ui->comboBox_connection_type->setMinimumWidth(inputMinWidth);
    ui->comboBox_serial_mode->setMinimumWidth(inputMinWidth);
    ui->comboBox->setMinimumWidth(inputMinWidth);
    ui->comboBox_3->setMinimumWidth(inputMinWidth);
    ui->comboBox_2->setMinimumWidth(inputMinWidth);
    ui->lineEdit_tcp_server_ip->setMinimumWidth(inputMinWidth);
    ui->lineEdit_tcp_server_port->setMinimumWidth(inputMinWidth);
    ui->lineEdit_2->setMinimumWidth(140);
    ui->lineEdit_3->setMinimumWidth(140);
    ui->lineEdit->setMinimumWidth(140);
    ui->lineEdit_4->setMinimumWidth(140);

    ui->pushButton_query_10min_avg->setMinimumHeight(controlHeight);
    ui->pushButton_refreshSerial->setMinimumHeight(controlHeight);
    ui->pushButton_refreshSerial->setMinimumWidth(0);
    ui->pushButton_refreshSerial->setMaximumWidth(16777215);
}

namespace {

int comPortSortKey(const QString& portName)
{
    if (portName.startsWith(QLatin1String("COM"), Qt::CaseInsensitive)) {
        return portName.mid(3).toInt();
    }
    return -1;
}

void applyPortSelection(QComboBox* combo, const QString& portName)
{
    if (!combo || portName.isEmpty() || portName == QStringLiteral("（未检测到串口）")) {
        return;
    }

    int index = combo->findText(portName);
    if (index < 0) {
        combo->addItem(portName);
        index = combo->findText(portName);
    }
    if (index >= 0) {
        combo->setCurrentIndex(index);
    }
}

} // namespace

void MainWindow::refreshSerialPortList()
{
    const QString previousPort1 = ui->comboBox_3->currentText();
    const QString previousPort2 = ui->comboBox->currentText();

    QStringList ports;
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        ports.append(info.portName());
    }

    std::sort(ports.begin(), ports.end(), [](const QString& a, const QString& b) {
        const int aNum = comPortSortKey(a);
        const int bNum = comPortSortKey(b);
        if (aNum >= 0 && bNum >= 0) {
            return aNum < bNum;
        }
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    const auto refillCombo = [](QComboBox* combo, const QStringList& portNames) {
        if (!combo) {
            return;
        }
        combo->blockSignals(true);
        combo->clear();
        if (portNames.isEmpty()) {
            combo->addItem(QStringLiteral("（未检测到串口）"));
            combo->setEnabled(false);
        } else {
            combo->addItems(portNames);
            combo->setEnabled(true);
        }
        combo->blockSignals(false);
    };

    refillCombo(ui->comboBox_3, ports);
    refillCombo(ui->comboBox, ports);

    if (!ports.isEmpty()) {
        applyPortSelection(ui->comboBox_3, previousPort1);
        applyPortSelection(ui->comboBox, previousPort2);
    }
}

void MainWindow::onRefreshSerialPortsClicked()
{
    refreshSerialPortList();

    QStringList ports;
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        ports.append(info.portName());
    }

    if (ui->textBrowser) {
        if (ports.isEmpty()) {
            ui->textBrowser->append(QStringLiteral("[串口] 已刷新，当前未检测到可用串口"));
        } else {
            ui->textBrowser->append(QStringLiteral("[串口] 已刷新，检测到 %1 个串口：%2")
                                    .arg(ports.size())
                                    .arg(ports.join(QStringLiteral(", "))));
        }
    }
}

void MainWindow::setupSharedSerialUi()
{
    ui->pushButton_btnSerial->hide();
    ui->label_21->hide();
    ui->comboBox_6->hide();
    ui->label_22->hide();
    ui->comboBox_7->hide();
}

void MainWindow::setupDeviceModifyUi()
{
    ui->pushButton_2->hide();
    ui->pushButton_delete->hide();
    ui->layoutWidget2->hide();
    ui->textBrowser_2->hide();
    ui->chartWidget->hide();
    if (ui->layoutWidget1) {
        ui->layoutWidget1->hide();
    }

    QVBoxLayout* pageLayout = new QVBoxLayout(ui->page_2);
    pageLayout->setContentsMargins(16, 16, 16, 16);
    pageLayout->setSpacing(12);

    m_esdEditPanel = new EsdEditPanel(ui->page_2);
    pageLayout->addWidget(m_esdEditPanel);

    m_esdEditPanel->setConnectionChecker([this]() {
        return getToolSerialWorker() != nullptr;
    });
    connect(m_esdEditPanel, &EsdEditPanel::frameSendRequested,
            this, &MainWindow::onEsdEditFrameSend);

    setDeviceModifyInteractionEnabled(false);
    applyDeviceModifyPageStyle();
}

QString MainWindow::loadDeviceModifyPassword() const
{
    const QString iniPath = QCoreApplication::applicationDirPath() + QStringLiteral("/device_modify.ini");
    QSettings settings(iniPath, QSettings::IniFormat);
    settings.setIniCodec("UTF-8");
    const QString password = settings.value(QStringLiteral("DeviceModify/password"),
                                          QStringLiteral("888888")).toString();
    return password;
}

void MainWindow::setDeviceModifyInteractionEnabled(bool enabled)
{
    if (m_esdEditPanel) {
        m_esdEditPanel->setEnabled(enabled);
    }
}

void MainWindow::onStackedWidgetCurrentChanged(int index)
{
    if (m_prevStackedPageIndex == kDeviceModifyPageIndex && index != kDeviceModifyPageIndex) {
        m_deviceModifyUnlocked = false;
        setDeviceModifyInteractionEnabled(false);
    }

    if (index == kDeviceModifyPageIndex) {
        QTimer::singleShot(0, this, &MainWindow::promptDeviceModifyPassword);
    }

    m_prevStackedPageIndex = index;
}

void MainWindow::promptDeviceModifyPassword()
{
    if (!m_esdEditPanel || ui->stackedWidget->currentIndex() != kDeviceModifyPageIndex) {
        return;
    }

    m_deviceModifyUnlocked = false;
    setDeviceModifyInteractionEnabled(false);

    const QString correctPassword = loadDeviceModifyPassword();

    while (ui->stackedWidget->currentIndex() == kDeviceModifyPageIndex) {
        bool ok = false;
        const QString input = QInputDialog::getText(
            this,
            QStringLiteral("设备修改验证"),
            QStringLiteral("请输入设备修改密码："),
            QLineEdit::Password,
            QString(),
            &ok);

        if (!ok) {
            return;
        }

        if (input == correctPassword) {
            m_deviceModifyUnlocked = true;
            setDeviceModifyInteractionEnabled(true);
            return;
        }

        QMessageBox::warning(this,
                             QStringLiteral("密码错误"),
                             QStringLiteral("密码不正确，请重新输入。"));
    }
}

void MainWindow::applyDeviceModifyPageStyle()
{
    if (!m_esdEditPanel) {
        return;
    }

    if (ui->page_2) {
        ui->page_2->setStyleSheet(QStringLiteral("QWidget#page_2 { background-color: #f5f7fb; }"));
    }

    applyAdminPanelFont(m_esdEditPanel);
}

void MainWindow::onEsdEditFrameSend(const QByteArray& frame, const QString& expectedFunc)
{
    if (!m_deviceModifyUnlocked) {
        if (m_esdEditPanel) {
            m_esdEditPanel->handleToolFailed(QStringLiteral("请先输入设备修改密码"));
        }
        return;
    }

    SerialWorker* worker = getToolSerialWorker();
    if (!worker) {
        if (m_esdEditPanel) {
            m_esdEditPanel->handleToolFailed(QStringLiteral("请先在连接设置中连接串口"));
        }
        return;
    }

    m_toolContext = ToolCommandContext::EsdEdit;
    m_activeToolWorker = worker;
    worker->sendToolCommand(frame, expectedFunc);
}

SerialWorker* MainWindow::getToolSerialWorker() const
{
    if (m_worker2 && m_worker2->isConnectionOpen()) {
        return m_worker2;
    }
    if (m_worker1 && m_worker1->isConnectionOpen()) {
        return m_worker1;
    }
    return nullptr;
}

QByteArray MainWindow::buildModbusHexFrame(const QString& hexPayload) const
{
    QByteArray sendData;
    bool ok = true;
    for (int i = 0; i < hexPayload.length() && ok; i += 2) {
        sendData.append(static_cast<char>(hexPayload.mid(i, 2).toUInt(&ok, 16)));
    }
    if (!ok) {
        return QByteArray();
    }
    const uint16_t crc = calcrc(sendData);
    sendData.append(static_cast<char>(crc & 0xFF));
    sendData.append(static_cast<char>((crc >> 8) & 0xFF));
    return sendData;
}

bool MainWindow::sendToolFrame(const QString& hexPayload, const QString& expectedFunc, ToolCommandContext context)
{
    SerialWorker* worker = getToolSerialWorker();
    if (!worker) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请先在连接设置中连接串口"));
        return false;
    }
    const QByteArray data = buildModbusHexFrame(hexPayload);
    if (data.isEmpty()) {
        QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("数据构造失败"));
        return false;
    }
    m_toolContext = context;
    m_activeToolWorker = worker;
    worker->sendToolCommand(data, expectedFunc);
    return true;
}

void MainWindow::onToolCommandFailed(const QString& reason)
{
    const ToolCommandContext context = m_toolContext;
    m_toolContext = ToolCommandContext::None;
    m_activeToolWorker = nullptr;

    if (context == ToolCommandContext::EsdEdit && m_esdEditPanel) {
        m_esdEditPanel->handleToolFailed(reason);
        return;
    }
    QMessageBox::warning(this, QStringLiteral("提示"), reason);
}

void MainWindow::onToolCommandCompleted(const QByteArray& frame, const QString& expectedFuncCode)
{
    const QString hexStr = QString(frame.toHex().toUpper());
    const ToolCommandContext context = m_toolContext;
    m_toolContext = ToolCommandContext::None;
    m_activeToolWorker = nullptr;

    switch (context) {
    case ToolCommandContext::DeviceTestQuery: {
        QString status = QStringLiteral("关闭");
        if (expectedFuncCode == QLatin1String("0110") && hexStr.length() >= 16) {
            const QString dataHex = hexStr.mid(12, 4);
            if (dataHex == "3000") status = QStringLiteral("内外触发");
            else if (dataHex == "2000") status = QStringLiteral("外部触发");
            else if (dataHex == "1000") status = QStringLiteral("内部触发");
            else if (dataHex == "0000") status = QStringLiteral("无触发");
            else status = dataHex;
        } else if ((expectedFuncCode == QLatin1String("0113") || expectedFuncCode == QLatin1String("0120"))
                   && hexStr.length() >= 16) {
            bool ok = false;
            const uint16_t data = hexStr.mid(12, 4).toUInt(&ok, 16);
            status = (ok && (data & 0x1000)) ? QStringLiteral("开启") : QStringLiteral("关闭");
        }
        ui->lineEdit_7->setText(status);
        break;
    }
    case ToolCommandContext::DeviceTestApply:
        ui->lineEdit_10->setText(QStringLiteral("已写入"));
        break;
    case ToolCommandContext::EsdEdit:
        if (m_esdEditPanel) {
            m_esdEditPanel->handleToolResponse(frame, expectedFuncCode);
        }
        break;
    default:
        break;
    }
}

void MainWindow::restoreFromConfig()
{
    QList<ImageConfig> imageConfigs;
    int savedCurrentIndex = -1;
    SerialConfig serialConfig;
    bool separateEnvEsd = true;
    QList<DisplayScreenConfig> displayScreens;

    bool loadSuccess = m_configManager->loadConfig(imageConfigs, savedCurrentIndex, serialConfig, separateEnvEsd, displayScreens);
    if (!loadSuccess) {
        writeCrashLog("[Config] 配置文件读取失败或不存在，启动默认配置");
        return;
    }

    m_displayScreens = displayScreens;
    m_separateEnvEsd = separateEnvEsd;
    if (ui->chkSeparateEnvEsd) {
        ui->chkSeparateEnvEsd->blockSignals(true);
        ui->chkSeparateEnvEsd->setChecked(m_separateEnvEsd);
        ui->chkSeparateEnvEsd->blockSignals(false);
    }
    if (requestHandler) {
        requestHandler->notifyDisplayModeChange(m_separateEnvEsd);
    }

    for (const ImageConfig& imgConfig : imageConfigs) {
        if (!QFile::exists(imgConfig.imagePath)) {
            writeCrashLog(QString("[Config] 图片文件不存在，跳过：%1").arg(imgConfig.imagePath));
            continue;
        }

        addImageToHistory(imgConfig.imagePath);

        int currentImgIndex = findImageIndex(imgConfig.imagePath);
        if (currentImgIndex == -1) {
            writeCrashLog(QString("[Config] 图片索引查找失败，跳过设备恢复：%1").arg(imgConfig.imagePath));
            continue;
        }

        // 设置主题
        m_imageDeviceList[currentImgIndex].theme = imgConfig.theme;

        ImageDeviceData& currentData = m_imageDeviceList[currentImgIndex];
        QWidget* parentWidget = ui->labelImage->parentWidget();
        if (!parentWidget) {
            writeCrashLog("[Config] 设备父控件获取失败，跳过设备恢复");
            continue;
        }

        // 恢复原有逻辑：读取绝对像素坐标并设置
        for (const DeviceConfig& devConfig : imgConfig.devices) {
            DeviceWidget* dev = new DeviceWidget(devConfig.type, devConfig.id, parentWidget);
            if (!dev) {
                writeCrashLog(QString("[Config] 设备创建失败：%1").arg(devConfig.id));
                continue;
            }

            // 直接设置绝对像素位置（和原来一致）
            dev->setPos(QPoint(devConfig.x, devConfig.y));
            dev->hide();

            // 连接选中信号
            wireDeviceSelectionSignals(dev);

            currentData.devices.append(dev);

            // 更新设备ID计数器（和原来一致）
            int currentId = devConfig.id.mid(1).toInt();
            if (currentId >= m_devIdCounters[devConfig.type]) {
                m_devIdCounters[devConfig.type] = currentId + 1;
            }

            // 日志恢复为绝对像素坐标
            writeCrashLog(QString("[Config] 恢复设备成功：类型=%1，ID=%2，像素坐标=(%3,%4)")
                         .arg(devConfig.type).arg(devConfig.id).arg(devConfig.x).arg(devConfig.y));
        }
    }

    // 恢复上次选中的图片（和原来一致）
    if (savedCurrentIndex >= 0 && savedCurrentIndex < m_imageDeviceList.size()) {
        m_currentImageIndex = savedCurrentIndex;
        switchToImage(m_currentImageIndex);
        if (ui->cmbImageList) {
            ui->cmbImageList->setCurrentIndex(m_currentImageIndex + 1);
        }
        writeCrashLog(QString("[Config] 恢复上次选中图片：%1")
                     .arg(QFileInfo(m_imageDeviceList[savedCurrentIndex].imagePath).fileName()));
    }

    // 应用串口配置
    if (!serialConfig.port1.isEmpty()) {
        int index1 = ui->comboBox_3->findText(serialConfig.port1);
        if (index1 < 0) {
            ui->comboBox_3->addItem(serialConfig.port1);
            index1 = ui->comboBox_3->findText(serialConfig.port1);
        }
        if (index1 >= 0) {
            ui->comboBox_3->setCurrentIndex(index1);
        }
    }

    if (!serialConfig.port2.isEmpty()) {
        int index2 = ui->comboBox->findText(serialConfig.port2);
        if (index2 < 0) {
            ui->comboBox->addItem(serialConfig.port2);
            index2 = ui->comboBox->findText(serialConfig.port2);
        }
        if (index2 >= 0) {
            ui->comboBox->setCurrentIndex(index2);
        }
    }

    if (!serialConfig.baudRate.isEmpty()) {
        int indexBaud = ui->comboBox_2->findText(serialConfig.baudRate);
        if (indexBaud >= 0) {
            ui->comboBox_2->setCurrentIndex(indexBaud);
        }
    }

    if (!serialConfig.sendInterval.isEmpty()) {
        ui->lineEdit->setText(serialConfig.sendInterval);
    }

    if (!serialConfig.overtime.isEmpty()) {
        ui->lineEdit_2->setText(serialConfig.overtime);
    }

    if (!serialConfig.maxResend.isEmpty()) {
        ui->lineEdit_3->setText(serialConfig.maxResend);
    }

    if (!serialConfig.delay.isEmpty()) {
        ui->lineEdit_4->setText(serialConfig.delay);
    }

    // 恢复连接类型和TCP Server配置
    ui->comboBox_connection_type->setCurrentIndex(serialConfig.connectionType);
    if (serialConfig.serialMode == 1) {
        ui->comboBox_serial_mode->setCurrentIndex(1);
    } else {
        ui->comboBox_serial_mode->setCurrentIndex(0);
    }
    ui->lineEdit_tcp_server_ip->setText(serialConfig.tcpServerIp);
    ui->lineEdit_tcp_server_port->setText(serialConfig.tcpServerPort);

    // 根据恢复的连接类型更新UI显示
    onConnectionTypeChanged(serialConfig.connectionType);
    onSerialModeChanged(ui->comboBox_serial_mode->currentIndex());

    writeCrashLog("[Config] 串口配置加载成功");
    writeCrashLog(QStringLiteral("[Config] 显示屏映射加载：%1 条").arg(m_displayScreens.size()));

    // 自动连接串口并启动轮询
    QTimer::singleShot(100, this, &MainWindow::autoConnect);
}
// 保存当前图片和设备到配置文件
void MainWindow::saveToConfig()
{
    QList<ImageConfig> imageConfigs;

    for (const ImageDeviceData& imgData : m_imageDeviceList) {
        if (imgData.imagePath.isEmpty() || !QFile::exists(imgData.imagePath)) {
            writeCrashLog(QString("[Config] 图片路径无效，跳过保存：%1").arg(imgData.imagePath));
            continue;
        }

        ImageConfig imgConfig;
        imgConfig.imagePath = imgData.imagePath;
        imgConfig.theme = imgData.theme; // 保存主题

        // 恢复原有逻辑：存储绝对像素坐标
        for (DeviceWidget* dev : imgData.devices) {
            if (!dev) continue;

            DeviceConfig devConfig;
            devConfig.type = dev->getType();
            devConfig.id = dev->getId();
            devConfig.x = dev->getPos().x(); // 绝对像素X
            devConfig.y = dev->getPos().y(); // 绝对像素Y
            imgConfig.devices.append(devConfig);
        }

        imageConfigs.append(imgConfig);
    }

    // 创建串口配置
    SerialConfig serialConfig;
    serialConfig.port1 = ui->comboBox_3->currentText();        // 线程一COM口
    serialConfig.port2 = ui->comboBox->currentText();         // 线程二COM口
    serialConfig.baudRate = ui->comboBox_2->currentText();    // 波特率
    serialConfig.sendInterval = ui->lineEdit->text();         // 发送间隔
    serialConfig.overtime = ui->lineEdit_2->text();           // 超时时间
    serialConfig.maxResend = ui->lineEdit_3->text();          // 最大重发次数
    serialConfig.delay = ui->lineEdit_4->text();              // 延迟时间
    serialConfig.connectionType = ui->comboBox_connection_type->currentIndex(); // 连接类型
    serialConfig.serialMode = ui->comboBox_serial_mode->currentIndex(); // 串口模式
    serialConfig.tcpServerIp = ui->lineEdit_tcp_server_ip->text(); // TCP Server IP地址
    serialConfig.tcpServerPort = ui->lineEdit_tcp_server_port->text(); // TCP Server 端口

    bool saveSuccess = m_configManager->saveConfig(imageConfigs, m_currentImageIndex, serialConfig, m_separateEnvEsd, m_displayScreens);
    if (saveSuccess) {
        writeCrashLog(QString("[Config] 配置保存成功：图片数=%1，当前选中索引=%2，显示屏=%3")
                     .arg(imageConfigs.size()).arg(m_currentImageIndex).arg(m_displayScreens.size()));
    } else {
        writeCrashLog("[Config] 配置保存失败！");
    }
}

// 自动连接串口并启动轮询
void MainWindow::autoConnect()
{
    if (m_isAllConnected) {
        return; // 已经连接，不需要重复连接
    }

    // 直接调用openuart方法连接并启动轮询
    // openuart方法会根据当前选择的连接类型（已从配置中恢复）启动相应的连接
    openuart();
}

void MainWindow::openuart()
{
    auto resetConnectButton = [this]() {
        ui->pushButton->setText(QStringLiteral("连接"));
        ui->pushButton->setEnabled(true);
    };

    if (m_isAllConnected) {
        m_worker1->closeSerial();
        m_worker2->closeSerial();
        m_singleLinkPolling = false;
        loadPollConfig();

        m_isAllConnected = false;
        isPollingActive = false;
        ui->pushButton->setText(QStringLiteral("连接"));
        ui->pushButton_3->setText(QStringLiteral("启用轮询"));
        writeCrashLog(QStringLiteral("连接已断开"));
        return;
    }

    bool sendOk;
    int sendIntervalSec = ui->lineEdit->text().toInt(&sendOk);
    int sendIntervalMs = sendOk && sendIntervalSec > 0 ? sendIntervalSec * 1000 : 5000;
    if (!sendOk || sendIntervalSec <= 0) {
        writeCrashLog(QStringLiteral("轮询间隔无效，已使用默认值5000ms"));
    }

    bool overtimeOk;
    int overtimeIntervalMs = ui->lineEdit_2->text().toInt(&overtimeOk);
    overtimeIntervalMs = overtimeOk && overtimeIntervalMs > 0 ? overtimeIntervalMs : 100;
    if (!overtimeOk || overtimeIntervalMs <= 0) {
        writeCrashLog(QStringLiteral("超时间隔无效，已使用默认值100ms"));
    }

    bool resendOk;
    int maxResendCount = ui->lineEdit_3->text().toInt(&resendOk);
    maxResendCount = resendOk && maxResendCount >= 1 ? maxResendCount : 3;
    if (!resendOk || maxResendCount < 1) {
        writeCrashLog(QStringLiteral("最大重发次数无效，已使用默认值3次"));
    }

    bool delayOk;
    int delayMs = ui->lineEdit_4->text().toInt(&delayOk);
    delayMs = delayOk && delayMs > 0 ? delayMs : 50;

    const int connectionType = ui->comboBox_connection_type->currentIndex();

    ui->pushButton->setText(QStringLiteral("连接中..."));
    ui->pushButton->setEnabled(false);

    if (connectionType == 0) {
        const int serialMode = ui->comboBox_serial_mode->currentIndex(); // 0=单串口 1=双串口
        const int baudRate = ui->comboBox_2->currentText().toInt();

        auto checkPort = [](const QString& portName) {
            return !portName.isEmpty() && (portName.startsWith(QStringLiteral("COM"))
                || portName.startsWith(QStringLiteral("/dev/")));
        };

        if (serialMode == 0) {
            const QString port = ui->comboBox_3->currentText();
            if (!checkPort(port)) {
                QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("COM口格式无效！请选择正确的COM口"));
                writeCrashLog(QStringLiteral("COM口格式无效：%1").arg(port));
                resetConnectButton();
                return;
            }

            m_singleLinkPolling = true;
            if (m_fullPollConfig.isEmpty()) {
                loadPollConfig();
            } else {
                syncPollConfigToWorkers(m_fullPollConfig);
            }

            m_worker1->openSerial(port, baudRate, sendIntervalMs, overtimeIntervalMs, maxResendCount, delayMs);
            m_worker1->startPolling();
            writeCrashLog(QStringLiteral("单串口模式：%1，线程一发送全部轮询配置").arg(port));
        } else {
            const QString port1 = ui->comboBox_3->currentText();
            const QString port2 = ui->comboBox->currentText();
            if (!checkPort(port1) || !checkPort(port2)) {
                QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("COM口格式无效！请选择正确的COM口"));
                writeCrashLog(QStringLiteral("COM口格式无效：端口1=%1，端口2=%2").arg(port1, port2));
                resetConnectButton();
                return;
            }
            if (port1 == port2) {
                QMessageBox::warning(this, QStringLiteral("提示"),
                                     QStringLiteral("双串口模式请选择两个不同的COM口！"));
                writeCrashLog(QStringLiteral("双串口模式端口重复：%1").arg(port1));
                resetConnectButton();
                return;
            }

            m_singleLinkPolling = false;
            if (m_fullPollConfig.isEmpty()) {
                loadPollConfig();
            } else {
                syncPollConfigToWorkers(m_fullPollConfig);
            }

            m_worker1->openSerial(port1, baudRate, sendIntervalMs, overtimeIntervalMs, maxResendCount, delayMs);
            m_worker2->openSerial(port2, baudRate, sendIntervalMs, overtimeIntervalMs, maxResendCount, delayMs);
            m_worker1->startPolling();
            m_worker2->startPolling();
            writeCrashLog(QStringLiteral("双串口模式：线程一=%1（C类），线程二=%2（W/T/E/I），波特率=%3，发送间隔=%4ms")
                         .arg(port1, port2).arg(baudRate).arg(sendIntervalMs));
        }

        m_isAllConnected = true;
        isPollingActive = true;
        ui->pushButton->setText(QStringLiteral("断开"));
        ui->pushButton_3->setText(QStringLiteral("停止轮询"));
        ui->pushButton->setEnabled(true);
    } else {
        const QString ipAddress = ui->lineEdit_tcp_server_ip->text().trimmed();
        const QString portStr = ui->lineEdit_tcp_server_port->text().trimmed();

        if (ipAddress.isEmpty() || portStr.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("本机地址和本机端口不能为空！"));
            writeCrashLog(QStringLiteral("TCP Server配置无效：IP地址或端口为空"));
            resetConnectButton();
            return;
        }

        bool portOk;
        const int port = portStr.toInt(&portOk);
        if (!portOk || port < 1 || port > 65535) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("本机端口无效！请输入1-65535之间的整数"));
            writeCrashLog(QStringLiteral("TCP Server端口无效：") + portStr);
            resetConnectButton();
            return;
        }

        m_worker1->openTcpServer(ipAddress, port, sendIntervalMs, overtimeIntervalMs, maxResendCount, delayMs);

        m_singleLinkPolling = true;
        if (m_fullPollConfig.isEmpty()) {
            loadPollConfig();
        } else {
            syncPollConfigToWorkers(m_fullPollConfig);
        }
        writeCrashLog(QStringLiteral("TCP Server：单链路模式，线程一发送全部轮询配置"));

        m_worker1->startPolling();
        writeCrashLog(QStringLiteral("TCP Server模式：线程一统一轮询，线程二不启动"));

        m_isAllConnected = true;
        isPollingActive = true;
        ui->pushButton->setText(QStringLiteral("断开"));
        ui->pushButton_3->setText(QStringLiteral("停止轮询"));
        ui->pushButton->setEnabled(true);

        writeCrashLog(QStringLiteral("TCP Server启动成功：%1:%2，发送间隔=%3ms")
                     .arg(ipAddress).arg(port).arg(sendIntervalMs));
    }
}
void MainWindow::sendNextData()
{
    // 检查连接状态，根据当前连接类型
    bool isConnectionOpen = false;
    QString connectionType = ui->comboBox_connection_type->currentText();
    if (connectionType == "串口通信") {
        isConnectionOpen = serial->isOpen();
    } else if (connectionType == "网络通信(TCP Server)") {
        // 对于TCP Server，我们需要检查worker中的连接状态
        // 这里简化处理，因为实际连接状态在worker中管理
        isConnectionOpen = true;
    }

    if (!isConnectionOpen || !isPollingActive || sendQueue.isEmpty()) {
            overtime->stop(); // 停止超时定时器
            return;
        }
    // 补充队列大小日志（无锁读取）
    int queueSize = 0;
    {
        QMutexLocker tempLocker(&queueMutex);
        queueSize = sendQueue.size();
    }
    writeCrashLog("【进入sendNextData】连接状态：" + QString(isConnectionOpen ? "已打开" : "已关闭") + "，当前队列大小：" + QString::number(queueSize));

    // 先检查连接状态
    if (!isConnectionOpen) {
        writeCrashLog("【错误】连接已关闭，停止发送");
        QMutexLocker locker(&queueMutex);
        sendQueue.clear();
        sendnum = 0;
        lastSentData.clear();
        QMessageBox::warning(this, "警告", "连接已断开，发送已停止！");
        return;
    }

    QByteArray currentData;
       bool isQueueEmpty = false; // 先定义变量存结果
       {
           QMutexLocker locker(&queueMutex);
           isQueueEmpty = sendQueue.isEmpty();
           if (!isQueueEmpty) {
               currentData = sendQueue.head(); // 只读取队列头部，不出队
           }
       }
       // 锁外写日志（关键修改）
       if (isQueueEmpty) {
           writeCrashLog("【提示】发送队列为空，重置未处理标识");
           lastSentData.clear();
           qDebug() << "【sendNextData】队列空，重置未处理标识完成";
           return;
       }
    // 日志输出当前发送数据
    QString logMsg = "[发送数据] " + currentData.toHex().toUpper();
    writeCrashLog(logMsg);

    // 设置预期响应（地址+功能码）
    currentExpectedAddrFunc.clear();
    if (currentData.size() >= 4) {
        QString addr = QString(currentData.mid(0,2).toHex().toUpper()).rightJustified(4,'0');
        QString func = QString(currentData.mid(2,2).toHex().toUpper()).rightJustified(4,'0');
        currentExpectedAddrFunc = addr + func;
    } else {
        writeCrashLog("【警告】发送数据长度不足4字节（无法解析地址+功能码），跳过该数据");
        // 移除无效数据，继续处理下一条
        QMutexLocker locker(&queueMutex);
        sendQueue.dequeue();
        locker.unlock();
        sendnum = 0;
        lastSentData.clear();
        sendNextData();
        return;
    }

    // 判断是否为新数据（重发时保留缓冲区，新数据已在延迟后清理过）
    bool isNewData = (lastSentData != currentData);
    if (isNewData) {
        lastSentData = currentData; // 记录本次数据
    }

    // 执行串口发送
    qint64 bytesWritten = serial->write(currentData);
    if (bytesWritten == -1) {
        writeCrashLog(QString("【错误】发送失败：%1").arg(serial->errorString()));
        QMessageBox::critical(this, "失败", "数据发送失败！");
        // 发送失败→移除当前数据，处理下一条
        QMutexLocker locker(&queueMutex);
        if (!sendQueue.isEmpty()) {
            sendQueue.dequeue();
        }
        locker.unlock();
        sendnum = 0;
        lastSentData.clear();
        sendNextData();
        return;
    }
    writeCrashLog(QString("【成功】发送完成，写入%1字节").arg(bytesWritten));
    serial->flush();

    // 发送成功→从队列移除当前数据
    {
        QMutexLocker locker(&queueMutex);
        if (!sendQueue.isEmpty()) {
            sendQueue.dequeue();
        }
        sendnum++; // 累加发送次数（用于重发计数）
    }

    // 启动超时定时器
    overtime->start();

    // 检查是否还有下一条数据（仅判断，不处理延迟，延迟由接收/超时逻辑触发）
    bool hasMoreData = false;
    {
        QMutexLocker locker(&queueMutex);
        hasMoreData = !sendQueue.isEmpty();
    }

    if (!hasMoreData) {
        writeCrashLog("队列已空");
        sendnum = 0;
        lastSentData.clear();
    }
}
void MainWindow::onTimeout()
{
    // 读取当前队列大小（无锁）
    int queueSize = 0;
    {
        QMutexLocker tempLocker(&queueMutex);
        queueSize = sendQueue.size();
    }

    // 检查连接状态，根据当前连接类型
    bool isConnectionOpen = false;
    QString connectionType = ui->comboBox_connection_type->currentText();
    if (connectionType == "串口通信") {
        isConnectionOpen = serial->isOpen();
    } else if (connectionType == "网络通信(TCP Server)") {
        // 对于TCP Server，我们需要检查worker中的连接状态
        // 这里简化处理，因为实际连接状态在worker中管理
        isConnectionOpen = true;
    }

    // 检查连接状态
    if (!isConnectionOpen) {
        writeCrashLog("【错误】连接已关闭，终止超时处理");
        sendnum = 0;
        currentExpectedAddrFunc.clear();
        lastSentData.clear();
        return;
    }

    if (sendnum < maxResendCount) {
            int delay = ui->lineEdit_4->text().toInt() > 0 ? ui->lineEdit_4->text().toInt() : 50;
            QTimer::singleShot(delay, [this, delay]() {
                recvBuffer.clear();
                sendNextData();
            });
        } else {
            writeCrashLog("【错误】连续" + QString::number(maxResendCount) + "次发送超时，丢弃当前数据，处理下一条");
            // 丢弃当前数据（从队列移除）→ 锁内只做dequeue，无日志
            {
                QMutexLocker locker(&queueMutex);
                if (!sendQueue.isEmpty()) {
                    sendQueue.dequeue();
                }
            }

        // 重置状态
        sendnum = 0;
        currentExpectedAddrFunc.clear();
        lastSentData.clear();

        if (isSingleTest) {
                   bool queueEmpty = false;
                   {
                       QMutexLocker locker3(&queueMutex);
                       queueEmpty = sendQueue.isEmpty();
                   }
                   // 锁外写日志
                   if (queueEmpty) {
                       writeCrashLog("[单次测试] 达到最大重发次数，测试结束");
                       isSingleTest = false;
                       if (isPollingActive) {
                           sendTimer->start();
                           writeCrashLog("[单次测试] 恢复轮询");
                       }
                       return;
                   }
               }

        int delay = ui->lineEdit_4->text().toInt() > 0 ? ui->lineEdit_4->text().toInt() : 50;
        QTimer::singleShot(delay, [this, delay]() {
            recvBuffer.clear();
            sendNextData();
        });
    }
}
void MainWindow::processModbusData(const QVector<QStringList>& tableData)
{
    QMutexLocker locker1(&m_newarrangeMutex);
        newarrange = tableData;

        QMutexLocker locker2(&m_configuredIdsMutex);
        configuredIds.clear();
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
    // qDebug() << "【已配置标识汇总】" << configuredIds;
}
void MainWindow::changemenu()
{
    QPushButton *triggeredButton = qobject_cast<QPushButton*>(sender());
    if(triggeredButton && triggeredButton->objectName()=="pushButton_lunxun")
    {
        newdialog dialog(this);
        if (dialog.exec() == QDialog::Accepted) {
            QVector<QStringList> tableData = dialog.getTableData();
            syncPollConfigToWorkers(tableData);
            writeCrashLog("配置数据已同步到线程和主线程");
        }
        return;
    }

    // 原有菜单切换逻辑（保留不变）
    QAction *triggeredAction = qobject_cast<QAction*>(sender());
    if (!triggeredAction) return;
    if (triggeredAction->objectName() == "action1") {
        ui->stackedWidget->setCurrentIndex(0);
    }
    else if(triggeredAction->objectName() == "action2"){
        ui->stackedWidget->setCurrentIndex(2); // page_2：设备修改（index 1 为已清空的 page_3）
        applyDeviceModifyPageStyle();
    }
    else if(triggeredAction->objectName() == "actionmap"){
        ui->stackedWidget->setCurrentIndex(3);
        updateMapPageToolbarsPosition();
    }
    else if(triggeredAction->objectName() == "action96"){
        ui->stackedWidget->setCurrentIndex(5);
    }
}
void MainWindow::recv()
{
    QByteArray newData = serial->readAll();
    if (newData.isEmpty()) return;
    recvBuffer.append(newData);

    writeCrashLog("【接收累积】长度：" + QString::number(recvBuffer.size())
                    + "，数据：" + recvBuffer.toHex().toUpper());

    if (recvBuffer.size() > 256) {
        recvBuffer = recvBuffer.mid(recvBuffer.size() - 256);
        writeCrashLog("【警告】缓冲区超256字节，保留最新256字节");
        ui->textBrowser->append("⚠️  缓冲区超上限，保留最新数据");
        ui->textBrowser->moveCursor(QTextCursor::End);
        return;
    }

    if (currentExpectedAddrFunc.isEmpty()) {
        writeCrashLog("【提示】未设置预期响应，暂不解析");
        return;
    }

    int expectedStart = -1;
    for (int i = 0; i <= recvBuffer.size() - 4; i++) {
        QString addr = QString(recvBuffer.mid(i,2).toHex().toUpper()).rightJustified(4,'0');
        QString func = QString(recvBuffer.mid(i+2,2).toHex().toUpper()).rightJustified(4,'0');
        if (addr + func == currentExpectedAddrFunc) {
            expectedStart = i;
            writeCrashLog("【找到预期帧起始】位置：" + QString::number(i) + "，地址+功能码：" + addr + func);
            break;
        }
    }

    if (expectedStart == -1) {
        writeCrashLog(QString("【提示】未找到预期帧起始（预期：%1），保留缓冲区").arg(currentExpectedAddrFunc));
        return;
    }

    QByteArray frameHeader = recvBuffer.mid(expectedStart, 4);
    QString funcCode = QString(frameHeader.mid(2, 2).toHex().toUpper());
    writeCrashLog("【解析功能码】" + funcCode);

    int expectedFrameLen = -1;
    if (recvBuffer.size() >= expectedStart + 6) {
        QByteArray regCountBytes = recvBuffer.mid(expectedStart + 4, 2);
        uint16_t regCount = (static_cast<unsigned char>(regCountBytes[0]) << 8) | static_cast<unsigned char>(regCountBytes[1]);
        expectedFrameLen = 2 + 2 + 2 + (regCount * 2) + 2;
        writeCrashLog("【动态计算帧长】寄存器数：" + QString::number(regCount) + "，预期帧长：" + QString::number(expectedFrameLen));
    }

    if (expectedFrameLen == -1) {
        writeCrashLog("【提示】未获取到寄存器数，保留缓冲区");
        return;
    }

    int currentFrameAvailableLen = recvBuffer.size() - expectedStart;
    if (currentFrameAvailableLen < expectedFrameLen) {
        writeCrashLog("【提示】帧不完整（需" + QString::number(expectedFrameLen)
                     + "字节，当前" + QString::number(currentFrameAvailableLen) + "字节），保留缓冲区");
        return;
    }

    QByteArray validFrame = recvBuffer.mid(expectedStart, expectedFrameLen);
    QByteArray dataToCrc = validFrame.left(expectedFrameLen - 2);
    uint16_t calcCrc = calcrc(dataToCrc);
    uint16_t recvCrc = (static_cast<unsigned char>(validFrame[expectedFrameLen-1]) << 8) | static_cast<unsigned char>(validFrame[expectedFrameLen-2]);

    if (calcCrc != recvCrc) {
        QString errInfo = QString("CRC校验失败：计算值=0x%1，接收值=0x%2")
                          .arg(QString::number(calcCrc,16).toUpper(), 4, '0')
                          .arg(QString::number(recvCrc,16).toUpper(), 4, '0');
        writeCrashLog("【错误】" + errInfo);
        ui->textBrowser->append(errInfo);
        ui->textBrowser->moveCursor(QTextCursor::End);
        recvBuffer = recvBuffer.mid(expectedStart + 1);
        return;
    }

    writeCrashLog("[接收数据] " + validFrame.toHex().toUpper());
    QString addr = QString(validFrame.mid(0, 2).toHex().toUpper()).rightJustified(4, '0');
    QString frameHex = validFrame.toHex().toUpper();
    QString crcHex = QString::number(recvCrc, 16).toUpper().rightJustified(4, '0');
    QTextBrowser* targetBrowser = isPollingActive ? ui->textBrowser : ui->textBrowser_2;

    parsingdata(validFrame);
    recvBuffer = recvBuffer.mid(expectedStart + expectedFrameLen);
    currentExpectedAddrFunc.clear();

    // 接收成功后重置发送次数，避免影响下一条
    sendnum = 0;

    int delay = ui->lineEdit_4->text().toInt() > 0 ? ui->lineEdit_4->text().toInt() : 50;
    QTimer::singleShot(delay, [this, delay]() {
        recvBuffer.clear();
        writeCrashLog(QString("【超时重发+延迟%1ms】清理缓冲区，发送下一条数据").arg(delay));
        sendNextData();
    });
}



uint16_t MainWindow::calcrc(const QByteArray &data) const
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

void MainWindow::parsingdata(const QByteArray& frame)
{
    const int UNIT_LEN = 2;
        const int CRC_LEN = 2;

        // 最小帧长校验
        if (frame.size() < 10) {
            // qDebug() << "❌ 帧无效（长度不足10字节）：" << frame.toHex().toUpper();
            return;
        }

        // 解析地址、功能码、寄存器数
        QString addressHex = QString(frame.mid(0, UNIT_LEN).toHex().toUpper()).rightJustified(4, '0');
        QString funcCode = QString(frame.mid(UNIT_LEN, UNIT_LEN).toHex().toUpper());
        QByteArray regCountBytes = frame.mid(4, UNIT_LEN);
        uint16_t regCount = (static_cast<unsigned char>(regCountBytes[0]) << 8) | static_cast<unsigned char>(regCountBytes[1]);
        int dataStart = 6; // 数据段起始位置
        int dataLen = regCount * UNIT_LEN; // 数据段长度

        // 校验数据段长度是否匹配
        if (frame.size() < dataStart + dataLen + CRC_LEN) {
            // qDebug() << "❌ 数据段长度不匹配：预期" << dataLen << "字节，实际可用" << (frame.size() - dataStart - CRC_LEN) << "字节";
            return;
        }

        // 提取数据位（所有寄存器原始数据，不丢弃，用于JSON和递增标识解析）
        QList<QString> dataBits;
        // qDebug() << "【数据提取】：";
        // qDebug() << "  数据起始位置：" << dataStart;
        // qDebug() << "  数据长度：" << dataLen;
        // qDebug() << "  寄存器数量：" << regCount;

        for (int i = 0; i < regCount; ++i) {
            int pos = dataStart + i * UNIT_LEN;
            QString dataBit = QString(frame.mid(pos, UNIT_LEN).toHex().toUpper());
            dataBits.append(dataBit);
            // qDebug() << "  数据位" << i+1 << "（位置" << pos << "-" << pos+UNIT_LEN-1 << "）：" << dataBit;
        }

        // 调试输出所有数据位
        // qDebug() << "【完整数据位列表】：" << dataBits;

        // 功能码匹配类型
        QString typePrefix;
        bool funcValid = true;
        if (funcCode == "1011" || funcCode == "0110") { // 腕带功能码
            typePrefix = "W";
        } else if (funcCode == "1014" || funcCode == "0113") { // 台垫功能码
            typePrefix = "T";
        } else if (funcCode == "1021" || funcCode == "0120") { // 设备功能码
            typePrefix = "E";
        } else if (funcCode == "1030") { // 尘埃功能码（不递增）
            typePrefix = "C";
        } else {
            qDebug() << "❌ 不支持的功能码：" << funcCode;
            funcValid = false;
        }
        if (!funcValid) return;

        // 匹配配置行和标识（适配C类型）
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

        // ------------ W/T/E类型解析（核心：每个寄存器对应一个递增标识）------------
        if (typePrefix != "C") {
            if (funcCode == "0110" || funcCode == "0113" || funcCode == "0120") {
                        QString statusDesc = "未知状态"; // 初始化状态

                        // 仅处理第一个寄存器（发送时固定读取1个寄存器）
                        if (dataBits.size() > 0) {
                            QString dataHex = dataBits[0];
                            // 十六进制转十进制
                            bool valueOk;
                            uint32_t dataValue = dataHex.toUInt(&valueOk, 16);
                            if (valueOk) {
                                // 添加qdebug输出，显示判断数据
                                // qDebug() << "【通道状态检测】功能码:" << funcCode;
                                // qDebug() << "  原始十六进制数据:" << dataHex;
                                // qDebug() << "  转换后十进制值:" << dataValue;

                                if (funcCode == "0113" || funcCode == "0120" || funcCode == "0110") {
                                    // 提取数据的第一个十六进制数字（高4位）
                                    QString firstHexChar = dataHex.left(1);
                                    // qDebug() << "  数据：" << dataHex;
                                    // qDebug() << "  第一个十六进制数字：" << firstHexChar;

                                    // 根据第一个十六进制数字判断状态，1开头显示开启
                                    if (firstHexChar == "1") {
                                        statusDesc = "开启";
                                    } else {
                                        statusDesc = "关闭";
                                    }
                                } else {
                                    // qDebug() << "  未知功能码，状态暂不处理";
                                    statusDesc = "暂不处理";
                                }
                                // qDebug() << "  最终状态:" << statusDesc;
                            } else {
                                // qDebug() << "【通道状态检测】数据解析失败，原始数据:" << dataHex;
                                statusDesc = "解析失败";
                            }
                        }

                        // 关键：将状态显示到lineEdit_7（UI线程安全）
                        QMetaObject::invokeMethod(this, [this, statusDesc]() {
                            ui->lineEdit_7->setText(statusDesc);
                        }, Qt::QueuedConnection);

                        // 日志记录（简化，仅保留核心信息）
                        writeCrashLog(QString("[自定义功能码解析] 功能码：%1，状态：%2")
                                     .arg(funcCode).arg(statusDesc));

                        // 详细UI显示，包括判断依据
                        QString resultInfo = QString("\n📊 【自定义功能码返回】");
                        resultInfo += QString("\n  功能码：%1").arg(funcCode);
                        if (dataBits.size() > 0) {
                            QString dataHex = dataBits[0];
                            bool valueOk;
                            uint32_t dataValue = dataHex.toUInt(&valueOk, 16);
                            if (valueOk) {
                                // 提取数据的第一个十六进制数字
                                QString firstHexChar = dataHex.left(1);
                                resultInfo += QString("\n  原始数据：%1").arg(dataHex);
                                resultInfo += QString("\n  第一个十六进制数字：%1").arg(firstHexChar);
                                resultInfo += QString("\n  十进制值：%1").arg(dataValue);
                                resultInfo += QString("\n  二进制值：%1").arg(QString::number(dataValue, 2).rightJustified(16, '0'));
                                resultInfo += QString("\n  判断依据：第一个十六进制数字 = %1").arg(firstHexChar);
                                resultInfo += QString("\n  最终状态：%1").arg(statusDesc);
                            }
                        }
                        resultInfo += "\n" + QString(50, '-');
                        ui->textBrowser->append(resultInfo);
                        ui->textBrowser->moveCursor(QTextCursor::End);

                        return; // 跳过原有W/T/E解析逻辑，直接结束
                    }


            // 处理所有返回的寄存器数据（不限制数量，每个数据对应一个递增标识）
            int processCount = dataBits.size(); // 关键：改为处理所有数据位，而非受incCount限制
            if (processCount <= 0) {
                // qDebug() << "❌ 无有效数据可处理：数据位" << dataBits.size() << "个";
                return;
            }


            for (int i = 0; i < processCount; ++i) {
                const int channel = configFound ? (matchedInfo.range.startChannel + i) : (i + 1);
                const QString currentId = configFound
                    ? makePollDeviceId(typePrefix, matchedInfo.modbusAddr, channel)
                    : ("测试" + typePrefix + QString::number(i + 1));

                QString dataHex = dataBits[i];
                // 关键修复：声明并赋值 dataValue（十六进制转十进制）
                bool valueOk;
                uint32_t dataValue = dataHex.toUInt(&valueOk, 16); // 16表示按十六进制解析
                if (!valueOk) {
                    dataValue = 0; // 转换失败时设默认值
                    // qDebug() << "❌ 数据转换失败：" << dataHex << "→ 设为默认值0";
                }

                // 所有键都拼接标识（无例外）
                QMutexLocker locker(&m_addressFuncDataMutex);
                addressFuncData[addressHex + funcCode + currentId] = QStringList() << dataHex;
                // 状态判断（保留原有阈值逻辑，现在dataValue已正确赋值）
                QString status, statusDesc;
                if (typePrefix == "W") {
                    status = (dataValue >= 75 && dataValue <= 3500) ? "1" : "2";
                    statusDesc = status == "1" ? "正常" : "异常";
                } else if (typePrefix == "T") {
                    status = (dataValue >= 75 && dataValue <= 350) ? "1" : "2";
                    statusDesc = status == "1" ? "正常" : "异常";
                } else if (typePrefix == "E") {
                    status = (dataValue >= 0 && dataValue <= 2500) ? "1" : "2";
                    statusDesc = status == "1" ? "正常" : "异常";
                }
            }

        }
    // ------------ 新增C类型（尘埃）解析逻辑 ------------
    else {
        // 校验尘埃数据长度（至少22个寄存器：0002-0017）
        if (dataBits.size() < 22) {
            qDebug() << "❌ 尘埃数据不完整：至少需要22个寄存器数据，当前" << dataBits.size() << "个";
            return;
        }

        // 定义需要解析的尘埃数据（名称+寄存器索引）
        struct DustData {
            QString name;       // 数据名称
            int statusIdx;      // 状态寄存器索引（对应dataBits的索引）
            int valueHighIdx;   // 测试值高16位索引
            int valueLowIdx;    // 测试值低16位索引
            bool isTempHum;     // 是否为温湿度（×0.1）
        };

        QList<DustData> dustDataList = {
            {"温度", 0, 1, -1, true},    // 状态0002(索引0)，值0003(索引1)
            {"湿度", 2, 3, -1, true},    // 状态0004(索引2)，值0005(索引3)
            {"0.3um尘埃", 4, 5, 6, false},// 状态0006(4)，值0007(5)+0008(6)
            {"0.5um尘埃", 7, 8, 9, false},// 状态0009(7)，值000A(8)+000B(9)
            {"1.0um尘埃", 10, 11, 12, false},// 状态000C(10)，值000D(11)+000E(12)
            {"2.5um尘埃", 13, 14, 15, false},// 状态000F(13)，值0010(14)+0011(15)
            {"5.0um尘埃", 16, 17, 18, false},// 状态0012(16)，值0013(17)+0014(18)
            {"10um尘埃", 19, 20, 21, false} // 状态0015(19)，值0016(20)+0017(21)
        };

        // 拼接解析结果
//        QString resultInfo = QString("\n📊 【尘埃（C类型）返回数据】");
//        resultInfo += "\n  地址：" + addressHex + "，功能码：" + funcCode + "，寄存器数：" + QString::number(regCount);
//        resultInfo += "\n  状态说明：启用(bit12=1) | 正常(bit8=0) | 异常(bit8=1)";

        // 解析每个数据项
        for (const DustData& item : dustDataList) {
            // 解析状态
            bool statusOk = false;
            uint16_t statusVal = dataBits[item.statusIdx].toUInt(&statusOk, 16);
//            if (!statusOk) {
//                resultInfo += QString("\n  - %1：状态解析失败").arg(item.name);
//                continue;
//            }

            // 状态判断
            bool isEnabled = (statusVal & 0x1000) != 0; // bit12=1：启用
            bool isNormal = (statusVal & 0x0100) == 0;  // bit8=0：正常
            QString statusDesc = isEnabled ? (isNormal ? "启用-正常" : "启用-异常") : "未启用";

            // 解析测试值
            QString valueDesc = "无";
            if (isEnabled) {
                if (item.isTempHum) {
                    // 温湿度：16位值×0.1
                    bool valOk = false;
                    uint16_t val = dataBits[item.valueHighIdx].toUInt(&valOk, 16);
                    if (valOk) {
                        double actualVal = val * 0.1;
                        valueDesc = item.name == "温度" ? QString("%1℃").arg(actualVal, 0, 'f', 1)
                                                       : QString("%1%RH").arg(actualVal, 0, 'f', 1);
                    } else {
                        valueDesc = "解析失败";
                    }
                } else {
                    // 尘埃：32位值（高16位+低16位）
                    if (item.valueLowIdx >= dataBits.size()) {
                        valueDesc = "数据不完整";
                        continue;
                    }
                    bool highOk = false, lowOk = false;
                    uint16_t highVal = dataBits[item.valueHighIdx].toUInt(&highOk, 16);
                    uint16_t lowVal = dataBits[item.valueLowIdx].toUInt(&lowOk, 16);
                    if (highOk && lowOk) {
                        uint32_t actualVal = (static_cast<uint32_t>(highVal) << 16) | lowVal;
                        valueDesc = QString("%1 个").arg(actualVal);
                    } else {
                        valueDesc = "解析失败";
                    }
                }
            }

//            resultInfo += QString("\n  - %1：%2，测试值：%3")
//                          .arg(item.name).arg(statusDesc).arg(valueDesc);
        }
        QString currentId;

//        // 显示到界面
//        resultInfo += "\n" + QString(50, '-');
//        ui->textBrowser->append(resultInfo);
//        ui->textBrowser->moveCursor(QTextCursor::End);
//        writeCrashLog("【尘埃数据解析完成】" + resultInfo);
//        QMutexLocker locker2(&m_addressFuncDataMutex);
//        addressFuncData[addressHex + funcCode + currentId] = dataBits;
    }
        if (!m_btn6SendKey.isEmpty()) { // 只有发送过pushButton_6指令才校验
                // 解析响应帧的地址和功能码（前4字节=8字符）
                QString respKey = frame.left(4).toHex().toUpper(); // 地址(2字节)=4字符
                respKey += frame.mid(2,2).toHex().toUpper();       // 功能码(2字节)=4字符
                // 响应帧核心参数示例：00080020

                // 校验：响应的地址和功能码与发送的一致 → 弹窗成功
                if (respKey == m_btn6SendKey) {
                    QMessageBox::information(this, "成功", "修改成功！");
                    m_btn6SendKey.clear(); // 清空标记，避免重复弹窗
                }
            }
    // 超时处理（不变）
    if (overtime->isActive()) {
        overtime->stop();
        sendnum = 0;
    }
}
void MainWindow::timerTriggerSend()
{
    QString type = ui->comboBox_what->currentText(); // 添加type变量声明

    pollCount++;
    if (pollCount >= 10) {
        ui->textBrowser->clear();
        pollCount = 0;
    }

    // 检查连接状态，根据当前连接类型
    bool isConnectionOpen = false;
    QString connectionType = ui->comboBox_connection_type->currentText();
    if (connectionType == "串口通信") {
        isConnectionOpen = serial->isOpen();
    } else if (connectionType == "网络通信(TCP Server)") {
        // 对于TCP Server，我们需要检查worker中的连接状态
        // 这里简化处理，因为实际连接状态在worker中管理
        isConnectionOpen = true;
    }

    if (!isConnectionOpen|| !isPollingActive) {
        sendTimer->stop();
        // 仅移除模态弹框，替换为日志输出（不阻塞事件循环）
        writeCrashLog("【定时发送】连接已关闭/轮询未激活，定时发送已暂停");
        return;
    }

    if (newarrange.isEmpty()) {
        writeCrashLog("【定时发送】无配置数据，重置标识后等待下一轮");
        return;
    }

    QQueue<QByteArray> newSendQueue; // 每次轮询生成全新队列
    QSet<QString> newProcessedIds;

    for (const QStringList& rowData : newarrange) {
        const QVector<PollRowInfo> entries = parsePollConfigEntries(rowData);
        for (const PollRowInfo& info : entries) {
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
                writeCrashLog("【" + info.typePrefix + "类型数据加入队列】地址：" + hexAddr + "，数据：" + sendData.toHex().toUpper());
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
                writeCrashLog("【C类型（尘埃）数据加入队列】地址：" + hexAddr + "，数据：" + sendData.toHex().toUpper());
                newProcessedIds.insert(makePollDeviceId("C", info.modbusAddr));
            }
        } else if (info.typePrefix == "I") {
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
            const uint8_t checksumByte = 256 - (checksum % 256);
            sendData.append(static_cast<char>(checksumByte));

            {
                QMutexLocker locker(&queueMutex);
                newSendQueue.enqueue(sendData);
                writeCrashLog("【离子风机数据加入队列】地址：" + hexAddr + "，信道：" + QString::number(channel) + "，数据：" + sendData.toHex().toUpper());
            }
            newProcessedIds.insert(makePollDeviceId("I", info.modbusAddr, info.range.startChannel));
        }
        }
    }
    bool startSend = false;
        int newQueueSize = newSendQueue.size(); // 提前存队列大小
        {
            QMutexLocker locker(&queueMutex);
            sendQueue = newSendQueue;
            processedIds.unite(newProcessedIds);
            sendnum = 0;
            startSend = !sendQueue.isEmpty() && !overtime->isActive();
        }
        // 锁外写日志（关键修改）
        writeCrashLog("【轮询生成队列】大小：" + QString::number(newQueueSize) + "，是否启动发送：" + (startSend ? "是" : "否"));

        if (startSend) {
            sendNextData();
        }
    }



void MainWindow::writeCrashLog(const QString& logContent)
{
    static QMutex logMutex;
    QMutexLocker locker(&logMutex);

    QString logPath = QDir(QCoreApplication::applicationDirPath()).filePath("err.txt");
    QFile logFile(logPath);
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    // 时间戳直接拼接（不用arg）
    QString timeStamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString logLine = "[" + timeStamp + "] " + logContent + "\n";

    QTextStream out(&logFile);
    out << logLine;
    logFile.flush();
    logFile.close();
}
void MainWindow::cleanLogFile()
{
    // 先停止日志刷新定时器（避免清理时写入，之前优化日志时加的）
    if (this->findChild<QTimer*>("m_logFlushTimer")) {
        this->findChild<QTimer*>("m_logFlushTimer")->stop();
    }

    QMutexLocker locker(&logMutex); // 仅加锁1次
    QString logPath = QDir(QCoreApplication::applicationDirPath()).filePath("err.txt");
    QFile logFile(logPath);
    bool cleanSuccess = false;

    // 清空日志文件
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logFile.close();
        // 直接写入清理记录（不调用writeCrashLog，避免再次加锁）
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QString timeStamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
            QString logLine = "[" + timeStamp + "] === 日志文件已定期清理 ===\n";
            QTextStream out(&logFile);
            out << logLine;
            logFile.flush();
            logFile.close();
        }
        cleanSuccess = true;
    }
    locker.unlock(); // 提前解锁

    // 清空textBrowser内容
    ui->textBrowser->clear();
    QString timeStamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    ui->textBrowser->append(QString("[%1] === 显示内容已定期清理 ===").arg(timeStamp));

    // 锁外输出日志（用qDebug，避免调用writeCrashLog再次加锁）
    if (cleanSuccess) {
        qDebug() << "[日志清理] 成功清空err.txt和textBrowser（每5分钟定期清理）";
    } else {
        qDebug() << "[日志清理] 失败：无法打开err.txt，原因：" << logFile.errorString();
    }

    // 重启日志刷新定时器
    if (this->findChild<QTimer*>("m_logFlushTimer")) {
        this->findChild<QTimer*>("m_logFlushTimer")->start();
    }
}
void MainWindow::onSingleTestClicked()
{
    // 1. 校验串口是否打开
    if (!serial->isOpen()) {
        QMessageBox::warning(this, "警告", "请先打开串口再执行单次测试！");
        return;
    }

    // 2. 读取并校验输入参数
    bool addrOk;
    int decAddr = ui->lineEdit_loc->text().toInt(&addrOk);
    if (!addrOk || decAddr <= 0) {
        QMessageBox::critical(this, "错误", "地址输入无效！请输入正整数");
        return;
    }
    QString hexAddr = QString("%1").arg(decAddr, 4, 16, QChar('0')).toUpper();

    bool regOk;
    int decReg = ui->lineEdit_load->text().toInt(&regOk);
    if (!regOk || decReg < 0) {
        QMessageBox::critical(this, "错误", "寄存器输入无效！请输入非负整数");
        return;
    }
    QString hexReg = QString("%1").arg(decReg, 4, 16, QChar('0')).toUpper();

    // 3. 设备类型（新增尘埃选项）
    QString type = ui->comboBox_what->currentText();
    QString funcCode;
    QString regCount = "0001"; // 默认读取1个寄存器
    if (type == "腕带") funcCode = "1011";
    else if (type == "设备") funcCode = "1021";
    else if (type == "台垫") funcCode = "1014";
    else if (type == "尘埃") { // 新增尘埃类型
        funcCode = "1030";
        hexReg = "0002"; // 固定起始寄存器0002
        regCount = "0016"; // 固定读取16个寄存器
    } else if (type == "离子风机") { // 新增离子风机类型
        funcCode = "1031";
        hexReg = "0001"; // 固定起始寄存器0001
        regCount = "0001"; // 固定读取1个寄存器
    } else {
        QMessageBox::critical(this, "错误", "请选择有效设备类型！");
        return;
    }

    // 4. 暂停轮询
    isPollingActive = sendTimer->isActive();
    if (isPollingActive) {
        sendTimer->stop();
        writeCrashLog("[单次测试] 暂停轮询（原轮询状态：运行中）");
    }

    // 5. 清空发送队列
    QMutexLocker locker(&queueMutex);
    sendQueue.clear();
    sendnum = 0;
    currentExpectedAddrFunc.clear();
    isSingleTest = true;
    locker.unlock();

    // 6. 构造测试数据帧
    QString modbusStr = hexAddr + funcCode + hexReg + regCount;
    QByteArray sendData;
    bool dataOk = true;
    for (int i = 0; i < modbusStr.length() && dataOk; i += 2) {
        QString byteStr = modbusStr.mid(i, 2);
        uint8_t byte = byteStr.toUInt(&dataOk, 16);
        if (!dataOk) {
            QMessageBox::critical(this, "错误", "数据构造失败！无效的十六进制字符");
            isSingleTest = false;
            return;
        }
        sendData.append(static_cast<char>(byte));
    }
    uint16_t crc = calcrc(sendData);
    sendData.append(static_cast<char>(crc & 0xFF));
    sendData.append(static_cast<char>((crc >> 8) & 0xFF));

    // 7. 加入队列并发送
    locker.relock();
    sendQueue.enqueue(sendData);
    locker.unlock();

    if (!overtime->isActive()) {
        sendnum = 0;
        sendNextData();
    }
}
void MainWindow::clearDeviceSelection()
{
    for (DeviceWidget* device : m_selectedDevices) {
        if (device) {
            device->setSelected(false);
        }
    }
    m_selectedDevices.clear();
}

void MainWindow::wireDeviceSelectionSignals(DeviceWidget* device)
{
    if (!device) {
        return;
    }
    connect(device, &DeviceWidget::deviceClicked, this, &MainWindow::onDeviceClicked);
    connect(device, &DeviceWidget::deviceDoubleClicked, this, &MainWindow::onDeviceDoubleClicked);
}

void MainWindow::onDeviceClicked(DeviceWidget* device, bool ctrlPressed)
{
    if (!device) {
        return;
    }

    if (ctrlPressed) {
        if (m_selectedDevices.contains(device)) {
            m_selectedDevices.remove(device);
            device->setSelected(false);
        } else {
            m_selectedDevices.insert(device);
            device->setSelected(true);
        }
    } else {
        clearDeviceSelection();
        m_selectedDevices.insert(device);
        device->setSelected(true);
    }

    QStringList ids;
    for (DeviceWidget* selected : m_selectedDevices) {
        if (selected) {
            ids.append(selected->getId());
        }
    }
    ids.sort();
    writeCrashLog(QString("[Device] 选中设备：%1").arg(ids.isEmpty() ? QStringLiteral("(无)") : ids.join(",")));
    if (ui->textBrowser_2) {
        ui->textBrowser_2->append(ids.isEmpty()
            ? QStringLiteral("已取消选中")
            : QStringLiteral("已选中设备：%1").arg(ids.join(", ")));
    }
}

void MainWindow::onDeviceDoubleClicked(DeviceWidget* device)
{
    // 双击保持为单选，便于沿用旧操作习惯
    onDeviceClicked(device, false);
}

bool MainWindow::deleteSelectedMapDevices(bool showSuccessBox)
{
    if (m_currentImageIndex == -1 || m_currentImageIndex >= m_imageDeviceList.size()) {
        QMessageBox::warning(this, "警告", "请先选择图片！");
        return false;
    }

    if (m_selectedDevices.isEmpty()) {
        QMessageBox::information(this, "提示",
            "请先单击选择要删除的设备！\n按住 Ctrl 再单击可多选。");
        writeCrashLog("[Device] 删除设备失败：未选中设备");
        return false;
    }

    auto& devices = m_imageDeviceList[m_currentImageIndex].devices;
    QList<DeviceWidget*> toDelete;
    for (DeviceWidget* device : m_selectedDevices) {
        if (device && devices.contains(device)) {
            toDelete.append(device);
        }
    }

    if (toDelete.isEmpty()) {
        clearDeviceSelection();
        QMessageBox::warning(this, "警告", "当前图片没有可删除的选中设备！");
        return false;
    }

    QStringList ids;
    for (DeviceWidget* device : toDelete) {
        ids.append(device->getId());
    }
    ids.sort();

    const QString confirmText = (toDelete.size() == 1)
        ? QStringLiteral("确定删除设备「%1」？").arg(ids.first())
        : QStringLiteral("确定删除已选的 %1 个设备？\n%2").arg(toDelete.size()).arg(ids.join(", "));
    if (QMessageBox::question(this, "确认删除", confirmText,
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        writeCrashLog(QString("[Device] 取消删除设备：%1").arg(ids.join(",")));
        return false;
    }

    int removedCount = 0;
    for (DeviceWidget* device : toDelete) {
        if (!devices.removeOne(device)) {
            continue;
        }

        const QString deviceType = device->getType();
        const QString deviceId = device->getId();
        const int idNumber = deviceId.mid(1).toInt();
        m_deletedDeviceIds[deviceType].insert(idNumber);
        m_selectedDevices.remove(device);

        disconnect(device, &DeviceWidget::deviceClicked, this, &MainWindow::onDeviceClicked);
        disconnect(device, &DeviceWidget::deviceDoubleClicked, this, &MainWindow::onDeviceDoubleClicked);

        device->hide();
        device->deleteLater();
        ++removedCount;

        writeCrashLog(QString("[Device] 删除设备成功：%1，类型：%2，ID编号：%3")
                     .arg(deviceId).arg(deviceType).arg(idNumber));
    }

    clearDeviceSelection();

    if (removedCount <= 0) {
        QMessageBox::warning(this, "警告", "删除设备失败！");
        writeCrashLog("[Device] 删除设备失败：未能移除选中设备");
        return false;
    }

    if (ui->textBrowser_2) {
        ui->textBrowser_2->append(QStringLiteral("已删除 %1 个设备：%2")
                                  .arg(removedCount).arg(ids.join(", ")));
    }
    if (showSuccessBox) {
        QMessageBox::information(this, "成功",
            QStringLiteral("已删除 %1 个设备。").arg(removedCount));
    }
    return true;
}

void MainWindow::updateIonizerStatus(const QString& deviceId, bool isOnline)
{
    if (m_currentImageIndex == -1 || m_currentImageIndex >= m_imageDeviceList.size()) {
        return;
    }

    ImageDeviceData& currentData = m_imageDeviceList[m_currentImageIndex];
    for (DeviceWidget* dev : currentData.devices) {
        if (!dev) continue;
        if (dev->getId() == deviceId) {
            dev->setOnline(isOnline);
            writeCrashLog(QString("[Ionizer] 更新离子风机状态：%1 -> %2")
                         .arg(deviceId).arg(isOnline ? "在线" : "待机"));
            return;
        }
    }
}

void MainWindow::loadPollConfig()
{
    QString configPath = qApp->applicationDirPath() + "/poll_config.txt";
    QString logMsg = QString("[PollConfig] 开始加载轮询配置，路径：%1").arg(configPath);
    ui->textBrowser->append(logMsg);
    ui->textBrowser->moveCursor(QTextCursor::End);
    writeCrashLog(logMsg);

    QFile file(configPath);
    if (!file.exists()) {
        logMsg = "[PollConfig] 配置文件不存在：" + configPath;
        ui->textBrowser->append(logMsg);
        ui->textBrowser->moveCursor(QTextCursor::End);
        writeCrashLog(logMsg);
        return;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        logMsg = "[PollConfig] 无法打开配置文件：" + file.errorString();
        ui->textBrowser->append(logMsg);
        ui->textBrowser->moveCursor(QTextCursor::End);
        writeCrashLog(logMsg);
        return;
    }

    QVector<QStringList> tableData;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        QStringList parts = line.split(",");
        tableData.append(parts);
        logMsg = QString("[PollConfig] 读取行：%1").arg(line);
        ui->textBrowser->append(logMsg);
        ui->textBrowser->moveCursor(QTextCursor::End);
        writeCrashLog(logMsg);
    }
    file.close();

    if (tableData.isEmpty()) {
        logMsg = "[PollConfig] 配置文件为空";
        ui->textBrowser->append(logMsg);
        ui->textBrowser->moveCursor(QTextCursor::End);
        writeCrashLog(logMsg);
        return;
    }

    logMsg = QString("[PollConfig] 共读取 %1 条配置").arg(tableData.size());
    ui->textBrowser->append(logMsg);
    ui->textBrowser->moveCursor(QTextCursor::End);
    writeCrashLog(logMsg);

    syncPollConfigToWorkers(tableData);

    logMsg = QString("[PollConfig] 已加载 %1 条配置到线程").arg(tableData.size());
    ui->textBrowser->append(logMsg);
    ui->textBrowser->moveCursor(QTextCursor::End);
    writeCrashLog(logMsg);
}

void MainWindow::syncPollConfigToWorkers(const QVector<QStringList>& tableData)
{
    m_fullPollConfig = tableData;

    if (tableData.isEmpty()) {
        m_worker1->setConfigData({});
        m_worker2->setConfigData({});
        processModbusData(tableData);
        DBManager::instance()->syncPollConfigFromRows(tableData);
        return;
    }

    if (m_singleLinkPolling) {
        m_worker1->setConfigData(tableData);
        m_worker1->setTaskType(TaskType::ALL_TYPE);
        m_worker2->setConfigData({});
        processModbusData(tableData);
        writeCrashLog(QStringLiteral("[PollConfig] 单链路：线程一加载全部 %1 条配置").arg(tableData.size()));
    } else {
        QVector<QStringList> worker1Config;
        QVector<QStringList> worker2Config;

        for (const QStringList& row : tableData) {
            const QVector<PollRowInfo> entries = parsePollConfigEntries(row);
            if (entries.isEmpty()) {
                continue;
            }

            bool hasC = false;
            bool hasWtei = false;
            for (const PollRowInfo& info : entries) {
                if (info.typePrefix == "C") {
                    hasC = true;
                } else {
                    hasWtei = true;
                }
            }
            if (hasC) {
                worker1Config.append(row);
            }
            if (hasWtei) {
                worker2Config.append(row);
            }
        }

        m_worker1->setConfigData(worker1Config);
        m_worker1->setTaskType(TaskType::C_TYPE);
        m_worker2->setConfigData(worker2Config);
        processModbusData(tableData);
        writeCrashLog(QStringLiteral("[PollConfig] 双链路：线程一 %1 条，线程二 %2 条")
                          .arg(worker1Config.size()).arg(worker2Config.size()));
    }

    DBManager::instance()->syncPollConfigFromRows(tableData);
}

void MainWindow::onPushButtonDeleteClicked()
{
    if (ui->textBrowser_2) {
        ui->textBrowser_2->clear();
    }
    deleteSelectedMapDevices(false);
}

QVector<QStringList> MainWindow::getPollConfigRows()
{
    {
        QMutexLocker locker(&m_newarrangeMutex);
        if (!newarrange.isEmpty()) {
            return newarrange;
        }
    }

    QVector<QStringList> tableData;
    const QString configPath = qApp->applicationDirPath() + "/poll_config.txt";
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return tableData;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        tableData.append(line.split(","));
    }
    return tableData;
}

QSet<QString> MainWindow::collectPlacedDeviceIds() const
{
    QSet<QString> placedIds;
    for (const ImageDeviceData& imgData : m_imageDeviceList) {
        for (DeviceWidget* dev : imgData.devices) {
            if (dev) {
                placedIds.insert(dev->getId());
            }
        }
    }
    return placedIds;
}

QList<QPoint> MainWindow::collectCurrentMapDevicePositions() const
{
    QList<QPoint> points;
    if (m_currentImageIndex < 0 || m_currentImageIndex >= m_imageDeviceList.size()) {
        return points;
    }

    for (DeviceWidget* dev : m_imageDeviceList[m_currentImageIndex].devices) {
        if (dev) {
            points.append(dev->getPos());
        }
    }
    return points;
}

QPoint MainWindow::computeDevicePlacement(const QList<QPoint>& occupied, int slotIndex) const
{
    if (m_currentImageIndex < 0 || m_currentImageIndex >= m_imageDeviceList.size()) {
        return QPoint();
    }

    const ImageDeviceData& imgData = m_imageDeviceList[m_currentImageIndex];
    QPixmap pixmap(imgData.imagePath);
    QSize imageSize = pixmap.size();
    if (imageSize.width() <= 0 || imageSize.height() <= 0) {
        imageSize = QSize(1280, 720);
    }

    const QPoint labelPos = ui->labelImage->pos();
    const int spacing = 52;
    const int margin = 56;
    const int minDistSq = 42 * 42;

    auto overlaps = [&](const QPoint& candidate) {
        for (const QPoint& existing : occupied) {
            const int dx = candidate.x() - existing.x();
            const int dy = candidate.y() - existing.y();
            if (dx * dx + dy * dy < minDistSq) {
                return true;
            }
        }
        return false;
    };

    const int cols = qMax(1, (imageSize.width() - margin * 2) / spacing);
    for (int attempt = 0; attempt < 500; ++attempt) {
        const int col = attempt % cols;
        const int row = attempt / cols;
        const QPoint candidate(labelPos.x() + margin + col * spacing,
                               labelPos.y() + margin + row * spacing);
        if (candidate.x() > labelPos.x() + imageSize.width() - margin) {
            continue;
        }
        if (candidate.y() > labelPos.y() + imageSize.height() - margin) {
            continue;
        }
        if (!overlaps(candidate)) {
            return candidate;
        }
    }

    const QPoint center = labelPos + QPoint(imageSize.width() / 2, imageSize.height() / 2);
    return center + QPoint((slotIndex % 6) * spacing, (slotIndex / 6) * spacing);
}

DeviceWidget* MainWindow::createMapDevice(const QString& type, const QString& devId, const QPoint& pos)
{
    QWidget* parentWidget = ui->labelImage->parentWidget();
    if (!parentWidget) {
        return nullptr;
    }

    DeviceWidget* dev = new DeviceWidget(type, devId, parentWidget);
    if (!dev) {
        return nullptr;
    }

    dev->setPos(pos);
    dev->show();
    wireDeviceSelectionSignals(dev);
    m_imageDeviceList[m_currentImageIndex].devices.append(dev);
    return dev;
}

QVector<PollDeviceRef> MainWindow::getUnplacedPollDevices(const QString& typeFilter)
{
    const QVector<PollDeviceRef> allDevices = expandPollConfigToDevices(getPollConfigRows());
    const QSet<QString> placedIds = collectPlacedDeviceIds();
    QVector<PollDeviceRef> unplaced;

    for (const PollDeviceRef& ref : allDevices) {
        if (!typeFilter.isEmpty() && ref.typePrefix != typeFilter) {
            continue;
        }
        if (!placedIds.contains(ref.deviceId)) {
            unplaced.append(ref);
        }
    }
    return unplaced;
}

void MainWindow::onBtnAddDeviceClicked()
{
    if (m_currentImageIndex == -1) {
        QMessageBox::warning(this, "警告", "请先选择背景图片！");
        writeCrashLog("[Device] 新增设备失败：未选择背景图片");
        return;
    }

    const QStringList typeNames = {"腕带(W)", "台垫(T)", "设备(E)", "尘埃(C)", "离子风机(I)"};
    bool ok = false;
    const QString typeName = QInputDialog::getItem(this, "选择设备类型", "请选择设备类型：", typeNames, 0, false, &ok);
    if (!ok || typeName.isEmpty()) {
        writeCrashLog("[Device] 取消选择设备类型");
        return;
    }

    QString type;
    if (typeName.startsWith("腕带")) type = "W";
    else if (typeName.startsWith("台垫")) type = "T";
    else if (typeName.startsWith("设备")) type = "E";
    else if (typeName.startsWith("尘埃")) type = "C";
    else if (typeName.startsWith("离子风机")) type = "I";

    const QVector<PollDeviceRef> candidates = getUnplacedPollDevices(type);
    if (candidates.isEmpty()) {
        QMessageBox::warning(this, "提示",
            QString("轮询配置中没有可添加的%1类型设备，或该类型设备已全部添加到地图中。\n请先在轮询设置中配置对应项。")
                .arg(typeName));
        writeCrashLog(QString("[Device] 新增设备失败：无可用轮询设备，类型=%1").arg(type));
        return;
    }

    const PollDeviceRef& nextDevice = candidates.first();
    QList<QPoint> occupied = collectCurrentMapDevicePositions();
    const QPoint center = computeDevicePlacement(occupied, occupied.size());
    occupied.append(center);

    DeviceWidget* dev = createMapDevice(nextDevice.typePrefix, nextDevice.deviceId, center);
    if (!dev) {
        QMessageBox::critical(this, "错误", "设备创建失败！");
        writeCrashLog(QString("[Device] 新增设备失败：类型=%1，ID=%2").arg(type).arg(nextDevice.deviceId));
        return;
    }

    writeCrashLog(QString("[Device] 新增设备成功：类型=%1，ID=%2，像素坐标=(%3,%4)")
                 .arg(nextDevice.typePrefix).arg(nextDevice.deviceId).arg(center.x()).arg(center.y()));
}

void MainWindow::onBtnAutoGenerateClicked()
{
    if (m_currentImageIndex == -1) {
        QMessageBox::warning(this, "警告", "请先选择背景图片！");
        writeCrashLog("[Device] 自动生成失败：未选择背景图片");
        return;
    }

    const QVector<PollDeviceRef> candidates = getUnplacedPollDevices();
    if (candidates.isEmpty()) {
        QMessageBox::information(this, "提示", "轮询配置中的设备点已全部添加到地图中，没有可自动生成的点位。");
        writeCrashLog("[Device] 自动生成：无未添加的轮询设备");
        return;
    }

    QList<QPoint> occupied = collectCurrentMapDevicePositions();
    int addedCount = 0;

    for (const PollDeviceRef& ref : candidates) {
        const QPoint pos = computeDevicePlacement(occupied, occupied.size());
        if (createMapDevice(ref.typePrefix, ref.deviceId, pos)) {
            occupied.append(pos);
            addedCount++;
            writeCrashLog(QString("[Device] 自动生成：%1，坐标=(%2,%3)")
                         .arg(ref.deviceId).arg(pos.x()).arg(pos.y()));
        }
    }

    QMessageBox::information(this, "成功",
        QString("已自动添加 %1 个设备点到当前地图。").arg(addedCount));
    writeCrashLog(QString("[Device] 自动生成完成，共添加 %1 个设备").arg(addedCount));
}
void MainWindow::addImageToHistory(const QString& imagePath)
{
    // 检查图片路径有效性
    if (imagePath.isEmpty() || !QFile::exists(imagePath)) {
        QMessageBox::warning(this, "警告", "图片路径无效或文件不存在！");
        writeCrashLog(QString("[Image] 图片路径无效：%1").arg(imagePath));
        return;
    }

    // 查找图片是否已在列表中
    int existingIndex = findImageIndex(imagePath);
    if (existingIndex != -1) {
        switchToImage(existingIndex);
        if (ui->cmbImageList) {
            ui->cmbImageList->setCurrentIndex(existingIndex + 1);
        }
        writeCrashLog(QString("[Image] 图片已存在，切换到该图片：%1").arg(QFileInfo(imagePath).fileName()));
        return;
    }

    // 新增图片数据
    ImageDeviceData newData;
    newData.imagePath = imagePath;
    newData.theme = "静电管理在线监控系统 ESD-1000.V1.0"; // 默认主题
    m_imageDeviceList.append(newData);

    // 更新下拉列表
    if (ui->cmbImageList) {
        QString fileName = QFileInfo(imagePath).fileName();
        ui->cmbImageList->addItem(fileName);
        int newIndex = m_imageDeviceList.size() - 1;
        switchToImage(newIndex);
        ui->cmbImageList->setCurrentIndex(newIndex + 1);
    }

    writeCrashLog(QString("[Image] 新增图片成功：路径=%1，列表总数=%2")
                 .arg(imagePath).arg(m_imageDeviceList.size()));

    updateCycleButtonText();
}
void MainWindow::switchToImage(int index)
{
    if (index < 0 || index >= m_imageDeviceList.size()) {
        writeCrashLog(QString("[Image] 切换图片失败：索引无效，index=%1").arg(index));
        return;
    }

    clearDeviceSelection();
    clearCurrentDevices();
    m_currentImageIndex = index;
    const ImageDeviceData& targetData = m_imageDeviceList[index];

    // 加载图片并更新前端显示
    QPixmap pixmap(targetData.imagePath);
    if (pixmap.isNull()) {
        QMessageBox::warning(this, "警告", "图片加载失败！");
        writeCrashLog(QString("[Image] 切换图片失败：图片加载失败，路径=%1").arg(targetData.imagePath));
        return;
    }

    ui->labelImage->setPixmap(pixmap);
    ui->labelImage->setFixedSize(pixmap.size());  // 前端显示原始尺寸

    // 关键：自动填充当前图片尺寸到前端输入框，方便用户直接修改
    if (ui->leImageWidth && ui->leImageHeight) {
        ui->leImageWidth->setText(QString::number(pixmap.width()));
        ui->leImageHeight->setText(QString::number(pixmap.height()));
    }

    // 更新主题输入框
    if (ui->leTheme) {
        ui->leTheme->setText(targetData.theme.isEmpty() ? "静电管理在线监控系统 ESD-1000.V1.0" : targetData.theme);
    }

    // 显示设备（前端设备同步显示）
    QWidget* parentWidget = ui->labelImage->parentWidget();
    for (DeviceWidget* dev : targetData.devices) {
        if (!dev) continue;
        dev->setParent(parentWidget);
        dev->show();
    }

    writeCrashLog(QString("[Image] 切换图片成功：%1，前端显示尺寸=%2x%3，设备数=%4")
                 .arg(QFileInfo(targetData.imagePath).fileName())
                 .arg(pixmap.width()).arg(pixmap.height())
                 .arg(targetData.devices.size()));

    // 立即通知浏览器页面更新主题，确保与图片切换同步
    QString themeText = targetData.theme.isEmpty() ? "静电管理在线监控系统 ESD-1000.V1.0" : targetData.theme;
    if (requestHandler) {
        requestHandler->notifyThemeChange(themeText);
        writeCrashLog(QString("[Theme] 主题同步更新：%1").arg(themeText));
    }

    updateMapPageToolbarsPosition();
}
// 查找图片在列表中的索引（根据路径）
int MainWindow::findImageIndex(const QString& imagePath) const
{
    const QString absWanted = QFileInfo(imagePath).absoluteFilePath();
    for (int i = 0; i < m_imageDeviceList.size(); ++i) {
        if (m_imageDeviceList[i].imagePath == imagePath
            || QFileInfo(m_imageDeviceList[i].imagePath).absoluteFilePath() == absWanted) {
            return i;
        }
    }
    return -1;
}
// 设置图片主题
void MainWindow::setImageTheme(const QString& imagePath, const QString& theme)
{
    int index = findImageIndex(imagePath);
    if (index == -1) {
        writeCrashLog(QString("[Theme] 设置主题失败：图片不存在，路径=%1").arg(imagePath));
        return;
    }

    m_imageDeviceList[index].theme = theme;
    writeCrashLog(QString("[Theme] 主题设置成功：图片=%1，主题=%2")
                 .arg(QFileInfo(imagePath).fileName()).arg(theme));

    // 如果是当前图片，立即通知浏览器更新
    if (index == m_currentImageIndex && requestHandler) {
        requestHandler->notifyThemeChange(theme.isEmpty() ? "静电管理在线监控系统 ESD-1000.V1.0" : theme);
    }

}
// 隐藏地图上所有图片的设备点（切换图片时只显示当前图片的设备）
void MainWindow::clearCurrentDevices()
{
    for (const ImageDeviceData& imgData : m_imageDeviceList) {
        for (DeviceWidget* dev : imgData.devices) {
            if (dev) {
                dev->hide();
            }
        }
    }
}

void MainWindow::updateMapPageToolbarsPosition()
{
    if (!ui->labelImage || !ui->layoutWidget_2 || !ui->layoutWidget3) {
        return;
    }

    const int margin = 10;
    const int rowGap = 6;
    const int rowHeight = 32;
    const int pageWidth = ui->page_4 ? ui->page_4->width() : 1850;
    const int toolbarWidth = qMax(800, pageWidth - margin * 2);
    const int imageBottom = ui->labelImage->y() + ui->labelImage->height();
    const int themeRowY = imageBottom + margin;
    const int deviceRowY = themeRowY + rowHeight + rowGap;

    ui->layoutWidget_2->setGeometry(margin, themeRowY, toolbarWidth, rowHeight);
    ui->layoutWidget3->setGeometry(margin, deviceRowY, toolbarWidth, rowHeight);
    ui->layoutWidget_2->raise();
    ui->layoutWidget3->raise();
}

// 新增：图片列表切换槽函数
void MainWindow::onImageListCurrentIndexChanged(int index)
{
    if (index == 0) {
        // 选择了默认提示项，清空显示
        clearDeviceSelection();
        clearCurrentDevices();
        m_currentImageIndex = -1;
        ui->labelImage->clear();
        return;
    }

    // 索引-1（因为默认提示项占了第0位）
    switchToImage(index - 1);
}
void MainWindow::onBtnSelectImageClicked()
{
    QString imagePath = QFileDialog::getOpenFileName(
        this,
        "选择图片",
        QCoreApplication::applicationDirPath(),
        "图片文件 (*.jpg *.jpeg *.png *.bmp *.gif)"
    );

    if (imagePath.isEmpty()) {
        writeCrashLog("【图片选择】用户取消选择");
        return;
    }

    QPixmap pixmap(imagePath);
    if (pixmap.isNull()) {
        QMessageBox::warning(this, "提示", "图片加载失败，请选择有效图片文件！");
        writeCrashLog(QString("【图片加载失败】路径：%1").arg(imagePath));
        return;
    }

    // 新增：添加图片到历史列表（自动切换显示）
    addImageToHistory(imagePath);

    writeCrashLog(QString("【图片选择成功】路径：%1，尺寸：%2x%3").arg(imagePath).arg(pixmap.width()).arg(pixmap.height()));
}
void MainWindow::onBtnApplySizeClicked()
{
    if (m_currentImageIndex == -1) {
        QMessageBox::warning(this, "警告", "请先选择背景图片！");
        writeCrashLog("[Image] 调整尺寸失败：未选择背景图片");
        return;
    }

    const ImageDeviceData& currentImgData = m_imageDeviceList[m_currentImageIndex];
    QPixmap originalPixmap(currentImgData.imagePath);
    if (originalPixmap.isNull()) {
        QMessageBox::warning(this, "警告", "图片加载失败！");
        writeCrashLog(QString("[Image] 调整尺寸失败：图片加载失败，路径=%1").arg(currentImgData.imagePath));
        return;
    }

    // 读取前端输入框的尺寸（和原来一致）
    QString widthStr = ui->leImageWidth->text().trimmed();
    QString heightStr = ui->leImageHeight->text().trimmed();

    if (widthStr.isEmpty() || heightStr.isEmpty()) {
        QMessageBox::warning(this, "警告", "宽度和高度不能为空！");
        writeCrashLog("[Image] 调整尺寸失败：宽度或高度为空");
        return;
    }

    bool okWidth, okHeight;
    int targetWidth = widthStr.toInt(&okWidth);
    int targetHeight = heightStr.toInt(&okHeight);

    if (!okWidth || !okHeight || targetWidth <= 0 || targetHeight <= 0) {
        QMessageBox::warning(this, "警告", "请输入有效的正整数尺寸！");
        writeCrashLog(QString("[Image] 调整尺寸失败：无效尺寸（宽=%1，高=%2）").arg(targetWidth).arg(targetHeight));
        return;
    }

    const int MAX_WIDTH = 4000;
    const int MAX_HEIGHT = 3000;
    if (targetWidth > MAX_WIDTH || targetHeight > MAX_HEIGHT) {
        QMessageBox::warning(this, "警告", QString("尺寸过大！最大支持 %1x%2").arg(MAX_WIDTH).arg(MAX_HEIGHT));
        writeCrashLog(QString("[Image] 调整尺寸失败：尺寸超过最大值（宽=%1，高=%2）").arg(targetWidth).arg(targetHeight));
        return;
    }

    // 恢复原有布局和尺寸策略（避免约束问题）
    QWidget* parentWidget = ui->labelImage->parentWidget();
    if (parentWidget) {
        QLayout* parentLayout = parentWidget->layout();
        if (parentLayout) {
            parentLayout->removeWidget(ui->labelImage);
        }
    }
    ui->labelImage->setMinimumSize(QSize());
    ui->labelImage->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
    ui->labelImage->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    ui->labelImage->setScaledContents(true);

    // 按目标尺寸缩放图片（和原来一致）
    QPixmap scaledPixmap = originalPixmap.scaled(
        targetWidth, targetHeight,
        Qt::IgnoreAspectRatio,
        Qt::SmoothTransformation
    );

    // 恢复原有缩放比例计算，同步设备位置
    float scaleX = (float)targetWidth / originalPixmap.width();
    float scaleY = (float)targetHeight / originalPixmap.height();
    for (DeviceWidget* dev : currentImgData.devices) {
        if (!dev) continue;
        QPoint oldPos = dev->getPos();
        // 按比例缩放设备坐标（核心：恢复原有正确的缩放逻辑）
        QPoint newPos = QPoint(qRound(oldPos.x() * scaleX), qRound(oldPos.y() * scaleY));
        dev->setPos(newPos);
    }

    // 应用图片并固定尺寸（和原来一致）
    ui->labelImage->clear();
    ui->labelImage->setPixmap(scaledPixmap);
    ui->labelImage->setFixedSize(targetWidth, targetHeight);
    ui->labelImage->update();
    ui->labelImage->repaint();

    updateMapPageToolbarsPosition();

    // 日志恢复为绝对像素相关
    writeCrashLog(QString("[Image] 前端尺寸应用成功：原尺寸=(%1x%2)，目标尺寸=(%3x%4)，缩放比例=(%5,%6)")
                 .arg(originalPixmap.width()).arg(originalPixmap.height())
                 .arg(targetWidth).arg(targetHeight).arg(scaleX).arg(scaleY));
    QMessageBox::information(this, "成功", QString("前端图片尺寸已强制设置为 %1x%2 像素！").arg(targetWidth).arg(targetHeight));
}

void MainWindow::onChkSeparateEnvEsdChanged(int state)
{
    m_separateEnvEsd = (state == Qt::Checked);
    if (requestHandler) {
        requestHandler->notifyDisplayModeChange(m_separateEnvEsd);
    }
    saveToConfig();
    writeCrashLog(QStringLiteral("[Display] 分离环境与ESD：%1").arg(m_separateEnvEsd ? QStringLiteral("开启") : QStringLiteral("关闭")));
}

void MainWindow::onBtnApplyThemeClicked()
{
    if (m_currentImageIndex == -1) {
        QMessageBox::warning(this, "警告", "请先选择背景图片！");
        writeCrashLog("[Theme] 应用主题失败：未选择背景图片");
        return;
    }

    QString newTheme = ui->leTheme->text().trimmed();
    if (newTheme.isEmpty()) {
        QMessageBox::warning(this, "警告", "主题名称不能为空！");
        writeCrashLog("[Theme] 应用主题失败：主题名称为空");
        return;
    }

    // 更新当前图片的主题
    m_imageDeviceList[m_currentImageIndex].theme = newTheme;

    // 立即通知请求处理器更新主题，确保实时同步
    if (requestHandler) {
        requestHandler->notifyThemeChange(newTheme);
        writeCrashLog(QString("[Theme] 主题实时更新：%1").arg(newTheme));
    }

    // 显示成功消息
    QMessageBox::information(this, "成功", QString("主题已更新为：%1").arg(newTheme));
    writeCrashLog(QString("[Theme] 主题应用成功：主题=%1，图片索引=%2").arg(newTheme).arg(m_currentImageIndex));
}

void MainWindow::onBtnSaveDevicePositionsClicked()
{
    saveToConfig();
    const bool backupOk = backupDevices();
    if (backupOk) {
        QMessageBox::information(this, "成功", "设备点位已保存到配置文件。");
        writeCrashLog("[Config] 用户手动保存设备点位成功");
    } else {
        QMessageBox::warning(this, "警告", "设备点位保存失败，请检查程序目录写入权限。");
        writeCrashLog("[Config] 用户手动保存设备点位失败");
    }
}

void MainWindow::onBtnApplyBgSwitchTimeClicked()
{
    QString timeStr = ui->leBgSwitchTime->text().trimmed();
    bool ok;
    int seconds = timeStr.toInt(&ok);

    if (!ok || seconds <= 0) {
        QMessageBox::warning(this, "警告", "请输入有效的正整数时间！");
        writeCrashLog("[Background] 应用切换时间失败：无效的时间值");
        return;
    }

    int milliseconds = seconds * 1000;
    m_imageCycleTimer->setInterval(milliseconds);

    writeCrashLog(QString("[Background] 背景图切换时间已更新：%1秒 (%2毫秒)").arg(seconds).arg(milliseconds));
    QMessageBox::information(this, "成功", QString("背景图切换时间已更新为：%1秒").arg(seconds));
}

void MainWindow::onBtnDeleteClicked()
{
    qDebug() << "===== 删除按钮被点击了！=====";
    if (m_currentImageIndex == -1 || m_currentImageIndex >= m_imageDeviceList.size()) {
        QMessageBox::warning(this, "警告", "请先选择要删除的图片！");
        writeCrashLog("[Image] 删除图片失败：未选择图片");
        return;
    }

    const ImageDeviceData& currentImgData = m_imageDeviceList[m_currentImageIndex];
    QString fileName = QFileInfo(currentImgData.imagePath).fileName();
    int ret = QMessageBox::question(
        this,
        "确认删除",
        QString("是否要删除图片「%1」及关联的所有设备？\n此操作不可撤销！").arg(fileName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    if (ret != QMessageBox::Yes) {
        writeCrashLog(QString("[Image] 取消删除图片：%1").arg(fileName));
        return;
    }

    // 清理设备
    clearDeviceSelection();
    const ImageDeviceData& targetData = m_imageDeviceList[m_currentImageIndex];
    for (DeviceWidget* dev : targetData.devices) {
        if (dev) {
            dev->hide();
            delete dev;
        }
    }

    // 移除图片数据
    m_imageDeviceList.removeAt(m_currentImageIndex);
    pruneDisplayScreensForMissingImages();

    // 更新下拉列表
    if (ui->cmbImageList) {
        ui->cmbImageList->removeItem(m_currentImageIndex + 1);
    }

    // 重置UI
    ui->labelImage->clear();
    ui->labelImage->setFixedSize(0, 0);
    m_currentImageIndex = -1;

    // 重置设备ID计数器（新增C类型）
    m_devIdCounters["W"] = 1;
    m_devIdCounters["T"] = 1;
    m_devIdCounters["E"] = 1;
    m_devIdCounters["C"] = 1;

    writeCrashLog(QString("[Image] 删除图片成功：%1，剩余图片数：%2").arg(fileName).arg(m_imageDeviceList.size()));
    QMessageBox::information(this, "成功", QString("图片「%1」及关联设备已删除！").arg(fileName));
    updateCycleButtonText();
        // 如果删除最后一张图，停止定时器
        if (m_imageDeviceList.isEmpty() && m_imageCycleTimer->isActive()) {
            m_imageCycleTimer->stop();
            updateCycleButtonText();
        }
}
// 删除选中的设备（按钮名：btnDelete_2）
void MainWindow::on_btnDelete_2_clicked() {
    deleteSelectedMapDevices(true);
}
bool MainWindow::backupDevices() {
    QJsonObject root;
    QJsonArray devArray;
    QMap<QString, bool> uniqueKeyMap; // 新增：去重键（设备ID+图片路径）

    for (const auto& imgData : m_imageDeviceList) {
        for (DeviceWidget* dev : imgData.devices) {
            if (!dev) continue;
            // 新增：生成唯一键，避免同一设备重复备份
            QString uniqueKey = dev->getId() + "_" + imgData.imagePath;
            if (uniqueKeyMap.contains(uniqueKey)) continue;
            uniqueKeyMap[uniqueKey] = true;

            QJsonObject devObj;
            devObj["id"] = dev->getId();
            devObj["type"] = dev->getType();
            devObj["x"] = dev->getPos().x();
            devObj["y"] = dev->getPos().y();
            devObj["image_path"] = imgData.imagePath;
            devArray.append(devObj);
        }
    }

    root["devices"] = devArray;
    root["backup_time"] = QDateTime::currentDateTime().toString();

    QFile file(QCoreApplication::applicationDirPath() + "/device_backup.json");
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool MainWindow::restoreDevices() {
    QString path = QCoreApplication::applicationDirPath() + "/device_backup.json";
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        writeCrashLog("[恢复设备] 备份文件不存在或无法打开：" + path);
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        writeCrashLog("[恢复设备] 备份文件格式错误（非JSON对象）");
        return false;
    }

    QJsonObject rootObj = doc.object();
    QJsonArray devArray = rootObj["devices"].toArray();
    if (devArray.isEmpty()) {
        writeCrashLog("[恢复设备] 备份文件中无设备数据");
        return false;
    }

    // 用于记录已存在的图片路径和设备ID（避免重复）
    QMap<QString, int> imgPathToIndex; // 图片路径 → 列表索引
    QSet<QString> existingDeviceIds;   // 已存在的设备唯一标识（ID+图片路径）

    // 先映射已有图片路径，避免重复创建图片数据
    for (int i = 0; i < m_imageDeviceList.size(); ++i) {
        imgPathToIndex[m_imageDeviceList[i].imagePath] = i;
    }

    // 遍历备份设备数据
    for (const QJsonValue& val : devArray) {
        if (!val.isObject()) continue;
        QJsonObject obj = val.toObject();

        // 提取设备信息
        QString devId = obj["id"].toString().trimmed();
        QString devType = obj["type"].toString().trimmed();
        QString imgPath = obj["image_path"].toString().trimmed();
        int x = obj["x"].toInt(-1);
        int y = obj["y"].toInt(-1);

        // 校验必要字段
        if (devId.isEmpty() || devType.isEmpty() || imgPath.isEmpty() || x == -1 || y == -1) {
            writeCrashLog("[恢复设备] 设备数据不完整，跳过：" + devId);
            continue;
        }

        // 生成设备唯一标识（避免同一图片下重复设备）
        QString deviceUniqueKey = devId + "_" + imgPath;
        if (existingDeviceIds.contains(deviceUniqueKey)) {
            writeCrashLog("[恢复设备] 设备已存在，跳过：" + deviceUniqueKey);
            continue;
        }

        // 处理图片数据（不存在则创建）
        int imgIndex = -1;
        if (imgPathToIndex.contains(imgPath)) {
            imgIndex = imgPathToIndex[imgPath];
        } else {
            // 新增图片数据
            ImageDeviceData newImgData;
            newImgData.imagePath = imgPath;
            m_imageDeviceList.append(newImgData);
            imgIndex = m_imageDeviceList.size() - 1;
            imgPathToIndex[imgPath] = imgIndex;

            // 更新下拉列表
            if (ui->cmbImageList) {
                QString fileName = QFileInfo(imgPath).fileName();
                ui->cmbImageList->addItem(fileName);
            }
            writeCrashLog("[恢复设备] 新增图片数据：" + imgPath);
        }

        // 创建设备控件
        QWidget* parentWidget = ui->labelImage->parentWidget();
        if (!parentWidget) {
            writeCrashLog("[恢复设备] 无法获取父控件，跳过设备：" + devId);
            continue;
        }

        DeviceWidget* dev = new DeviceWidget(devType, devId, parentWidget);
        if (!dev) {
            writeCrashLog("[恢复设备] 设备创建失败：" + devId);
            continue;
        }

        // 设置设备位置（绝对像素）
        dev->setPos(QPoint(x, y));
        dev->hide();

        wireDeviceSelectionSignals(dev);

        // 添加到设备列表并标记已存在
        m_imageDeviceList[imgIndex].devices.append(dev);
        existingDeviceIds.insert(deviceUniqueKey);

        writeCrashLog(QString("[恢复设备] 成功恢复设备：ID=%1，类型=%2，图片=%3，位置=(%4,%5)")
                     .arg(devId).arg(devType).arg(QFileInfo(imgPath).fileName()).arg(x).arg(y));
    }

    // 如果恢复后有图片，默认切换到第一张
    if (!m_imageDeviceList.isEmpty() && m_currentImageIndex == -1) {
        switchToImage(0);
        if (ui->cmbImageList) {
            ui->cmbImageList->setCurrentIndex(1);
        }
    }

    writeCrashLog(QString("[恢复设备] 完成，共恢复%1个设备，图片总数=%2")
                 .arg(existingDeviceIds.size()).arg(m_imageDeviceList.size()));
    return true;
}
void MainWindow::switchToNextImage() {
    int imageCount = m_imageDeviceList.size();
    if (imageCount <= 0) {
        m_imageCycleTimer->stop();
        return;
    }

    int nextIndex = m_currentImageIndex < 0 ? 0 : (m_currentImageIndex + 1) % imageCount;

    // 立即切换图片，确保主题同步更新
    switchToImage(nextIndex);
    if (ui->cmbImageList) {
        ui->cmbImageList->setCurrentIndex(m_currentImageIndex + 1);
    }
}
QList<QJsonObject> MainWindow::getCurrentImageDevices()
{
    QList<QJsonObject> devicesJson;

    // 1. 校验：是否选中图片
    if (m_currentImageIndex < 0 || m_currentImageIndex >= m_imageDeviceList.size()) {
        return devicesJson;
    }

    // 2. 获取当前图片的【原始尺寸】（关键：用原始尺寸计算比例，避免前端缩放影响）
    const ImageDeviceData& currentImgData = m_imageDeviceList[m_currentImageIndex];
    QPixmap originalPixmap(currentImgData.imagePath);
    int imgWidth = originalPixmap.width();  // 图片原始宽度（如1800像素）
    int imgHeight = originalPixmap.height();// 图片原始高度（如650像素）

    // 边界处理：避免图片加载失败导致宽高为0（除零错误）
    if (imgWidth <= 0 || imgHeight <= 0) {
        imgWidth = 1280;  // 默认宽度（和你代码默认值一致）
        imgHeight = 720;  // 默认高度（和你代码默认值一致）
        writeCrashLog("[getCurrentImageDevices] 图片加载失败，使用默认尺寸计算比例：1280x720");
    }

    // 3. 遍历当前图片的所有设备
    const QList<DeviceWidget*>& devices = m_imageDeviceList[m_currentImageIndex].devices;
    for (DeviceWidget* dev : devices) {
        if (!dev) continue;

        QJsonObject devObj;
        devObj["id"] = dev->getId();       // 设备ID（如"E1"）
        devObj["type"] = dev->getType();   // 设备类型（如"E"）

        // 4. 核心转换：绝对像素 → 相对比例（×100，保留1位小数，转字符串）
        int absoluteX = dev->getPos().x();  // 设备绝对像素X（如476）
        int absoluteY = dev->getPos().y();  // 设备绝对像素Y（如330）

        // 计算相对比例（示例：476 ÷ 1800 × 100 ≈ 26.4）
        double relativeX = (static_cast<double>(absoluteX) / imgWidth) * 100;
        double relativeY = (static_cast<double>(absoluteY) / imgHeight) * 100;

        // 转成字符串（保留1位小数，格式如"26.4"，和你要的一致）
        devObj["x"] = QString::number(relativeX, 'f', 1);
        devObj["y"] = QString::number(relativeY, 'f', 1);

        devicesJson.append(devObj);
    }

    return devicesJson;
}
// 【新增】获取当前图片的修改时间戳（用于前端检测图片变化）
qint64 MainWindow::getCurrentImageTimestamp()
{
    if (m_currentImageIndex < 0 || m_currentImageIndex >= m_imageDeviceList.size()) {
        return 0;
    }

    QString imagePath = m_imageDeviceList[m_currentImageIndex].imagePath;
    return QFileInfo(imagePath).lastModified().toMSecsSinceEpoch();
}

QString MainWindow::normalizeClientIp(const QString& rawIp)
{
    QString ip = rawIp.trimmed();
    if (ip.startsWith(QStringLiteral("::ffff:"), Qt::CaseInsensitive)) {
        ip = ip.mid(7);
    }
    if (ip == QStringLiteral("::1") || ip.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("127.0.0.1");
    }
    // 去掉可能的端口（极少见）
    if (ip.contains(QLatin1Char('%'))) {
        ip = ip.section(QLatin1Char('%'), 0, 0);
    }
    return ip;
}

int MainWindow::resolveImageIndexForClientIp(const QString& clientIp) const
{
    const QString ip = normalizeClientIp(clientIp);
    if (!ip.isEmpty()) {
        for (const DisplayScreenConfig& screen : m_displayScreens) {
            if (normalizeClientIp(screen.ip) != ip) {
                continue;
            }

            QList<int> validIndices;
            for (const QString& path : screen.imagePaths) {
                const int idx = findImageIndex(path);
                if (idx >= 0) {
                    validIndices.append(idx);
                }
            }
            if (validIndices.isEmpty()) {
                break;
            }
            if (validIndices.size() == 1) {
                return validIndices.first();
            }

            const int interval = screen.switchSeconds > 0 ? screen.switchSeconds : 10;
            const qint64 slot = QDateTime::currentSecsSinceEpoch() / interval;
            const int rot = static_cast<int>(slot % validIndices.size());
            return validIndices.at(rot);
        }
    }
    // 未登记或图片已删除：回退主界面当前选中图
    if (m_currentImageIndex >= 0 && m_currentImageIndex < m_imageDeviceList.size()) {
        return m_currentImageIndex;
    }
    return m_imageDeviceList.isEmpty() ? -1 : 0;
}

QString MainWindow::getImagePathForClientIp(const QString& clientIp) const
{
    const int idx = resolveImageIndexForClientIp(clientIp);
    if (idx < 0 || idx >= m_imageDeviceList.size()) {
        return QString();
    }
    return m_imageDeviceList[idx].imagePath;
}

QString MainWindow::getImageThemeForClientIp(const QString& clientIp) const
{
    const int idx = resolveImageIndexForClientIp(clientIp);
    if (idx < 0 || idx >= m_imageDeviceList.size()) {
        return QStringLiteral("静电管理在线监控系统 ESD-1000.V1.0");
    }
    const QString& theme = m_imageDeviceList[idx].theme;
    return theme.isEmpty() ? QStringLiteral("静电管理在线监控系统 ESD-1000.V1.0") : theme;
}

QList<QJsonObject> MainWindow::getImageDevicesByIndex(int imageIndex) const
{
    QList<QJsonObject> devicesJson;
    if (imageIndex < 0 || imageIndex >= m_imageDeviceList.size()) {
        return devicesJson;
    }

    const ImageDeviceData& currentImgData = m_imageDeviceList[imageIndex];
    QPixmap originalPixmap(currentImgData.imagePath);
    int imgWidth = originalPixmap.width();
    int imgHeight = originalPixmap.height();
    if (imgWidth <= 0 || imgHeight <= 0) {
        imgWidth = 1280;
        imgHeight = 720;
    }

    for (DeviceWidget* dev : currentImgData.devices) {
        if (!dev) continue;
        QJsonObject devObj;
        devObj["id"] = dev->getId();
        devObj["type"] = dev->getType();
        const int absoluteX = dev->getPos().x();
        const int absoluteY = dev->getPos().y();
        const double relativeX = (static_cast<double>(absoluteX) / imgWidth) * 100;
        const double relativeY = (static_cast<double>(absoluteY) / imgHeight) * 100;
        devObj["x"] = QString::number(relativeX, 'f', 1);
        devObj["y"] = QString::number(relativeY, 'f', 1);
        devicesJson.append(devObj);
    }
    return devicesJson;
}

QList<QJsonObject> MainWindow::getImageDevicesForClientIp(const QString& clientIp)
{
    return getImageDevicesByIndex(resolveImageIndexForClientIp(clientIp));
}

qint64 MainWindow::getImageTimestampByIndex(int imageIndex) const
{
    if (imageIndex < 0 || imageIndex >= m_imageDeviceList.size()) {
        return 0;
    }
    return QFileInfo(m_imageDeviceList[imageIndex].imagePath).lastModified().toMSecsSinceEpoch();
}

qint64 MainWindow::getImageTimestampForClientIp(const QString& clientIp) const
{
    const int idx = resolveImageIndexForClientIp(clientIp);
    const qint64 fileTs = getImageTimestampByIndex(idx);
    // 叠加入轮换槽位，保证多图切换时前端一定检测到变化
    const QString ip = normalizeClientIp(clientIp);
    int interval = 10;
    for (const DisplayScreenConfig& screen : m_displayScreens) {
        if (normalizeClientIp(screen.ip) == ip) {
            interval = screen.switchSeconds > 0 ? screen.switchSeconds : 10;
            break;
        }
    }
    const qint64 slot = QDateTime::currentSecsSinceEpoch() / interval;
    return fileTs + slot * 1000000000LL + (idx + 1);
}

void MainWindow::pruneDisplayScreensForMissingImages()
{
    QList<DisplayScreenConfig> kept;
    for (DisplayScreenConfig screen : m_displayScreens) {
        QStringList validPaths;
        for (const QString& path : screen.imagePaths) {
            if (findImageIndex(path) >= 0) {
                validPaths.append(path);
            }
        }
        if (!validPaths.isEmpty()) {
            screen.imagePaths = validPaths;
            kept.append(screen);
        }
    }
    m_displayScreens = kept;
}

void MainWindow::onBtnDisplayScreensClicked()
{
    if (m_imageDeviceList.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请先添加背景图片，再配置显示屏映射。"));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("显示屏设置（IP → 背景图轮换）"));
    dlg.resize(860, 480);

    QVBoxLayout *root = new QVBoxLayout(&dlg);
    QLabel *hint = new QLabel(
        QStringLiteral("显示机浏览器访问 http://本机IP:1388 时，按对方 IP 推送对应背景图及点位。\n"
                       "同一显示屏可勾选多张背景图，并设置切换间隔（秒）；只勾选一张则不轮换。\n"
                       "未登记的 IP 将显示主界面当前选中的背景图。"),
        &dlg);
    hint->setWordWrap(true);
    root->addWidget(hint);

    QTableWidget *table = new QTableWidget(&dlg);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({
        QStringLiteral("名称"),
        QStringLiteral("显示机IP"),
        QStringLiteral("背景图（可多选）"),
        QStringLiteral("切换秒数")
    });
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setDefaultSectionSize(110);
    root->addWidget(table);

    auto makeImageList = [this](const QStringList& selectedPaths) -> QListWidget* {
        QListWidget *list = new QListWidget();
        list->setMinimumHeight(90);
        for (int i = 0; i < m_imageDeviceList.size(); ++i) {
            const QString path = m_imageDeviceList[i].imagePath;
            const QString name = QFileInfo(path).fileName();
            QListWidgetItem *item = new QListWidgetItem(
                QStringLiteral("%1 — %2").arg(i + 1).arg(name), list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setData(Qt::UserRole, path);
            bool checked = false;
            const QString absPath = QFileInfo(path).absoluteFilePath();
            for (const QString& sel : selectedPaths) {
                if (sel == path || QFileInfo(sel).absoluteFilePath() == absPath) {
                    checked = true;
                    break;
                }
            }
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        }
        return list;
    };

    auto addRow = [&](const DisplayScreenConfig& screen) {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(screen.name));
        table->setItem(row, 1, new QTableWidgetItem(screen.ip));
        table->setCellWidget(row, 2, makeImageList(screen.imagePaths));
        QSpinBox *spin = new QSpinBox(table);
        spin->setRange(1, 3600);
        spin->setValue(screen.switchSeconds > 0 ? screen.switchSeconds : 10);
        spin->setSuffix(QStringLiteral(" 秒"));
        table->setCellWidget(row, 3, spin);
    };

    for (const DisplayScreenConfig& screen : m_displayScreens) {
        addRow(screen);
    }
    if (table->rowCount() == 0) {
        DisplayScreenConfig empty;
        empty.name = QStringLiteral("显示屏1");
        empty.ip = QStringLiteral("192.168.1.101");
        empty.switchSeconds = 10;
        if (!m_imageDeviceList.isEmpty()) {
            empty.imagePaths << m_imageDeviceList[0].imagePath;
        }
        addRow(empty);
    }

    QHBoxLayout *btnRow = new QHBoxLayout();
    QPushButton *btnAdd = new QPushButton(QStringLiteral("添加"), &dlg);
    QPushButton *btnRemove = new QPushButton(QStringLiteral("删除选中"), &dlg);
    QPushButton *btnOk = new QPushButton(QStringLiteral("保存"), &dlg);
    QPushButton *btnCancel = new QPushButton(QStringLiteral("取消"), &dlg);
    btnRow->addWidget(btnAdd);
    btnRow->addWidget(btnRemove);
    btnRow->addStretch();
    btnRow->addWidget(btnOk);
    btnRow->addWidget(btnCancel);
    root->addLayout(btnRow);

    connect(btnAdd, &QPushButton::clicked, &dlg, [&]() {
        DisplayScreenConfig screen;
        screen.name = QStringLiteral("显示屏%1").arg(table->rowCount() + 1);
        screen.ip.clear();
        screen.switchSeconds = 10;
        if (!m_imageDeviceList.isEmpty()) {
            screen.imagePaths << m_imageDeviceList[0].imagePath;
        }
        addRow(screen);
    });
    connect(btnRemove, &QPushButton::clicked, &dlg, [&]() {
        const int row = table->currentRow();
        if (row >= 0) {
            table->removeRow(row);
        }
    });
    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnOk, &QPushButton::clicked, &dlg, [&]() {
        QList<DisplayScreenConfig> screens;
        QSet<QString> usedIps;
        for (int row = 0; row < table->rowCount(); ++row) {
            DisplayScreenConfig screen;
            screen.name = table->item(row, 0) ? table->item(row, 0)->text().trimmed() : QString();
            screen.ip = table->item(row, 1) ? table->item(row, 1)->text().trimmed() : QString();
            QListWidget *list = qobject_cast<QListWidget*>(table->cellWidget(row, 2));
            QSpinBox *spin = qobject_cast<QSpinBox*>(table->cellWidget(row, 3));
            screen.switchSeconds = spin ? spin->value() : 10;
            if (list) {
                for (int i = 0; i < list->count(); ++i) {
                    QListWidgetItem *item = list->item(i);
                    if (item && item->checkState() == Qt::Checked) {
                        screen.imagePaths.append(item->data(Qt::UserRole).toString());
                    }
                }
            }

            if (screen.ip.isEmpty()) {
                QMessageBox::warning(&dlg, QStringLiteral("提示"),
                                     QStringLiteral("第 %1 行 IP 不能为空").arg(row + 1));
                return;
            }
            QHostAddress addr(screen.ip);
            if (addr.isNull()) {
                QMessageBox::warning(&dlg, QStringLiteral("提示"),
                                     QStringLiteral("第 %1 行 IP 格式无效：%2").arg(row + 1).arg(screen.ip));
                return;
            }
            const QString norm = normalizeClientIp(screen.ip);
            if (usedIps.contains(norm)) {
                QMessageBox::warning(&dlg, QStringLiteral("提示"),
                                     QStringLiteral("IP 重复：%1").arg(screen.ip));
                return;
            }
            usedIps.insert(norm);
            screen.ip = norm;
            if (screen.imagePaths.isEmpty()) {
                QMessageBox::warning(&dlg, QStringLiteral("提示"),
                                     QStringLiteral("第 %1 行请至少勾选一张背景图").arg(row + 1));
                return;
            }
            screens.append(screen);
        }
        m_displayScreens = screens;
        saveToConfig();
        writeCrashLog(QStringLiteral("[DisplayScreen] 已保存 %1 条显示屏映射（支持多图轮换）")
                          .arg(m_displayScreens.size()));
        QMessageBox::information(&dlg, QStringLiteral("成功"),
                                 QStringLiteral("显示屏映射已保存。\n"
                                                "多图时按设定秒数自动轮换；显示机浏览器打开 http://本机IP:1388 即可。"));
        dlg.accept();
    });

    dlg.exec();
}

// 控制图片自动循环的启动/停止
void MainWindow::onPushButton4Clicked()
{
    // 先检查是否有图片（无图片则提示）
    if (m_imageDeviceList.isEmpty()) {
        QMessageBox::warning(this, "提示", "暂无图片可循环！请先添加背景图片");
        writeCrashLog("[图片循环控制] 无图片，无法启动自动循环");
        return;
    }

    // 根据定时器状态切换（启动 ↔ 停止）
    if (m_imageCycleTimer->isActive()) {
        // 状态：正在循环 → 停止循环
        m_imageCycleTimer->stop();
        writeCrashLog("[图片循环控制] 停止自动循环");
    } else {
        // 状态：未循环 → 启动循环
        m_imageCycleTimer->start();
        // 启动时立即切换到下一张（可选，增强交互体验）
        switchToNextImage();
        writeCrashLog(QString("[图片循环控制] 启动自动循环，切换间隔：%1ms")
                     .arg(m_imageCycleTimer->interval()));
    }

    // 更新按钮文本（同步状态）
    updateCycleButtonText();
}

// 辅助函数：根据定时器状态更新按钮文本
void MainWindow::updateCycleButtonText()
{
    if (m_imageCycleTimer->isActive()) {
        ui->pushButton_4->setText("停止自动循环");
    } else {
        ui->pushButton_4->setText("启动自动循环");
    }
}
// ========== 极简加密（XOR）：防止用户直接修改文本文件 ==========
QString MainWindow::simpleEncrypt(const QString& content) {
    QString result;
    for (QChar c : content) {
        result.append(QChar(c.unicode() - 1)); // 每个字符减1，变成乱码
    }
    return result;
}

// ========== 读取/初始化首次运行时间 ==========
QDateTime MainWindow::getFirstRunTime() {
    QString licensePath = QCoreApplication::applicationDirPath() + "/license.txt";
    QFile file(licensePath);
    QString pcName = QSysInfo::machineHostName();

    if (!file.exists()) {
        // 首次运行：加密存储（字符-1）
        QDateTime firstRun = QDateTime::currentDateTime();
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QString data = pcName + "|" + firstRun.toString(Qt::ISODate);
            QTextStream out(&file);
            out << simpleEncrypt(data); // 加密：字符-1
            file.close();
        }
        return firstRun;
    }

    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString encrypted = file.readAll().trimmed();
        // 解密：每个字符+1还原
        QString decrypted;
        for (QChar c : encrypted) {
            decrypted.append(QChar(c.unicode() + 1)); // 逆操作：加1
        }
        QStringList parts = decrypted.split("|");

        if (parts.size() == 2 && parts[0] == pcName) {
            return QDateTime::fromString(parts[1], Qt::ISODate);
        } else {
            return QDateTime::fromString("2000-01-01", Qt::ISODate);
        }
    }
    return QDateTime::currentDateTime();
}

// ========== 检查是否过期（30天） ==========
bool MainWindow::checkTrialExpired() {
    // 注释掉过期检查，永远返回false表示未过期
    return false;
//    QDateTime firstRun = getFirstRunTime();
//    // 计算首次运行到现在的天数差
//    int daysPassed = firstRun.daysTo(QDateTime::currentDateTime());
//    return daysPassed > 7; // 超过7天返回true（过期）
}
void MainWindow::onPushButton5Clicked()
{
    if (!getToolSerialWorker()) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请先在连接设置中连接串口"));
        return;
    }

    ui->lineEdit_7->clear();

    bool addrOk = false;
    const int decAddr = ui->lineEdit_5->text().toInt(&addrOk);
    if (!addrOk || decAddr <= 0) {
        QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("主机地址输入无效！请输入正整数"));
        return;
    }
    const QString hexAddr = QString("%1").arg(decAddr, 2, 16, QChar('0')).toUpper();

    bool regOk = false;
    const int decReg = ui->lineEdit_6->text().toInt(&regOk);
    if (!regOk || decReg < 0) {
        QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("通道编号输入无效！请输入非负整数"));
        return;
    }

    const QString type = ui->comboBox_4->currentText().trimmed();
    QString funcCode;
    if (type == QStringLiteral("腕带")) {
        funcCode = QStringLiteral("0110");
    } else if (type == QStringLiteral("台垫")) {
        funcCode = QStringLiteral("0113");
    } else if (type == QStringLiteral("设备")) {
        funcCode = QStringLiteral("0120");
    } else {
        QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("请选择有效类型（腕带/台垫/设备）！"));
        return;
    }

    QString modbusStr;
    if (type == QStringLiteral("腕带")) {
        const int channelAddr = decReg - 1;
        if (channelAddr < 0 || channelAddr > 0x3C) {
            QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("腕带通道编号超出范围！请输入1-60之间的数字"));
            return;
        }
        const QString hexChannelAddr = QString("%1").arg(channelAddr, 4, 16, QChar('0')).toUpper();
        const QString wristbandHexAddr = QString("%1").arg(decAddr, 4, 16, QChar('0')).toUpper();
        modbusStr = wristbandHexAddr + funcCode + hexChannelAddr + QStringLiteral("0001");
    } else if (type == QStringLiteral("台垫")) {
        modbusStr = QStringLiteral("00") + hexAddr + funcCode + QStringLiteral("00000001");
    } else {
        modbusStr = QStringLiteral("00") + hexAddr + funcCode + QStringLiteral("00000001");
    }

    btn5DeviceType = type;
    btn5DeviceAddr = decAddr;
    btn5DeviceReg = decReg;
    sendToolFrame(modbusStr, funcCode, ToolCommandContext::DeviceTestQuery);
}

void MainWindow::onPushButton6Clicked()
{
    if (!getToolSerialWorker()) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请先在连接设置中连接串口"));
        return;
    }

    bool addrOk = false;
    int addr = ui->lineEdit_8->text().toInt(&addrOk);
    if (!addrOk || addr <= 0) {
        addr = 1;
    }
    const QString hexAddr = QString("%1").arg(addr, 4, 16, QChar('0')).toUpper();

    bool channelOk = false;
    int channel = ui->lineEdit_9->text().toInt(&channelOk);
    if (!channelOk || channel <= 0) {
        channel = 1;
    }
    const QString hexChannel = QString("%1").arg(channel, 4, 16, QChar('0')).toUpper();

    const QString type = ui->comboBox_5->currentText();
    QString func;
    if (type == QStringLiteral("腕带")) {
        func = QStringLiteral("0010");
    } else if (type == QStringLiteral("台垫")) {
        func = QStringLiteral("0013");
    } else if (type == QStringLiteral("设备")) {
        func = QStringLiteral("0020");
    } else {
        QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("请选择有效类型（腕带/台垫/设备）！"));
        return;
    }

    const int status = ui->lineEdit_10->text().toInt();
    const QString data = (status == 1) ? QStringLiteral("1000") : QStringLiteral("0000");
    const QString sendStr = hexAddr + func + QStringLiteral("0000") + hexChannel + data;
    sendToolFrame(sendStr, func, ToolCommandContext::DeviceTestApply);
}
// 打开button5/6专用串口（参数从comboBox_6/7读取）
void MainWindow::openBtnSerial()
{
    if (m_isBtnSerialConnected) {
        // 关闭专用串口
        QMutexLocker locker(&serialBtnMutex);
        if (serialBtn->isOpen()) {
            serialBtn->close();
        }
        locker.unlock();

        m_isBtnSerialConnected = false;
        ui->pushButton_btnSerial->setText("打开专用串口");
        QMessageBox::information(this, "成功", "button5/6专用串口已关闭");
        writeCrashLog("[专用串口] 已关闭");
        return;
    }

    // 读取专用串口配置（仅UI控件，无线程）
    QString portName = ui->comboBox_6->currentText().trimmed();
    bool baudOk;
    int baudRate = ui->comboBox_7->currentText().toInt(&baudOk);
    if (!baudOk) {
        QMessageBox::critical(this, "错误", "波特率选择无效！");
        writeCrashLog("[专用串口] 打开失败：波特率无效");
        return;
    }
    if (!portName.startsWith("COM") && !portName.startsWith("/dev/")) {
        QMessageBox::warning(this, "提示", "COM口格式无效！请选择正确的COM口");
        writeCrashLog("[专用串口] 打开失败：端口格式无效 " + portName);
        return;
    }

    // 配置并打开专用串口
    QMutexLocker locker(&serialBtnMutex);
    serialBtn->setPortName(portName);
    serialBtn->setBaudRate(baudRate);
    serialBtn->setDataBits(QSerialPort::Data8);
    serialBtn->setParity(QSerialPort::NoParity);
    serialBtn->setStopBits(QSerialPort::OneStop);
    serialBtn->setFlowControl(QSerialPort::NoFlowControl);

    if (!serialBtn->open(QIODevice::ReadWrite)) {
        locker.unlock();
        QMessageBox::critical(this, "错误", "专用串口打开失败：" + serialBtn->errorString());
        writeCrashLog("[专用串口] 打开失败：" + serialBtn->errorString());
        return;
    }
    locker.unlock();

    m_isBtnSerialConnected = true;
    ui->pushButton_btnSerial->setText("关闭专用串口");
    QMessageBox::information(this, "成功", "button5/6专用串口已打开");
    writeCrashLog("[专用串口] 已打开：" + portName);
}

// 托盘图标初始化
void MainWindow::initTrayIcon()
{
    // 创建托盘菜单
    m_trayMenu = new QMenu(this);

    // 创建菜单项
    m_showAction = new QAction("显示主窗口", this);
    m_hideAction = new QAction("隐藏主窗口", this);
    m_exitAction = new QAction("退出程序", this);

    // 添加菜单项到菜单
    m_trayMenu->addAction(m_showAction);
    m_trayMenu->addAction(m_hideAction);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(m_exitAction);

    // 创建托盘图标
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setContextMenu(m_trayMenu);

    // 设置托盘图标
    QIcon icon;
    icon.addFile("symbol/3HESD.png"); // 使用symbol文件夹下的3HESD.png作为图标
    m_trayIcon->setIcon(icon);
    m_trayIcon->setToolTip(QStringLiteral("静电管理在线监控系统 ESD-1000.V1.0"));

    // 连接信号槽
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);
    connect(m_showAction, &QAction::triggered, this, &MainWindow::onShowActionTriggered);
    connect(m_hideAction, &QAction::triggered, this, &MainWindow::onHideActionTriggered);
    connect(m_exitAction, &QAction::triggered, this, &MainWindow::onExitActionTriggered);

    // 显示托盘图标
    m_trayIcon->show();
}

// 托盘图标激活事件
void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch (reason)
    {
    case QSystemTrayIcon::Trigger: // 单击
        if (this->isHidden())
        {
            this->show();
        }
        else
        {
            this->hide();
        }
        break;
    case QSystemTrayIcon::DoubleClick: // 双击
        this->show();
        this->activateWindow();
        break;
    default:
        break;
    }
}

// 显示主窗口
void MainWindow::onShowActionTriggered()
{
    this->show();
    this->activateWindow();
}

// 隐藏主窗口
void MainWindow::onHideActionTriggered()
{
    this->hide();
}

// 退出程序
void MainWindow::onExitActionTriggered()
{
    // 关闭托盘图标
    m_trayIcon->hide();

    // 退出应用程序
    qApp->quit();
}

// 重写关闭事件
void MainWindow::closeEvent(QCloseEvent *event)
{
    // 询问用户是否要退出程序
    QMessageBox::StandardButton button;
    button = QMessageBox::question(this, "提示", "是否要退出程序？",
                                   QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);

    if (button == QMessageBox::Yes)
    {
        // 关闭托盘图标
        m_trayIcon->hide();

        // 退出应用程序
        event->accept();
    }
    else if (button == QMessageBox::No)
    {
        // 最小化到托盘
        this->hide();
        event->ignore();
    }
    else
    {
        // 取消关闭
        event->ignore();
    }
}

// 设置开机自启动
void MainWindow::setAutoStart(bool autoStart)
{
    m_autoStart = autoStart;

    QString appName = QApplication::applicationName();
    QString appPath = QApplication::applicationFilePath();
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);

    if (autoStart)
    {
        // 添加到开机自启动
        settings.setValue(appName, QVariant(appPath));
    }
    else
    {
        // 移除开机自启动
        settings.remove(appName);
    }
}

// 获取开机自启动状态
bool MainWindow::getAutoStart()
{
    QString appName = QApplication::applicationName();
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    return settings.contains(appName);
}

void MainWindow::recvBtnData()
{
    // 1. 读取专用串口数据
    QMutexLocker locker(&serialBtnMutex);
    QByteArray newData = serialBtn->readAll();
    locker.unlock();
    if (newData.isEmpty()) return;

    // 打印接收的原始数据到终端
    QString newDataHex = QString(newData.toHex(' ').toUpper());
    qDebug() << "[RECV RAW]" << newDataHex;
    writeCrashLog(QString("[RECV RAW] %1").arg(newDataHex));

    // 2. 追加到专用缓冲区
    QMutexLocker bufLocker(&btnRecvBufferMutex);
    btnRecvBuffer.append(newData);
    bufLocker.unlock();

    // 3. 缓冲区大小限制
    if (btnRecvBuffer.size() > 256) {
        btnRecvBuffer = btnRecvBuffer.mid(btnRecvBuffer.size() - 256);
        writeCrashLog("[专用串口] 缓冲区超256字节，保留最新256字节");
        return;
    }

    // 4. 校验预期响应
    if (btnCurrentExpectedAddrFunc.isEmpty()) {
        writeCrashLog("[专用串口] 未设置预期响应，暂不解析");
        return;
    }

    // 5. 查找预期帧起始位置
    int expectedStart = -1;
    bufLocker.relock();
    for (int i = 0; i <= btnRecvBuffer.size() - 4; i++) {
        QString addr;
        QString func;
        int addrOffset = 0;

        // 对于台垫，需要检查是否有前缀字节00
        if (btn5DeviceType == "台垫" && i < btnRecvBuffer.size() && btnRecvBuffer.at(i) == 0x00) {
            // 台垫响应有前缀字节00，跳过它
            addrOffset = 1;
            addr = QString(btnRecvBuffer.mid(i+1,2).toHex().toUpper()).rightJustified(4,'0');
            func = QString(btnRecvBuffer.mid(i+3,2).toHex().toUpper()).rightJustified(4,'0');
            writeCrashLog(QString("[recvBtnData] 台垫响应检测到前缀字节00，地址=%1，功能码=%2").arg(addr).arg(func));
        } else {
            // 其他设备或没有前缀字节的响应
            addr = QString(btnRecvBuffer.mid(i,2).toHex().toUpper()).rightJustified(4,'0');
            func = QString(btnRecvBuffer.mid(i+2,2).toHex().toUpper()).rightJustified(4,'0');
        }

        // 对于腕带，需要特殊处理功能码匹配，因为响应可能是"01 10"而不是"0110"
        bool funcMatch = false;
        if (btn5DeviceType == "腕带" && btnCurrentExpectedAddrFunc.right(4) == "0110") {
            // 检查是否是"01 10"格式（功能码分两个字节）
            if (i + 3 < btnRecvBuffer.size()) {
                QString respFuncByte1 = QString(btnRecvBuffer.mid(i+2+addrOffset, 1).toHex().toUpper());
                QString respFuncByte2 = QString(btnRecvBuffer.mid(i+3+addrOffset, 1).toHex().toUpper());
                funcMatch = (respFuncByte1 + respFuncByte2 == "0110");
                writeCrashLog(QString("[recvBtnData] 腕带功能码检查：%1 %2 -> %3，地址=%4")
                             .arg(respFuncByte1).arg(respFuncByte2).arg(respFuncByte1 + respFuncByte2).arg(addr));
            }
        } else {
            // 其他设备使用常规匹配
            funcMatch = (addr + func == btnCurrentExpectedAddrFunc);
        }

        if (funcMatch && addr == btnCurrentExpectedAddrFunc.left(4)) {
            expectedStart = i;
            writeCrashLog(QString("[recvBtnData] 找到预期帧起始：位置%1，地址+功能码：%2")
                         .arg(i).arg(addr + func));
            break;
        }
    }
    bufLocker.unlock();

    if (expectedStart == -1) {
        writeCrashLog(QString("[专用串口] 未找到预期帧（预期：%1），保留缓冲区").arg(btnCurrentExpectedAddrFunc));
        return;
    }

    // 6. 计算帧长度（针对读取响应：地址+功能码+字节数+数据+CRC）
    // 对于腕带，响应格式可能是：地址+功能码+寄存器地址+数据+CRC 或 地址+功能码+字节数+数据+CRC
    // 对于台垫，响应格式可能有前缀字节00
    int expectedFrameLen = -1;
    bufLocker.relock();
    if (btnRecvBuffer.size() >= expectedStart + 5) { // 至少需要地址+功能码+字节数
        uint8_t byteCount = static_cast<uint8_t>(btnRecvBuffer[expectedStart + 4]);

        // 对于腕带，需要特殊处理帧长度计算
        if (btn5DeviceType == "腕带") {
            // 腕带响应格式可能是：
            // 1) 02 01 10 00 01 30 01 27 D9（地址+功能码+寄存器地址+数据+CRC）
            // 2) 02 01 10 01 30 01 27 D9（地址+功能码+字节数+数据+CRC）

            // 检查第5个字节是否是寄存器地址（通常小于60）
            uint8_t fifthByte = static_cast<uint8_t>(btnRecvBuffer[expectedStart + 4]);
            uint8_t sixthByte = static_cast<uint8_t>(btnRecvBuffer[expectedStart + 5]);

            // 如果第5个字节是0x00且第6个字节小于60，可能是寄存器地址格式
            if (fifthByte == 0x00 && sixthByte <= 0x3C) {
                // 格式：地址+功能码+寄存器地址+数据+CRC（2+2+2+2+2=10字节）
                expectedFrameLen = 10;
                writeCrashLog(QString("[recvBtnData] 腕带响应格式：寄存器地址格式，预期帧长%1字节").arg(expectedFrameLen));
            } else {
                // 格式：地址+功能码+字节数+数据+CRC
                // 对于腕带，字节数通常为1-2（单个通道）
                if (byteCount <= 2) {
                    expectedFrameLen = 2 + 2 + 1 + byteCount + 2;
                } else {
                    // 可能是异常响应或其他格式，尝试固定长度
                    expectedFrameLen = 9; // 最小帧长度
                }
                writeCrashLog(QString("[recvBtnData] 腕带响应格式：字节数格式，字节数%1，预期帧长%2字节").arg(byteCount).arg(expectedFrameLen));
            }
        } else if (btn5DeviceType == "台垫") {
            // 检查是否有前缀字节00
            bool hasPrefix = (expectedStart < btnRecvBuffer.size() && btnRecvBuffer.at(expectedStart) == 0x00);
            if (hasPrefix) {
                // 台垫响应有前缀字节00，字节数在偏移量4处
                byteCount = static_cast<uint8_t>(btnRecvBuffer[expectedStart + 4]);
                expectedFrameLen = 1 + 2 + 2 + 1 + byteCount + 2; // 前缀+地址+功能码+字节数+数据+CRC
                writeCrashLog(QString("[recvBtnData] 台垫响应格式：有前缀字节00，字节数%1，预期帧长%2字节").arg(byteCount).arg(expectedFrameLen));
            } else {
                // 台垫响应没有前缀字节，使用标准格式
                expectedFrameLen = 2 + 2 + 1 + byteCount + 2;
                writeCrashLog(QString("[recvBtnData] 台垫响应格式：无前缀字节，字节数%1，预期帧长%2字节").arg(byteCount).arg(expectedFrameLen));
            }
        } else {
            // 设备：标准读取响应
            expectedFrameLen = 2 + 2 + 1 + byteCount + 2;
            writeCrashLog(QString("[recvBtnData] 设备响应帧长：字节数%1 → 预期帧长%2字节").arg(byteCount).arg(expectedFrameLen));
        }
    }
    bufLocker.unlock();

    if (expectedFrameLen == -1) {
        writeCrashLog("[专用串口] 未获取到寄存器数，保留缓冲区");
        return;
    }

    // 7. 校验帧完整性
    bufLocker.relock();
    int currentFrameAvailableLen = btnRecvBuffer.size() - expectedStart;
    if (currentFrameAvailableLen < expectedFrameLen) {
        writeCrashLog(QString("[专用串口] 帧不完整（需%1字节，当前%2字节），保留缓冲区")
                     .arg(expectedFrameLen).arg(currentFrameAvailableLen));
        bufLocker.unlock();
        return;
    }

    // 8. 提取有效帧并校验CRC
    QByteArray validFrame = btnRecvBuffer.mid(expectedStart, expectedFrameLen);
    QByteArray dataToCrc = validFrame.left(expectedFrameLen - 2);
    uint16_t calcCrc = calcrc(dataToCrc);
    uint16_t recvCrc = (static_cast<unsigned char>(validFrame[expectedFrameLen-1]) << 8) | static_cast<unsigned char>(validFrame[expectedFrameLen-2]);
    bufLocker.unlock();

    if (calcCrc != recvCrc) {
        QString errInfo = QString("[专用串口] CRC校验失败：计算值=0x%1，接收值=0x%2")
                          .arg(QString::number(calcCrc,16).toUpper().rightJustified(4,'0'))
                          .arg(QString::number(recvCrc,16).toUpper().rightJustified(4,'0'));
        writeCrashLog(errInfo);
        ui->textBrowser->append(errInfo);
        bufLocker.relock();
        btnRecvBuffer = btnRecvBuffer.mid(expectedStart + 1);
        bufLocker.unlock();
        return;
    }

    // 检查是否是pushButton_5的响应，如果是则解析状态
    if (!btn5DeviceType.isEmpty()) {
        QString respAddr = QString(validFrame.mid(0,2).toHex().toUpper()).rightJustified(4,'0');
        QString respFunc = QString(validFrame.mid(2,2).toHex().toUpper()).rightJustified(4,'0');
        int respAddrInt = respAddr.toInt(nullptr, 16);

        writeCrashLog(QString("[recvBtnData] 检查pushButton_5响应：设备类型=%1，地址=%2，功能码=%3")
                     .arg(btn5DeviceType).arg(respAddrInt).arg(respFunc));

        // 验证是否是预期的响应
        bool isExpectedResponse = false;
        if (btn5DeviceType == "腕带" && respFunc == "0110") {
            isExpectedResponse = true;
        } else if (btn5DeviceType == "台垫" && respFunc == "0113") {
            isExpectedResponse = true;
        } else if (btn5DeviceType == "设备" && respFunc == "0120") {
            isExpectedResponse = true;
        }

        if (isExpectedResponse && respAddrInt == btn5DeviceAddr) {
            writeCrashLog("[recvBtnData] 确认是pushButton_5的响应，调用解析函数");
            parsePushButton5Response(btn5DeviceType, btn5DeviceAddr, btn5DeviceReg);
            // 清除设备类型，避免重复解析
            btn5DeviceType.clear();
            // 注意：不在这里清理缓冲区，让parsePushButton5Response函数处理
            return;
        } else {
            // 不是pushButton_5的响应，使用常规解析
            parsingdata(validFrame); // 调用解析函数，更新UI
        }
    } else {
        // 不是pushButton_5的响应，使用常规解析
        parsingdata(validFrame); // 调用解析函数，更新UI
    }
    // ==============================================================

    // 处理pushButton_6响应
    if (!m_btn6SendKey.isEmpty()) {
        writeCrashLog(QString("[recvBtnData] 处理pushButton_6响应：预期核心参数=%1").arg(m_btn6SendKey));

        // 解析响应帧的核心参数（地址+功能码+通道+数量：前8个字符）
        QString respKey;

        // 检查响应帧是否有前缀字节00
        if (validFrame.size() > 0 && validFrame.at(0) == 0x00) {
            // 有前缀字节00，从第2字节开始解析
            if (validFrame.size() >= 8) {
                respKey = QString(validFrame.mid(1,2).toHex().toUpper()).rightJustified(4, '0'); // 地址(2字节)=4字符
                respKey += QString(validFrame.mid(3,2).toHex().toUpper()); // 功能码(2字节)=4字符
                respKey += QString(validFrame.mid(5,2).toHex().toUpper()); // 通道(2字节)=4字符
                respKey += QString(validFrame.mid(7,2).toHex().toUpper()); // 数量(2字节)=4字符
            }
        } else {
            // 没有前缀字节，从第1字节开始解析
            if (validFrame.size() >= 8) {
                respKey = QString(validFrame.mid(0,2).toHex().toUpper()).rightJustified(4, '0'); // 地址(2字节)=4字符
                respKey += QString(validFrame.mid(2,2).toHex().toUpper()); // 功能码(2字节)=4字符
                respKey += QString(validFrame.mid(4,2).toHex().toUpper()); // 通道(2字节)=4字符
                respKey += QString(validFrame.mid(6,2).toHex().toUpper()); // 数量(2字节)=4字符
            }
        }

        writeCrashLog(QString("[recvBtnData] 解析pushButton_6响应核心参数：%1").arg(respKey));

        // 校验：响应核心参数和发送的一致 → 弹窗成功
        if (respKey == m_btn6SendKey) {
            QMessageBox::information(this, "成功", "修改成功！");
            m_btn6SendKey.clear(); // 清空标记，避免重复弹窗
        }
    }

    // 9. 清理缓冲区+重置预期响应（仅对非pushButton_5响应）
    bufLocker.relock();
    btnRecvBuffer = btnRecvBuffer.mid(expectedStart + expectedFrameLen);
    btnCurrentExpectedAddrFunc.clear();
    bufLocker.unlock();
}

void MainWindow::parsePushButton5Response(const QString& type, int addr, int reg)
{
    // 检查是否有响应数据
    QMutexLocker bufLocker(&btnRecvBufferMutex);
    if (btnRecvBuffer.isEmpty()) {
        bufLocker.unlock();
        writeCrashLog("[parsePushButton5Response] 没有收到响应数据");
        ui->lineEdit_7->setText("无响应数据");
        return;
    }

    // 查找匹配地址和功能码的响应帧
    QString expectedAddr = QString("%1").arg(addr, 4, 16, QChar('0')).toUpper();
    QString expectedFunc;
    QString actualFunc; // 实际响应中的功能码可能不同
    QString errorFunc;  // 异常响应的功能码（功能码+80H）

    if (type == "腕带") {
        expectedFunc = "0110"; // 发送的功能码
        actualFunc = "0110";   // 实际响应的功能码
        errorFunc = "90";      // 异常响应功能码（10H+80H）
    } else if (type == "台垫") {
        expectedFunc = "0113"; // 发送的功能码
        // 根据实际数据，台垫响应可能是01H（读取线圈状态）或13H（保持寄存器）
        // 我们先尝试匹配01H，如果不匹配再尝试13H
        actualFunc = "01";     // 实际响应的功能码（读取线圈状态）
        errorFunc = "81";      // 异常响应功能码（01H+80H）
    } else if (type == "设备") {
        expectedFunc = "0120"; // 发送的功能码
        // 根据实际数据，设备响应可能是01H（读取线圈状态）或20H（保持寄存器）
        // 我们先尝试匹配01H，如果不匹配再尝试20H
        actualFunc = "01";     // 实际响应的功能码（读取线圈状态）
        errorFunc = "81";      // 异常响应功能码（01H+80H）
    }

    QString expectedAddrFunc = expectedAddr + actualFunc;
    QString expectedAddrErrorFunc = expectedAddr + errorFunc;
    writeCrashLog(QString("[parsePushButton5Response] 查找响应帧：地址+功能码=%1 或 %2").arg(expectedAddrFunc).arg(expectedAddrErrorFunc));

    int frameStart = -1;
    bool isErrorFrame = false;
    bool isAltResponse = false; // 是否使用备用响应格式

    // 首先尝试匹配主要响应格式
    for (int i = 0; i <= btnRecvBuffer.size() - 4; i++) {
        QString respAddr;
        QString respFunc;
        int addrOffset = 0;

        // 对于台垫，需要检查是否有前缀字节00
        if (type == "台垫" && i < btnRecvBuffer.size() && btnRecvBuffer.at(i) == 0x00) {
            // 台垫响应有前缀字节00，跳过它
            addrOffset = 1;
            respAddr = QString(btnRecvBuffer.mid(i+1, 2).toHex().toUpper()).rightJustified(4, '0');
            respFunc = QString(btnRecvBuffer.mid(i+3, 2).toHex().toUpper()).rightJustified(4, '0');
            writeCrashLog(QString("[parsePushButton5Response] 台垫响应检测到前缀字节00，地址=%1，功能码=%2").arg(respAddr).arg(respFunc));
        } else {
            // 其他设备或没有前缀字节的响应
            respAddr = QString(btnRecvBuffer.mid(i, 2).toHex().toUpper()).rightJustified(4, '0');
            respFunc = QString(btnRecvBuffer.mid(i+2, 2).toHex().toUpper()).rightJustified(4, '0');
        }

        // 对于腕带，需要特殊处理功能码匹配，因为响应可能是"01 10"而不是"0110"
        bool funcMatch = false;
        if (type == "腕带" && expectedFunc == "0110") {
            // 检查是否是"01 10"格式（功能码分两个字节）
            if (i + 3 < btnRecvBuffer.size()) {
                QString respFuncByte1 = QString(btnRecvBuffer.mid(i+2, 1).toHex().toUpper());
                QString respFuncByte2 = QString(btnRecvBuffer.mid(i+3, 1).toHex().toUpper());
                funcMatch = (respFuncByte1 + respFuncByte2 == "0110");
                writeCrashLog(QString("[parsePushButton5Response] 腕带功能码检查：%1 %2 -> %3，地址=%4")
                             .arg(respFuncByte1).arg(respFuncByte2).arg(respFuncByte1 + respFuncByte2).arg(respAddr));
            }
        } else if (type == "台垫" && expectedFunc == "0113") {
            // 台垫特殊处理：检查功能码匹配（与腕带类似的逻辑）
            if (i + 3 < btnRecvBuffer.size()) {
                QString respFuncByte1 = QString(btnRecvBuffer.mid(i+2+addrOffset, 1).toHex().toUpper());
                QString respFuncByte2 = QString(btnRecvBuffer.mid(i+3+addrOffset, 1).toHex().toUpper());
                funcMatch = (respFuncByte1 + respFuncByte2 == "0113");
                writeCrashLog(QString("[parsePushButton5Response] 台垫功能码检查：%1 %2 -> %3，地址=%4")
                             .arg(respFuncByte1).arg(respFuncByte2).arg(respFuncByte1 + respFuncByte2).arg(respAddr));
            }
        } else {
            // 其他设备使用常规匹配
            funcMatch = (respAddr + respFunc == expectedAddrFunc);
        }

        if (funcMatch && respAddr == expectedAddr) {
            frameStart = i;
            isErrorFrame = false;
            writeCrashLog(QString("[parsePushButton5Response] 找到正常匹配帧：位置%1，地址=%2").arg(i).arg(respAddr));
            break;
        } else if (respAddr + respFunc == expectedAddrErrorFunc) {
            frameStart = i;
            isErrorFrame = true;
            writeCrashLog(QString("[parsePushButton5Response] 找到异常匹配帧：位置%1").arg(i));
            break;
        }
    }

    // 对于腕带，如果没找到常规匹配，尝试直接匹配"02 01 10"格式
    if (frameStart == -1 && type == "腕带") {
        writeCrashLog(QString("[parsePushButton5Response] 尝试腕带直接匹配格式：地址+功能码=%1").arg(expectedAddr + "0110"));
        writeCrashLog(QString("[parsePushButton5Response] 缓冲区数据：%1").arg(QString(btnRecvBuffer.toHex(' ').toUpper())));

        for (int i = 0; i <= btnRecvBuffer.size() - 8; i++) {  // 确保有足够的字节来匹配完整帧
            QString respAddr = QString(btnRecvBuffer.mid(i, 2).toHex().toUpper()).rightJustified(4, '0');
            QString respFuncByte1 = "";
            QString respFuncByte2 = "";
            if (i+2 < btnRecvBuffer.size()) {
                respFuncByte1 = QString(btnRecvBuffer.mid(i+2, 1).toHex().toUpper());
            }
            if (i+3 < btnRecvBuffer.size()) {
                respFuncByte2 = QString(btnRecvBuffer.mid(i+3, 1).toHex().toUpper());
            }

            writeCrashLog(QString("[parsePushButton5Response] 位置%1：地址=%2，功能码1=%3，功能码2=%4")
                         .arg(i).arg(respAddr).arg(respFuncByte1).arg(respFuncByte2));

            // 检查是否匹配腕带格式：地址 01 10 ...
            if (respAddr == expectedAddr && respFuncByte1 == "01" && respFuncByte2 == "10") {
                frameStart = i;
                isErrorFrame = false;
                writeCrashLog(QString("[parsePushButton5Response] 找到腕带直接匹配帧：位置%1").arg(i));
                break;
            }
        }
    }

    // 对于腕带，如果没找到常规异常响应，尝试匹配特殊格式：00 02 90 10...
    if (frameStart == -1 && type == "腕带") {
        writeCrashLog(QString("[parsePushButton5Response] 尝试腕带特殊异常响应格式：地址+功能码=%1 90 10").arg(expectedAddr));
        writeCrashLog(QString("[parsePushButton5Response] 缓冲区数据：%1").arg(QString(btnRecvBuffer.toHex(' ').toUpper())));

        for (int i = 0; i <= btnRecvBuffer.size() - 8; i++) {  // 确保有足够的字节来匹配完整帧
            QString respAddr = QString(btnRecvBuffer.mid(i, 2).toHex().toUpper()).rightJustified(4, '0');
            QString respFunc1 = QString(btnRecvBuffer.mid(i+2, 2).toHex().toUpper()).rightJustified(4, '0');
            QString respFunc2 = "";
            if (i+3 < btnRecvBuffer.size()) {
                respFunc2 = QString(btnRecvBuffer.mid(i+3, 2).toHex().toUpper()).rightJustified(4, '0');
            }

            writeCrashLog(QString("[parsePushButton5Response] 位置%1：地址=%2，功能码1=%3，功能码2=%4")
                         .arg(i).arg(respAddr).arg(respFunc1).arg(respFunc2));

            // 检查是否匹配腕带特殊异常格式：地址 90 10 ...
            if (respAddr == expectedAddr && respFunc1 == "90" && respFunc2 == "10") {
                frameStart = i;
                isErrorFrame = true;
                writeCrashLog(QString("[parsePushButton5Response] 找到腕带特殊异常匹配帧：位置%1").arg(i));

                // 验证帧长度是否足够（至少8字节）
                if (i + 8 <= btnRecvBuffer.size()) {
                    // 验证CRC
                    QByteArray frameToCheck = btnRecvBuffer.mid(i, 8);
                    QByteArray dataToCrc = frameToCheck.left(6); // 前6字节用于CRC计算
                    uint16_t calcCrc = calcrc(dataToCrc);
                    uint16_t recvCrc = (static_cast<unsigned char>(frameToCheck[7]) << 8) |
                                       static_cast<unsigned char>(frameToCheck[6]);

                    writeCrashLog(QString("[parsePushButton5Response] 腕带特殊异常CRC校验：计算值=0x%1，接收值=0x%2")
                                 .arg(calcCrc, 4, 16, QChar('0')).toUpper()
                                 .arg(recvCrc, 4, 16, QChar('0')).toUpper());

                    // 如果CRC校验失败，继续查找下一个可能的帧
                    if (calcCrc != recvCrc) {
                        writeCrashLog(QString("[parsePushButton5Response] 腕带特殊异常CRC校验失败，继续查找"));
                        frameStart = -1;
                        isErrorFrame = false;
                        continue;
                    }
                }
                break;
            }
        }
    }

    // 如果没有找到主要格式，尝试匹配备用格式（腕带/台垫/设备）
    if (frameStart == -1) {
        QString altFunc;
        if (type == "腕带") {
            altFunc = "0110"; // 腕带备用功能码
        } else if (type == "台垫") {
            altFunc = "13"; // 台垫备用功能码
        } else if (type == "设备") {
            altFunc = "20"; // 设备备用功能码
        }

        QString expectedAddrAltFunc = expectedAddr + altFunc;
        writeCrashLog(QString("[parsePushButton5Response] 尝试备用响应格式：地址+功能码=%1").arg(expectedAddrAltFunc));

        for (int i = 0; i <= btnRecvBuffer.size() - 4; i++) {
            QString respAddr;
            QString respFunc;

            // 对于台垫，需要检查是否有前缀字节00
            int altAddrOffset = 0;
            if (type == "台垫" && i < btnRecvBuffer.size() && btnRecvBuffer.at(i) == 0x00) {
                // 台垫响应有前缀字节00，跳过它
                altAddrOffset = 1;
                respAddr = QString(btnRecvBuffer.mid(i+1, 2).toHex().toUpper()).rightJustified(4, '0');
                respFunc = QString(btnRecvBuffer.mid(i+3, 2).toHex().toUpper()).rightJustified(4, '0');
                writeCrashLog(QString("[parsePushButton5Response] 台垫备用格式检测到前缀字节00，地址=%1，功能码=%2").arg(respAddr).arg(respFunc));
            } else {
                // 其他设备或没有前缀字节的响应
                respAddr = QString(btnRecvBuffer.mid(i, 2).toHex().toUpper()).rightJustified(4, '0');
                respFunc = QString(btnRecvBuffer.mid(i+2, 2).toHex().toUpper()).rightJustified(4, '0');
            }

            // 特殊功能码匹配（与主格式类似的逻辑）
            bool altFuncMatch = false;
            if (type == "腕带" && altFunc == "0110") {
                // 腕带特殊处理
                if (i + 3 < btnRecvBuffer.size()) {
                    QString respFuncByte1 = QString(btnRecvBuffer.mid(i+2, 1).toHex().toUpper());
                    QString respFuncByte2 = QString(btnRecvBuffer.mid(i+3, 1).toHex().toUpper());
                    altFuncMatch = (respFuncByte1 + respFuncByte2 == "0110");
                }
            } else if (type == "台垫" && altFunc == "13") {
                // 台垫特殊处理
                if (i + 3 < btnRecvBuffer.size()) {
                    QString respFuncByte1 = QString(btnRecvBuffer.mid(i+2+altAddrOffset, 1).toHex().toUpper());
                    QString respFuncByte2 = QString(btnRecvBuffer.mid(i+3+altAddrOffset, 1).toHex().toUpper());
                    altFuncMatch = (respFuncByte1 + respFuncByte2 == "0113");
                }
            } else {
                // 其他设备使用常规匹配
                altFuncMatch = (respAddr + respFunc == expectedAddrAltFunc);
            }

            if (altFuncMatch && respAddr == expectedAddr) {
                frameStart = i;
                isErrorFrame = false;
                isAltResponse = true;
                writeCrashLog(QString("[parsePushButton5Response] 找到备用格式匹配帧：位置%1").arg(i));
                break;
            }
        }
    }

    if (frameStart == -1) {
        bufLocker.unlock();
        writeCrashLog("[parsePushButton5Response] 未找到匹配的响应帧");
        // 显示原始接收的数据用于调试
        QString hexData = btnRecvBuffer.toHex(' ').toUpper();
        writeCrashLog(QString("[parsePushButton5Response] 缓冲区数据：%1").arg(hexData));
        ui->lineEdit_7->setText(QString("未找到匹配响应 (原始数据: %1)").arg(hexData));
        return;
    }

    // 提取响应帧数据
    QByteArray frame = btnRecvBuffer.mid(frameStart);
    bufLocker.unlock();

    // 如果是异常响应，解析异常码
    if (isErrorFrame) {
        // 腕带异常响应格式：地址+80+10+异常码+CRC = 2+1+1+2+2 = 8字节
        // 标准异常响应格式：地址+功能码+异常码+CRC = 2+2+2+2 = 8字节
        if (frame.size() < 8) {
            writeCrashLog(QString("[parsePushButton5Response] 异常响应帧长度不足：%1字节（最少需要8字节）").arg(frame.size()));
            ui->lineEdit_7->setText("异常响应帧长度不足");
            return;
        }

        // 验证CRC
        QByteArray dataToCrc = frame.left(frame.size() - 2);
        uint16_t calcCrc = calcrc(dataToCrc);
        uint16_t recvCrc = (static_cast<unsigned char>(frame[frame.size()-1]) << 8) |
                           static_cast<unsigned char>(frame[frame.size()-2]);

        if (calcCrc != recvCrc) {
            writeCrashLog(QString("[parsePushButton5Response] 异常响应CRC校验失败：计算值=0x%1，接收值=0x%2")
                         .arg(calcCrc, 4, 16, QChar('0')).toUpper()
                         .arg(recvCrc, 4, 16, QChar('0')).toUpper());
            ui->lineEdit_7->setText("异常响应CRC校验失败");
            return;
        }

        // 提取异常码
        uint16_t errorCode = 0;
        if (type == "腕带") {
            // 腕带特殊异常格式：00 02 80 10 00 02 D0 1F
            // 异常码在偏移量4-5处（2字节）
            if (frame.size() >= 6) {
                errorCode = (static_cast<uint8_t>(frame[4]) << 8) |
                            static_cast<uint8_t>(frame[5]);
                writeCrashLog(QString("[parsePushButton5Response] 腕带特殊异常码提取：frame[4]=0x%1，frame[5]=0x%2，errorCode=0x%3")
                             .arg(static_cast<uint8_t>(frame[4]), 2, 16, QChar('0')).toUpper()
                             .arg(static_cast<uint8_t>(frame[5]), 2, 16, QChar('0')).toUpper()
                             .arg(errorCode, 4, 16, QChar('0')).toUpper());
            } else {
                writeCrashLog(QString("[parsePushButton5Response] 腕带异常响应帧长度不足：%1字节（无法提取异常码）").arg(frame.size()));
                ui->lineEdit_7->setText("腕带异常响应帧长度不足");
                return;
            }
        } else {
            // 标准异常格式：异常码在偏移量3处（1字节）
            if (frame.size() >= 4) {
                errorCode = static_cast<uint8_t>(frame[3]);
                writeCrashLog(QString("[parsePushButton5Response] 标准异常码提取：frame[3]=0x%1，errorCode=0x%2")
                             .arg(static_cast<uint8_t>(frame[3]), 2, 16, QChar('0')).toUpper()
                             .arg(errorCode, 2, 16, QChar('0')).toUpper());
            } else {
                writeCrashLog(QString("[parsePushButton5Response] 标准异常响应帧长度不足：%1字节（无法提取异常码）").arg(frame.size()));
                ui->lineEdit_7->setText("标准异常响应帧长度不足");
                return;
            }
        }

        QString errorDesc;
        switch (errorCode) {
            case 0x0001:
                errorDesc = "非法功能码";
                break;
            case 0x0002:
                errorDesc = "非法数据地址";
                break;
            case 0x0003:
                errorDesc = "非法数据值";
                break;
            case 0x0004:
                errorDesc = "从站设备故障";
                break;
            default:
                errorDesc = QString("未知错误码：0x%1").arg(errorCode, 4, 16, QChar('0')).toUpper();
                break;
        }

        QString errorText = QString("%1异常：%2").arg(type).arg(errorDesc);
        ui->lineEdit_7->setText(errorText);

        writeCrashLog(QString("[parsePushButton5Response] %1异常响应：错误码=0x%2，描述=%3")
                     .arg(type).arg(errorCode, 4, 16, QChar('0')).toUpper()
                     .arg(errorDesc));

        return;
    }

    // 验证帧长度（最小长度：地址+功能码+寄存器地址+数据+CRC = 2+2+2+2+2 = 10字节）
    // 对于腕带（写入响应）：地址+功能码+寄存器地址+数据+CRC = 2+2+2+2+2 = 10字节
    // 对于台垫/设备（读取线圈状态）：地址+功能码+字节数+数据+CRC = 2+2+1+2+2 = 9字节
    // 对于带前缀字节的台垫响应：前缀+地址+功能码+字节数+数据+CRC = 1+2+2+1+2+2 = 10字节
    int minFrameLen = (type == "腕带") ? 10 : 9;
    if (type == "台垫" && frameStart < btnRecvBuffer.size() && btnRecvBuffer.at(frameStart) == 0x00) {
        // 台垫响应有前缀字节00，最小长度增加1字节
        minFrameLen = 10;
    }

    if (frame.size() < minFrameLen) {
        writeCrashLog(QString("[parsePushButton5Response] 响应帧长度不足：%1字节（最少需要%2字节）").arg(frame.size()).arg(minFrameLen));
        ui->lineEdit_7->setText("响应帧长度不足");
        return;
    }

    // 验证CRC
    QByteArray dataToCrc;
    if (type == "台垫" && frame.size() > 0 && frame.at(0) == 0x00) {
        // 台垫响应有前缀字节00，CRC计算不包含前缀字节
        dataToCrc = frame.mid(1, frame.size() - 3); // 跳过前缀字节和CRC
        writeCrashLog("[parsePushButton5Response] 台垫响应CRC计算：跳过前缀字节00");
    } else {
        // 其他响应，CRC计算包含整个帧（除了CRC本身）
        dataToCrc = frame.left(frame.size() - 2);
    }

    uint16_t calcCrc = calcrc(dataToCrc);
    uint16_t recvCrc;

    if (type == "台垫" && frame.size() > 0 && frame.at(0) == 0x00) {
        // 台垫响应有前缀字节00，CRC在帧末尾
        recvCrc = (static_cast<unsigned char>(frame[frame.size()-1]) << 8) |
                  static_cast<unsigned char>(frame[frame.size()-2]);
    } else {
        // 其他响应，CRC在帧末尾
        recvCrc = (static_cast<unsigned char>(frame[frame.size()-1]) << 8) |
                  static_cast<unsigned char>(frame[frame.size()-2]);
    }

    if (calcCrc != recvCrc) {
        writeCrashLog(QString("[parsePushButton5Response] CRC校验失败：计算值=0x%1，接收值=0x%2")
                     .arg(calcCrc, 4, 16, QChar('0')).toUpper()
                     .arg(recvCrc, 4, 16, QChar('0')).toUpper());
        ui->lineEdit_7->setText("CRC校验失败");
        return;
    }

    // 记录完整的响应帧（用于调试）
    QString frameHex = frame.toHex().toUpper();
    writeCrashLog(QString("[parsePushButton5Response] 完整响应帧：%1").arg(frameHex));

    // 根据设备类型解析状态
    QString statusText;
    if (type == "腕带") {
        // 腕带工作模式响应格式：
        // 02 01 10 00 01 30 01 27 D9（单个通道）
        // 其中：02=地址，01 10=功能码，00 01=寄存器地址，30 01=工作模式数据，27 D9=CRC

        // 检查响应中的寄存器地址，确保与请求的通道匹配
        uint16_t respRegAddr = 0;
        if (frame.size() >= 6) {
            respRegAddr = (static_cast<uint8_t>(frame[4]) << 8) |
                         static_cast<uint8_t>(frame[5]);
            writeCrashLog(QString("[parsePushButton5Response] 腕带响应寄存器地址：0x%1（通道%2）")
                         .arg(respRegAddr, 4, 16, QChar('0')).toUpper()
                         .arg(respRegAddr + 1)); // 寄存器地址0000对应通道1，所以+1
        }

        // 检查是否是异常响应（数据在偏移量6-7处是FFFF）
        if (frame.size() >= 8) {
            uint16_t exceptionData = (static_cast<uint8_t>(frame[6]) << 8) |
                                    static_cast<uint8_t>(frame[7]);

            if (exceptionData == 0xFFFF) {
                // 这是异常响应
                statusText = QString("腕带通道%1异常响应：FF FF").arg(respRegAddr + 1);
                writeCrashLog(QString("[parsePushButton5Response] 检测到腕带异常响应：通道%1，异常数据=0x%2")
                             .arg(respRegAddr + 1)
                             .arg(exceptionData, 4, 16, QChar('0')).toUpper());
                ui->lineEdit_7->setText(statusText);
                return;
            }
        }

        // 正常响应：数据在偏移量6-7处（2字节），这是单个通道的工作模式数据
        if (frame.size() < 9) {
            writeCrashLog(QString("[parsePushButton5Response] 腕带响应帧长度不足：%1字节（最少需要9字节）").arg(frame.size()));
            ui->lineEdit_7->setText("腕带响应帧长度不足");
            return;
        }

        uint16_t statusData = (static_cast<uint8_t>(frame[6]) << 8) |
                             static_cast<uint8_t>(frame[7]);

        writeCrashLog(QString("[parsePushButton5Response] 腕带工作模式数据：0x%1（%2）")
                     .arg(statusData, 4, 16, QChar('0')).toUpper()
                     .arg(statusData));

        // 对于单个通道响应，直接解析工作模式（0、1、2、3）
        // 使用响应中的寄存器地址确定通道号，而不是用户输入的通道号
        int channelNum = respRegAddr + 1; // 寄存器地址0000对应通道1，所以+1
        QString workMode;

        // 根据完整的工作模式数据判断传感器状态
        // 3000 是内外都触发，2000 是外部触发，1000 是内部触发，0000 是都不触发
        if (statusData == 0x3000) {
            workMode = "内外传感器都可以触发";
        } else if (statusData == 0x2000) {
            workMode = "仅外传感器触发";
        } else if (statusData == 0x1000) {
            workMode = "仅内传感器触发";
        } else if (statusData == 0x0000) {
            workMode = "内外传感器都不触发";
        } else {
            // 对于其他值，按照原来的逻辑，取最左边的一位
            QString statusStr = QString::number(statusData, 16).toUpper();
            if (statusStr.length() > 1) {
                statusStr = statusStr.left(1); // 取最左边的一位
            }

            // 解析工作模式（根据最左边的一位）
            if (statusStr == "0") {
                workMode = "内外传感器都不触发";
            } else if (statusStr == "1") {
                workMode = "仅内传感器触发";
            } else if (statusStr == "2") {
                workMode = "仅外传感器触发";
            } else if (statusStr == "3") {
                workMode = "内外传感器都可以触发";
            } else {
                workMode = QString("未知模式(0x%1)").arg(statusData, 4, 16, QChar('0')).toUpper();
            }
        }

        statusText = QString("腕带第%1通道工作模式: %2").arg(channelNum).arg(workMode);

        writeCrashLog(QString("[parsePushButton5Response] 腕带通道%1工作模式解析：数据=0x%2")
                     .arg(channelNum)
                     .arg(statusData, 4, 16, QChar('0')).toUpper());
        writeCrashLog(QString("[parsePushButton5Response] 腕带工作模式：%1").arg(statusText));

    } else if (type == "台垫") {
        // 台垫响应格式可能是：
        // 1) 00 02 01 13 00 01 FF FF B7 A9 (带前缀字节的保持寄存器读取)
        // 2) 00 02 01 01 01 FF FF B7 A9 (带前缀字节的读取线圈状态)
        // 3) 02 01 13 00 01 FF FF B7 A9 (保持寄存器读取)
        // 4) 02 01 01 01 FF FF B7 A9 (读取线圈状态)

        // 检查是否有前缀字节00
        int dataOffset = 0;
        if (frame.size() > 0 && frame.at(0) == 0x00) {
            // 台垫响应有前缀字节00，数据偏移量增加1
            dataOffset = 1;
            writeCrashLog("[parsePushButton5Response] 台垫响应检测到前缀字节00，调整数据偏移量");
        }

        if (isAltResponse) {
            // 备用格式：保持寄存器读取响应
            // 完整格式：0002 0113 0001 1000 BBD9 或 02 01 13 00 01 10 00 BB D9
            // 数据在偏移量5和6处（无前缀）或6和7处（有前缀）
            uint16_t statusData = (static_cast<uint8_t>(frame[5+dataOffset]) << 8) |
                                 static_cast<uint8_t>(frame[6+dataOffset]);

            writeCrashLog(QString("[parsePushButton5Response] 台垫备用格式状态数据：0x%1（%2）")
                         .arg(statusData, 4, 16, QChar('0')).toUpper()
                         .arg(statusData));

            // 台垫工作模式解析（功能码0113H）
            // 1000表示开启，0000表示关闭
            QString workMode;
            if (statusData == 0x1000) {
                workMode = "开启";
            } else if (statusData == 0x0000) {
                workMode = "关闭";
            } else {
                workMode = QString("未知状态(0x%1)").arg(statusData, 4, 16, QChar('0')).toUpper();
            }
            statusText = QString("台垫工作模式: %1").arg(workMode);

        } else {
            // 主要格式：读取线圈状态响应：地址+功能码+字节数+数据+CRC
            uint8_t byteCount = static_cast<uint8_t>(frame[4+dataOffset]);
            writeCrashLog(QString("[parsePushButton5Response] 台垫响应数据字节数：%1").arg(byteCount));

            // 特殊处理：如果字节数为0x00，但后面有数据，可能是特殊格式的保持寄存器响应
            if (byteCount == 0x00 && frame.size() >= 9) {
                // 完整格式：0002 0113 0001 1000 BBD9 或 02 01 13 00 01 10 00 BB D9
                // 数据在偏移量5和6处（无前缀）或6和7处（有前缀）
                uint16_t statusData = (static_cast<uint8_t>(frame[5+dataOffset]) << 8) |
                                     static_cast<uint8_t>(frame[6+dataOffset]);

                writeCrashLog(QString("[parsePushButton5Response] 台垫特殊格式状态数据：0x%1（%2）")
                             .arg(statusData, 4, 16, QChar('0')).toUpper()
                             .arg(statusData));

                // 台垫工作模式解析（功能码0113H）
                // 1000表示开启，0000表示关闭
                QString workMode;
                if (statusData == 0x1000) {
                    workMode = "开启";
                } else if (statusData == 0x0000) {
                    workMode = "关闭";
                } else {
                    workMode = QString("未知状态(0x%1)").arg(statusData, 4, 16, QChar('0')).toUpper();
                }
                statusText = QString("台垫工作模式: %1").arg(workMode);
            } else if (byteCount < 2 || frame.size() < 7 + byteCount + dataOffset) {
                writeCrashLog(QString("[parsePushButton5Response] 台垫数据长度不足：字节数=%1，帧大小=%2")
                             .arg(byteCount).arg(frame.size()));
                ui->lineEdit_7->setText("数据长度不足");
                return;
            } else {
                // 提取状态数据（2字节，大端序），从偏移量5处提取（无前缀）或6处提取（有前缀）
                uint16_t statusData = (static_cast<uint8_t>(frame[5+dataOffset]) << 8) |
                                     static_cast<uint8_t>(frame[6+dataOffset]);

                writeCrashLog(QString("[parsePushButton5Response] 台垫状态数据：0x%1（%2）")
                             .arg(statusData, 4, 16, QChar('0')).toUpper()
                             .arg(statusData));

                // 台垫工作模式解析（功能码0113H）
                // 1000表示开启，0000表示关闭
                QString workMode;
                if (statusData == 0x1000) {
                    workMode = "开启";
                } else if (statusData == 0x0000) {
                    workMode = "关闭";
                } else {
                    workMode = QString("未知状态(0x%1)").arg(statusData, 4, 16, QChar('0')).toUpper();
                }
                statusText = QString("台垫工作模式: %1").arg(workMode);
            }
        }

        writeCrashLog(QString("[parsePushButton5Response] 台垫状态：%1").arg(statusText));

    } else if (type == "设备") {
        // 设备响应格式可能是：
        // 1) 02 01 20 00 01 FF FF B7 A9 (保持寄存器读取)
        // 2) 02 01 01 01 FF FF B7 A9 (读取线圈状态)

        if (isAltResponse) {
            // 备用格式：保持寄存器读取响应
            // 数据在偏移量4-5处（2字节）
            uint16_t statusData = (static_cast<uint8_t>(frame[4]) << 8) |
                                 static_cast<uint8_t>(frame[5]);

            writeCrashLog(QString("[parsePushButton5Response] 设备备用格式状态数据：0x%1（%2）")
                         .arg(statusData, 4, 16, QChar('0')).toUpper()
                         .arg(statusData));

            // 设备接地测试通道工作模式解析（功能码0120H）
            // 1000表示开启，0000表示关闭
            QString channelMode;
            if (statusData == 0x1000) {
                channelMode = "开启";
            } else if (statusData == 0x0000) {
                channelMode = "关闭";
            } else {
                channelMode = QString("未知状态(0x%1)").arg(statusData, 4, 16, QChar('0')).toUpper();
            }
            statusText = QString("设备接地测试通道模式: %1").arg(channelMode);

        } else {
            // 主要格式：读取线圈状态响应：地址+功能码+字节数+数据+CRC
            uint8_t byteCount = static_cast<uint8_t>(frame[4]);
            writeCrashLog(QString("[parsePushButton5Response] 设备响应数据字节数：%1").arg(byteCount));

            if (byteCount < 2 || frame.size() < 7 + byteCount) {
                writeCrashLog(QString("[parsePushButton5Response] 设备数据长度不足：字节数=%1，帧大小=%2")
                             .arg(byteCount).arg(frame.size()));
                ui->lineEdit_7->setText("数据长度不足");
                return;
            }

            // 提取状态数据（2字节，大端序）
            uint16_t statusData = (static_cast<uint8_t>(frame[5]) << 8) |
                                 static_cast<uint8_t>(frame[6]);

            writeCrashLog(QString("[parsePushButton5Response] 设备状态数据：0x%1（%2）")
                         .arg(statusData, 4, 16, QChar('0')).toUpper()
                         .arg(statusData));

            // 设备接地测试通道工作模式解析（功能码0120H）
            // 1000表示开启，0000表示关闭
            QString channelMode;
            if (statusData == 0x1000) {
                channelMode = "开启";
            } else if (statusData == 0x0000) {
                channelMode = "关闭";
            } else {
                channelMode = QString("未知状态(0x%1)").arg(statusData, 4, 16, QChar('0')).toUpper();
            }
            statusText = QString("设备接地测试通道模式: %1").arg(channelMode);
        }

        writeCrashLog(QString("[parsePushButton5Response] 设备状态：%1").arg(statusText));
    }

    // 将状态显示在lineEdit_7中
    ui->lineEdit_7->setText(statusText);
    writeCrashLog(QString("[parsePushButton5Response] 状态已显示到lineEdit_7：%1").arg(statusText));

    // 清理缓冲区，移除已处理的帧
    QMutexLocker cleanupLocker(&btnRecvBufferMutex);
    if (!btnRecvBuffer.isEmpty()) {
        // 查找并移除已处理的帧
        QByteArray frameHex = frame.toHex().toUpper();
        QByteArray bufferHex = btnRecvBuffer.toHex().toUpper();

        int framePos = bufferHex.indexOf(frameHex);
        if (framePos >= 0) {
            // 计算字节位置（每2个十六进制字符代表1个字节）
            int bytePos = framePos / 2;
            int frameByteLen = frame.size();

            writeCrashLog(QString("[parsePushButton5Response] 清理缓冲区：移除位置%1到%2的帧数据")
                         .arg(bytePos).arg(bytePos + frameByteLen));

            btnRecvBuffer = btnRecvBuffer.mid(bytePos + frameByteLen);
            btnCurrentExpectedAddrFunc.clear();

            writeCrashLog(QString("[parsePushButton5Response] 缓冲区清理完成，剩余%1字节")
                         .arg(btnRecvBuffer.size()));
        } else {
            writeCrashLog("[parsePushButton5Response] 未在缓冲区中找到匹配的帧，不清除缓冲区");
        }
    }
}

void MainWindow::onConnectionTypeChanged(int index)
{
    // 0: 串口通信, 1: 网络通信(Tcp Server)
    if (index == 0) {
        ui->label_serial_mode->show();
        ui->comboBox_serial_mode->show();
        ui->pushButton_refreshSerial->show();
        ui->label_2->show(); // 波特率
        ui->comboBox_2->show();
        updateSerialPortVisibility();

        ui->label_tcp_server_ip->hide();
        ui->lineEdit_tcp_server_ip->hide();
        ui->label_tcp_server_port->hide();
        ui->lineEdit_tcp_server_port->hide();
    } else {
        ui->label_serial_mode->hide();
        ui->comboBox_serial_mode->hide();
        ui->label->hide();
        ui->comboBox->hide();
        ui->pushButton_refreshSerial->hide();
        ui->label_17->hide();
        ui->comboBox_3->hide();
        ui->label_2->hide();
        ui->comboBox_2->hide();

        ui->label_tcp_server_ip->show();
        ui->lineEdit_tcp_server_ip->show();
        ui->label_tcp_server_port->show();
        ui->lineEdit_tcp_server_port->show();
    }
}

void MainWindow::onSerialModeChanged(int index)
{
    Q_UNUSED(index);
    if (ui->comboBox_connection_type->currentIndex() == 0) {
        updateSerialPortVisibility();
    }
}

void MainWindow::updateSerialPortVisibility()
{
    const bool dualSerial = ui->comboBox_serial_mode->currentIndex() == 1;

    ui->label_17->show();
    ui->comboBox_3->show();
    ui->label_17->setText(dualSerial ? QStringLiteral("通讯端口1") : QStringLiteral("COM口"));

    ui->label->setVisible(dualSerial);
    ui->comboBox->setVisible(dualSerial);
    if (dualSerial) {
        ui->label->setText(QStringLiteral("通讯端口2"));
    }
}

// 轮询周期完成处理
void MainWindow::onPollingCycleFinished()
{
    m_finishedWorkersCount++;

    if (m_finishedWorkersCount >= activePollWorkerCount()) {
        calculateAndInsertCombinedQualifiedRate();
        resetPollingStats();
        m_finishedWorkersCount = 0;
    }
}

// 重置轮询统计数据
void MainWindow::resetPollingStats()
{
    m_wTotalCount = 0;
    m_wQualifiedCount = 0;
    m_tTotalCount = 0;
    m_tQualifiedCount = 0;
    m_eTotalCount = 0;
    m_eQualifiedCount = 0;
    m_temperatureValues.clear();
    m_humidityValues.clear();
    m_cleanlinessValues.clear();
    m_workerRateDataMap.clear();
}

// 计算并插入合并后的合格率数据
void MainWindow::calculateAndInsertCombinedQualifiedRate()
{
    QDateTime currentTime = QDateTime::currentDateTime();
    
    // 从所有线程收集数据
    double wRate = 0.0, tRate = 0.0, eRate = 0.0;
    double avgTemp = 0.0, avgHumidity = 0.0, avgCleanliness = 0.0;
    
    // 合并合格率数据（取有效值）
    foreach (const WorkerRateData& data, m_workerRateDataMap.values()) {
        if (data.wRate > 0) wRate = data.wRate;
        if (data.tRate > 0) tRate = data.tRate;
        if (data.eRate > 0) eRate = data.eRate;
        if (data.avgTemp > 0) avgTemp = data.avgTemp;
        if (data.avgHumidity > 0) avgHumidity = data.avgHumidity;
        if (data.avgCleanliness > 0) avgCleanliness = data.avgCleanliness;
    }
    
    // 发送信号给DBManager插入合格率数据
    DBManager::instance()->handleQualifiedRateData(currentTime, wRate, tRate, eRate, avgTemp, avgHumidity, avgCleanliness);
}
//
