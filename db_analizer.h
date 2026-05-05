#ifndef DB_ANALIZER_H
#define DB_ANALIZER_H

#include <QString>
#include <QVector>
#include <QDateTime>

struct MetricSnapshot {
    QDateTime timestamp;
    double tps;
    double cacheHit;
    qint64 fsyncs;
    int locks;
    double rollbackRate;
};

class DbAnalizer {
public:
    DbAnalizer() {
        maxHistorySize = 50;
    }

    void addSnapshot(double tps, double cache, qint64 fsync, int locks, double rb) {
        MetricSnapshot snap = {QDateTime::currentDateTime(), tps, cache, fsync, locks, rb};
        history.append(snap);
        if (history.size() > maxHistorySize) history.removeFirst();
    }

    QString getExpertConclusion() {
        if (history.size() < 2) return "Сбор данных для анализа...";

        MetricSnapshot current = history.last();
        MetricSnapshot previous = history.at(history.size() - 2);

        QStringList conclusions;

        if (current.tps < previous.tps * 0.7 && previous.tps > 0.5) {
            conclusions << "ВНИМАНИЕ: Зафиксировано резкое падение нагрузки (TPS).";
        }

        if (current.fsyncs > previous.fsyncs + 500) {
            conclusions << "КОНТЕКСТ: Рост задержек на уровне ОС/Диска (fsync). Оборудование не справляется с записью.";
        }

        if (current.cacheHit < 92.0) {
            if (current.tps > 0.1) {
                conclusions << "КОНТЕКСТ: Низкий Cache Hit при активной работе. Вероятны тяжелые JOIN или отсутствие индексов.";
            } else {
                conclusions << "КОНТЕКСТ: Низкий Cache Hit при низкой нагрузке. Недостаточный объем shared_buffers.";
            }
        }

        if (current.locks > 0) {
            conclusions << QString("КОНТЕКСТ: Причина тормозов — блокировки (%1 шт). Конфликт транзакций в приложении.").arg(current.locks);
        }

        if (current.rollbackRate > previous.rollbackRate + 2.0) {
            conclusions << "ТРЕНД: Рост процента откатов. Проблема в бизнес-логике софта или целостности данных.";
        }

        if (conclusions.isEmpty()) return "Состояние системы: Стабильное. Аномалий не выявлено.";

        return "АНАЛИЗ СОБЫТИЙ:\n• " + conclusions.join("\n• ");
    }

    QString getShortStatus() {
        if (history.isEmpty()) return "Инициализация...";
        MetricSnapshot s = history.last();
        if (s.cacheHit < 95.0 || s.locks > 0 || s.rollbackRate > 5.0) return "⚠️ ЕСТЬ ЗАМЕЧАНИЯ";
        return "НОРМА";
    }

private:
    QVector<MetricSnapshot> history;
    int maxHistorySize;
};

#endif
