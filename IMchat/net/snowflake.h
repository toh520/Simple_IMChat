#ifndef SNOWFLAKE_H
#define SNOWFLAKE_H

#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>

class Snowflake {
public:
    static Snowflake& instance() {
        static Snowflake inst;
        return inst;
    }

    qint64 nextId() {
        QMutexLocker locker(&mutex_);
        qint64 timestamp = currentTimestamp();

        if (timestamp < lastTimestamp_) {
            // 时钟回拨处理：此时简单复用上次时间戳，以防生成重复 ID
            timestamp = lastTimestamp_;
        }

        if (timestamp == lastTimestamp_) {
            sequence_ = (sequence_ + 1) & 0xFFF; // 12 bit sequence
            if (sequence_ == 0) {
                timestamp = waitNextMillis(lastTimestamp_);
            }
        } else {
            sequence_ = 0;
        }

        lastTimestamp_ = timestamp;

        // 拼接：41位时间戳差值 | 10位机器ID | 12位序列号
        return ((timestamp - kEpoch) << 22) | (kWorkerId << 12) | sequence_;
    }

private:
    Snowflake() {}
    Snowflake(const Snowflake&) = delete;
    Snowflake& operator=(const Snowflake&) = delete;

    qint64 currentTimestamp() {
        return QDateTime::currentMSecsSinceEpoch();
    }

    qint64 waitNextMillis(qint64 last) {
        qint64 curr = currentTimestamp();
        while (curr <= last) {
            curr = currentTimestamp();
        }
        return curr;
    }

private:
    QMutex mutex_;
    qint64 lastTimestamp_{-1};
    int sequence_{0};

    static constexpr qint64 kEpoch = 1704067200000ULL; // 2024-01-01 00:00:00 UTC
    static constexpr int kWorkerId = 1;                 // 客户端机器ID，可固定为1
};

#endif // SNOWFLAKE_H
