#include "pollconfig.h"

PollChannelRange parsePollChannelRange(const QString& config)
{
    PollChannelRange range;
    const QString text = config.trimmed();
    if (text.isEmpty()) {
        return range;
    }

    if (text.contains('-')) {
        const QStringList parts = text.split('-');
        if (parts.size() != 2) {
            return range;
        }
        bool startOk = false;
        bool endOk = false;
        const int startChannel = parts[0].toInt(&startOk);
        const int endChannel = parts[1].toInt(&endOk);
        if (!startOk || !endOk || startChannel < 1 || endChannel < startChannel) {
            return range;
        }
        range.startChannel = startChannel;
        range.endChannel = endChannel;
        range.startRegister = startChannel - 1;
        range.registerCount = endChannel - startChannel + 1;
        range.valid = true;
        return range;
    }

    if (text.contains('.')) {
        const QStringList parts = text.split('.');
        if (parts.size() != 2) {
            return range;
        }
        bool startOk = false;
        bool countOk = false;
        const int startRegister = parts[0].toInt(&startOk);
        const int registerCount = parts[1].toInt(&countOk);
        if (!startOk || !countOk || registerCount <= 0 || startRegister < 0) {
            return range;
        }
        range.startRegister = startRegister;
        range.registerCount = registerCount;
        range.startChannel = startRegister + 1;
        range.endChannel = startRegister + registerCount;
        range.valid = true;
    }

    return range;
}

QString makePollDeviceId(const QString& typePrefix, int modbusAddr, int channel)
{
    if (typePrefix == "C") {
        return "C" + QString::number(modbusAddr);
    }
    return typePrefix + QString::number(modbusAddr) + "-" + QString::number(channel);
}

QByteArray encodeModbusHexPayload(const QString& modbusHex)
{
    QByteArray sendData;
    for (int i = 0; i < modbusHex.length(); i += 2) {
        const QString byteStr = modbusHex.mid(i, 2);
        bool ok = false;
        const uint8_t byte = byteStr.toUInt(&ok, 16);
        if (!ok) {
            return QByteArray();
        }
        sendData.append(static_cast<char>(byte));
    }
    return sendData;
}

static bool fillPollRowInfo(int modbusAddr, int configCol, const QString& typePrefix,
                            const QString& configText, PollRowInfo& info)
{
    info = PollRowInfo();
    info.modbusAddr = modbusAddr;
    info.configCol = configCol;
    info.typePrefix = typePrefix;
    info.configText = configText;

    if (typePrefix == "C") {
        info.range.startChannel = 1;
        info.range.endChannel = 1;
        info.range.startRegister = 0;
        info.range.registerCount = 1;
        info.range.valid = true;
        info.valid = true;
        return true;
    }

    if (typePrefix == "I") {
        info.range = parsePollChannelRange(configText);
        if (!info.range.valid) {
            bool channelOk = false;
            const int channel = configText.toInt(&channelOk);
            if (!channelOk || channel < 1) {
                return false;
            }
            info.range.startChannel = channel;
            info.range.endChannel = channel;
            info.range.startRegister = channel - 1;
            info.range.registerCount = 1;
            info.range.valid = true;
        }
        info.valid = info.range.valid;
        return info.valid;
    }

    info.range = parsePollChannelRange(configText);
    info.valid = info.range.valid;
    return info.valid;
}

QVector<PollRowInfo> parsePollConfigEntries(const QStringList& row)
{
    QVector<PollRowInfo> entries;
    if (row.isEmpty()) {
        return entries;
    }

    bool addrOk = false;
    const int modbusAddr = row[0].toInt(&addrOk);
    if (!addrOk || modbusAddr <= 0) {
        return entries;
    }

    struct ConfigColumn {
        int col;
        QString prefix;
    };
    const ConfigColumn columns[] = {
        {1, "W"},
        {2, "T"},
        {3, "E"},
        {4, "C"},
        {5, "I"}
    };

    for (const ConfigColumn& column : columns) {
        if (row.size() <= column.col) {
            continue;
        }
        const QString value = row[column.col].trimmed();
        if (value.isEmpty()) {
            continue;
        }

        PollRowInfo info;
        if (fillPollRowInfo(modbusAddr, column.col, column.prefix, value, info)) {
            entries.append(info);
        }
    }

    return entries;
}

bool parsePollConfigRow(const QStringList& row, PollRowInfo& info)
{
    const QVector<PollRowInfo> entries = parsePollConfigEntries(row);
    if (entries.size() != 1) {
        info = PollRowInfo();
        return false;
    }
    info = entries.first();
    return true;
}

QVector<PollDeviceRef> expandPollConfigToDevices(const QVector<QStringList>& rows)
{
    QVector<PollDeviceRef> devices;
    for (const QStringList& row : rows) {
        const QVector<PollRowInfo> entries = parsePollConfigEntries(row);
        for (const PollRowInfo& info : entries) {
            if (info.typePrefix == "C") {
                PollDeviceRef ref;
                ref.typePrefix = "C";
                ref.deviceId = makePollDeviceId("C", info.modbusAddr);
                devices.append(ref);
            } else if (info.typePrefix == "I") {
                PollDeviceRef ref;
                ref.typePrefix = "I";
                ref.deviceId = makePollDeviceId("I", info.modbusAddr, info.range.startChannel);
                devices.append(ref);
            } else {
                for (int ch = info.range.startChannel; ch <= info.range.endChannel; ++ch) {
                    PollDeviceRef ref;
                    ref.typePrefix = info.typePrefix;
                    ref.deviceId = makePollDeviceId(info.typePrefix, info.modbusAddr, ch);
                    devices.append(ref);
                }
            }
        }
    }
    return devices;
}
