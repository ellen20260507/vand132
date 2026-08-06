#include "placement_new_global.h"
#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QFile>
#include <QCoreApplication>
#include "httplistener.h"
#include "myrequesthandler.h"
#include "mainwindow.h"
#include "uistyle.h"

int main(int argc, char *argv[])
{
    try {
        QApplication app(argc, argv);
        const QString appDisplayName = QStringLiteral("静电管理在线监控系统 ESD-1000.V1.0");
        app.setApplicationName(QStringLiteral("ESD-1000"));
        app.setApplicationDisplayName(appDisplayName);
        // 设置应用程序图标
        app.setWindowIcon(QIcon(":/symbol/3HESD.png"));
        installMessageBoxTuner();
        // 设置插件路径，确保Qt能够找到SQL驱动
        QString appDir = QCoreApplication::applicationDirPath();
        QString pluginPath = appDir + "/plugins";
        QCoreApplication::addLibraryPath(pluginPath);
        qDebug() << "程序启动，当前工作目录：" << QDir::currentPath();
        qDebug() << "插件路径:" << pluginPath;

        // 1. 创建MainWindow（内部包含requestHandler�?
        MainWindow mainWindow;
        mainWindow.show();

        // 2. 关键修改：获取exe所在目录，拼接 server.ini 路径（exe同目录）
        QString exeDir = QCoreApplication::applicationDirPath(); // exe所在目录（无论放哪台机器都正确�?
        QString iniPath = exeDir + "/server.ini"; // 拼接：exe同级目录下的 server.ini

        // 打印日志：方便排查ini文件路径是否正确（命令行可见�?
        qDebug() << "正在查找服务器配置文件：" << iniPath;

        // 检�?server.ini 是否存在（不存在直接提示错误，避免默默失败）
        if (!QFile::exists(iniPath)) {
            qCritical() << "[致命错误] 找不到server.ini 配置文件!";
            qCritical() << "请将 server.ini 放到以下目录:" << exeDir;
            return 1; // 配置文件缺失，直接退出程序（避免HTTP服务启动失败�?
        }

        // 3. 读取ini配置（从exe同目录读取）
        QSettings* settings = new QSettings(iniPath, QSettings::IniFormat);
        settings->beginGroup("Server");

        // 4. 启动HTTP服务器（关联MainWindow的requestHandler�?
        stefanfrings::HttpListener* listener = new stefanfrings::HttpListener(settings, mainWindow.requestHandler, &app);


        // 6. 打印启动成功日志（命令行可见，确认配置生效）
        int port = settings->value("port", 8080).toInt(); // 端口（默�?080，ini中可配置�?
        QString host = settings->value("host", "0.0.0.0").toString(); // 监听地址（默�?.0.0.0�?
        qDebug() << "=====================================";
        qDebug() << "HTTP服务器启动成功！";
        qDebug() << "监听地址:" << host;
        qDebug() << "监听端口:" << port;
        qDebug() << "本地访问：http://localhost:" << port;
        qDebug() << "局域网访问：http://本机IP:" << port;
        qDebug() << "=====================================";

        int result = app.exec();
        
        // 7. 释放资源，确保程序退出时正确释放端口和数据库连接
        delete listener;
        delete settings;
        
        // 8. 清理数据库资源（关闭数据库连接）
        QSqlDatabase db = QSqlDatabase::database("dbManagerConnection", false);
        if (db.isOpen()) {
            db.close();
        }
        
        // 9. 移除数据库连接，确保资源完全释放
        QSqlDatabase::removeDatabase("dbManagerConnection");
        
        return result;
    } catch (const std::exception& e) {
        qCritical() << "捕获到C++异常:" << e.what();
        QMessageBox::critical(nullptr, "程序崩溃", QString("程序遇到未处理的异常:%1").arg(e.what()));
        return 1;
    } catch (...) {
        qCritical() << "捕获到未知异常!";
        QMessageBox::critical(nullptr, "程序崩溃", "程序遇到未知异常，已停止运行");
        return 1;
    }
}
