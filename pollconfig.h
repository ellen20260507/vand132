#ifndef POLLCONFIG_H
#define POLLCONFIG_H

#include <QString>
#include <QStringList>
#include <QVector>

struct PollChannelRange {
    int startChannel = 0;
    int endChannel = 0;
    int startRegister = 0;
    int registerCount = 0;
    bool valid = false;
};

struct PollRowInfo {
    int modbusAddr = 0;
    QString typePrefix;
    int configCol = -1;
    QString configText;
    PollChannelRange range;
    bool valid = false;
};

struct PollDeviceRef {
    QString typePrefix;
    QString deviceId;
};

PollChannelRange parsePollChannelRange(const QString& config);
QString makePollDeviceId(const QString& typePrefix, int modbusAddr, int channel = 0);
QVector<PollRowInfo> parsePollConfigEntries(const QStringList& row);
QVector<PollDeviceRef> expandPollConfigToDevices(const QVector<QStringList>& rows);
bool parsePollConfigRow(const QStringList& row, PollRowInfo& info);
QByteArray encodeModbusHexPayload(const QString& modbusHex);

#endif
