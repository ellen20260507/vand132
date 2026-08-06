#ifndef MYREQUESTHANDLER_H
#define MYREQUESTHANDLER_H

#include <QObject>
#include <QJsonArray>
#include <QMutex>
#include <QSettings>
#include <QRunnable>
#include <QDateTime>
#include "httpglobal.h"
#include "httprequesthandler.h"
#include "staticfilecontroller.h"
using namespace stefanfrings;

class MyRequestHandler : public HttpRequestHandler
{
    Q_OBJECT
public:
    explicit MyRequestHandler(QObject* parent = nullptr);
    void service(HttpRequest& request, HttpResponse& response) override;
    void generateDeviceStatusPage(HttpResponse& response);
    void generateHistoryChartPage(HttpResponse& response);
    void generateHistoryChartData(HttpResponse& response, const QString& timeRange);
    QString generateHistoryChartDataJson(const QString& timeRange);
    QString generateQualifiedRateDataJson(const QString& timeRange);
    QString generateWQualifiedRateDataJson(const QString& timeRange);
    QString generateTQualifiedRateDataJson(const QString& timeRange);
    QString generateEQualifiedRateDataJson(const QString& timeRange);
    QString generateTempDataJson(const QString& timeRange);
    QString generateHumidityDataJson(const QString& timeRange);
    QString generateCleanlinessDataJson(const QString& timeRange);
    void notifyThemeChange(const QString& theme); // 通知主题变化
    void notifyDisplayModeChange(bool separateEnvEsd); // 分离/合并 环境与ESD 显示
    bool isSeparateEnvEsd() const { return m_separateEnvEsd; }
    virtual ~MyRequestHandler();

signals:
    void envHistoryDataReady(const QJsonArray& timeArray, const QJsonArray& tempArray, const QJsonArray& humidityArray, const QJsonArray& cleanlinessArray);

private slots:
    void onEnvHistoryDataReady(const QJsonArray& timeArray, const QJsonArray& tempArray, const QJsonArray& humidityArray, const QJsonArray& cleanlinessArray);

private:
    QString appDir;
    QJsonArray allPoints; // 存储所有黄点数据
    QString yellowTxtPath;
    QString redTxtPath;
    QString backgroundImagePath;
    QString currentTheme; // 当前主题
    bool m_separateEnvEsd = true; // true=按主题分开展示，false=统一布局
    StaticFileController* staticFileController; // 静态文件控制器
    QSettings* staticSettings; // 静态文件控制器设置
    // 异步响应数据结构
    struct AsyncResponseData {
        HttpResponse* response;
        QDateTime start;
        QDateTime end;
    };
    
    QList<AsyncResponseData> m_pendingResponses; // 待处理的响应列表
    
    // 异步响应处理函数
    void handleAsyncResponse(HttpResponse* response, const QJsonArray& timeArray, const QJsonArray& tempArray, const QJsonArray& humidityArray, const QJsonArray& cleanlinessArray);
    
    bool loadPointsFromCSV(const QString& filePath); // 从CSV加载黄点数据
    QString generateHistoryChartPage(); // 生成历史折线图页面
    QPair<QDateTime, QDateTime> calculateTimeRange(const QString& timeRange, bool useFixedDayRange); // 计算时间范围
    
    // 异步查询环境数据的工作线程类
    class EnvDataQueryTask : public QRunnable {
    public:
        EnvDataQueryTask(MyRequestHandler* handler, const QDateTime& start, const QDateTime& end, int interval);
        void run() override;
    private:
        MyRequestHandler* m_handler;
        QDateTime m_start;
        QDateTime m_end;
        int m_interval;
    };
    
    void handleMesReadingsQuery(HttpRequest& request, HttpResponse& response);

    QMutex m_mutex;
};

#endif // MYREQUESTHANDLER_H
