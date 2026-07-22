#ifndef ESDEDITPANEL_H
#define ESDEDITPANEL_H

#include <QWidget>
#include <QByteArray>
#include <QString>
#include <QLineEdit>
#include <QTimer>
#include <functional>

QT_BEGIN_NAMESPACE
namespace Ui { class EsdEditPanel; }
QT_END_NAMESPACE

class EsdEditPanel : public QWidget
{
    Q_OBJECT

public:
    using ConnectionChecker = std::function<bool()>;

    explicit EsdEditPanel(QWidget *parent = nullptr);
    ~EsdEditPanel();

    void setConnectionChecker(ConnectionChecker checker);
    void handleToolResponse(const QByteArray &frame, const QString &expectedFunc);
    void handleToolFailed(const QString &reason);

signals:
    void frameSendRequested(const QByteArray &frame, const QString &expectedFunc);

private slots:
    void on_applyButton_clicked();
    void on_queryButton_clicked();
    void on_setChannelButton_clicked();
    void on_typeComboBox_currentIndexChanged(int index);
    void on_applyChannelButton_clicked();
    void on_applyDelayButton_clicked();
    void on_readGroundButton_clicked();
    void on_applyGroundButton_clicked();
    void on_readTempButton_clicked();
    void on_applyTempButton_clicked();
    void on_readUpperLimitButton_clicked();
    void on_readLowerLimitButton_clicked();
    void on_applyUpperLimitButton_clicked();
    void on_applyLowerLimitButton_clicked();
    void on_readInternalResistanceButton_clicked();
    void on_applyInternalResistanceButton_clicked();
    void on_applyTimeButton_clicked();
    void on_syncPcTimeButton_clicked();

private:
    enum LimitReadTarget {
        LimitReadUpper = 0,
        LimitReadLower = 1,
        LimitReadInternalResistance = 2
    };

    Ui::EsdEditPanel *ui;
    QString currentExpectedAddrFunc;
    QString pendingSuccessMessage;
    int pendingLimitChannel;
    int pendingLimitRangeStart;
    int pendingLimitRangeCount;
    LimitReadTarget pendingLimitReadTarget;
    bool pendingGroundParamRead;
    bool pendingTempParamRead;
    QString pendingTimeModifyText;
    QTimer *m_timeModifyTimer = nullptr;
    ConnectionChecker m_connectionChecker;
    QLineEdit *channelUpperLimitEdits[8];

    bool checkConnection() const;
    void sendFrame(const QByteArray &frame, const QString &expectedFunc);
    void processValidFrame(const QByteArray &validFrame, const QString &expectedFunc);
    QString buildHexChannelField(const QString &channelText, int *outChannel = nullptr);
    QString buildChannelModbusCommand(const QString &type, const QString &funcCode,
                                      const QString &hexAddr, const QString &channelField,
                                      const QString &extraSuffix = QString());
    bool getChannelParams(int &address, int &channel);
    void getLimitFuncCodes(const QString &type, QString &readUpper, QString &readLower,
                           QString &writeUpper, QString &writeLower);
    QString buildLimitReadCommand(const QString &type, const QString &funcCode, int address, int channel);
    void sendLimitReadCommand(const QString &funcCode, const QString &logTag);
    void sendLimitWriteCommand(const QString &funcCode, double limitValue, const QString &logTag,
                               const QString &successMessage);
    QString formatLimitValue(const QString &dataHex);
    quint16 crc16(const QByteArray &data);
    QByteArray hexStringToByteArray(const QString &hex);
    QString byteArrayToHexString(const QByteArray &data);
    void notifyModifySuccess(const QString &message);
    void updateTriggerComboBox();
    void updateDeviceInternalResistanceVisibility();
    void initReferenceInfo();
    void initChannelUpperLimitDisplays();
    void updateApplyButtonState();
    bool isLimitReadFunc(const QString &func) const;
    void parseChannelRange(int &startChannel, int &channelCount);
    void parseLimitReadValues(const QByteArray &validFrame);
    void clearAllChannelValues();
    int getSharedDeviceAddress(bool &ok) const;
    void applyEsdEditPanelStyles();
    bool getSysParamAddress(int &address);
    QString getGroundRegisterAddr() const;
    QString getTempRegisterAddr() const;
    void sendSysParamCommand(int address, const QString &funcCode, const QString &registerAddr,
                             const QString &dataSuffix, const QString &logTag);
    QString encodeTimeRegisterValue(int value) const;
    QString formatTimeModifyText(int year, int month, int day, int weekday,
                                 int hour, int minute, int second) const;
    bool parseTimeModifyInputs(int &year, int &month, int &day, int &weekday,
                               int &hour, int &minute, int &second);
    void initTimeModifyFields();
    void fillTimeModifyFields(int year, int month, int day, int weekday,
                              int hour, int minute, int second);
    void sendTimeModifyCommand(int year, int month, int day, int weekday,
                               int hour, int minute, int second);
    void notifyTimeModifyResult(bool success);
};

#endif // ESDEDITPANEL_H
