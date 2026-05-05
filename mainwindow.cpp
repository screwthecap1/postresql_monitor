#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTime>
#include <QSettings>
#include <QStandardItemModel>
#include <QElapsedTimer>
#include <QTextStream>
#include <QDateTime>
#include <QFile>
#include <QDebug>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QStandardPaths>
#include <QProcess>
#include <QDir>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    this->setWindowTitle("Система мониторинга PostgreSQL");

    ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    ui->tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    ui->tableView->setMaximumSize(QSize(16777215, 16777215));

    ui->tableView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &MainWindow::refreshData);

    QString fullLegend = "ОПИСАНИЕ ЦВЕТОВОЙ ИНДИКАЦИИ:\n\n"
                             "● БЕЛЫЙ: База данных в пределах нормы.\n"
                             "● КРАСНЫЙ: Критический объем данных (превышен лимит в config.ini).\n"
                             "● ТЕКСТ ЗЕЛЕНЫЙ: Высокая эффективность кэша (>95%).\n"
                             "● ТЕКСТ ОРАНЖЕВЫЙ: Проблемы с производительностью (низкий кэш/высокие откаты).";

    ui->tableView->setToolTip(fullLegend);
    ui->statusbar->setToolTip(fullLegend);

    analizer = new DbAnalizer();

    ui->expertLog->setPlaceholderText("Здесь будет отображаться анализ событий...");
    ui->expertLog->setMaximumHeight(100);
    ui->expertLog->setStyleSheet("background-color: #f0f0f0; color: #333; font-family: Consolas;");

}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::on_pushButton_clicked()
{
    QString configPath = qApp->applicationDirPath() + "/config.ini";

    if (!QFile::exists(configPath)) {
        QString sourcePath = "/home/klimentiy/QTprojects/test/config.ini";
        if (QFile::exists(sourcePath)) {
            configPath = sourcePath;
        } else {
            QMessageBox::critical(this, "Ошибка",
                "Файл конфигурации не найден!\nПуть: " + qApp->applicationDirPath());
            return;
        }
    }

    QSettings settings(configPath, QSettings::IniFormat);

    QSqlDatabase db = QSqlDatabase::addDatabase("QPSQL");
    db.setHostName(settings.value("db/host", "127.0.0.1").toString());
    db.setDatabaseName(settings.value("db/name", "postgres").toString());
    db.setUserName(settings.value("db/user", "postgres").toString());
    db.setPassword(settings.value("db/password", "").toString());

    this->sizeLimit = settings.value("db/limit", 52428800).toLongLong();
    this->latencyBorder = settings.value("borders/max_latency", 150).toInt();
    this->cacheBorder = settings.value("borders/min_cache_hit_ratio", 95.0).toDouble();

    if (db.open()) {
            qDebug() << "Успешное подключение к БД!"; // Увидим в консоли Qt Creator
            refreshData();
            updateTimer->start(5000);
            ui->pushButton->setText("Мониторинг запущен...");
            ui->pushButton->setEnabled(false);
        } else {
            qDebug() << "ОШИБКА ПОДКЛЮЧЕНИЯ:" << db.lastError().text();
            qDebug() << "Доступные драйверы:" << QSqlDatabase::drivers();
            QMessageBox::critical(this, "Ошибка подключения",
                                 "Сообщение от системы: " + db.lastError().text());
        }

}

void MainWindow::on_exportButton_clicked()
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString folderPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString reportPath = folderPath + "/отчет_мониторинга_" + timestamp + ".txt";

    QFile file(reportPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);

        out.setCodec("UTF-8");
        out.setGenerateByteOrderMark(true);

        out << "==========================================\n";
        out << QString::fromUtf8("=== АНАЛИТИЧЕСКИЙ ОТЧЕТ МОНИТОРИНГА СУБД ===\n");
        out << "==========================================\n";
        out << QString::fromUtf8("Дата и время: ") << QDateTime::currentDateTime().toString("dd.MM.yyyy hh:mm:ss") << "\n\n";

        out << "--- " << QString::fromUtf8("ТЕКУЩИЕ МЕТРИКИ") << " ---\n";
        out << QString::fromUtf8("Версия СУБД: ") << ui->labelVersion->text().section('\n', 1).trimmed() << "\n";
        out << QString::fromUtf8("Общий объем: ") << ui->labelTotalSize->text().section('\n', 1).trimmed() << "\n";
        out << QString::fromUtf8("Эффективность кэша: ") << ui->labelCache->text().section('\n', 1).trimmed() << "\n";
        out << QString::fromUtf8("Текущая нагрузка: ") << ui->labelTPS->text() << "\n\n";

        out << "--- " << QString::fromUtf8("ЭКСПЕРТНОЕ ЗАКЛЮЧЕНИЕ") << " ---\n";
        out << analizer->getExpertConclusion() << "\n";

        out << "==========================================\n";
        out << QString::fromUtf8("Отчет сформирован автоматически системой мониторинга.\n");

        file.close();

        ui->statusbar->showMessage(QString::fromUtf8("Отчет сохранен в Документы"), 5000);
        QDesktopServices::openUrl(QUrl::fromLocalFile(reportPath));

    } else {
        QMessageBox::critical(this, QString::fromUtf8("Ошибка"),
            QString::fromUtf8("Не удалось создать файл отчета!"));
    }
}

void MainWindow::on_badgerButton_clicked()
{
    QString configPath = qApp->applicationDirPath() + "/config.ini";
    if (!QFile::exists(configPath)) {
        configPath = "/home/klimentiy/QTprojects/test/config.ini";
    }
    QSettings settings(configPath, QSettings::IniFormat);
    QString currentUser = settings.value("db/user", "postgres").toString();

    QStringList allowedDatabases;
    QSqlQuery query;
    QString checkSql = QString(
        "SELECT datname FROM pg_database "
        "WHERE datistemplate = false "
        "AND has_database_privilege('%1', datname, 'CONNECT')").arg(currentUser);

    if (query.exec(checkSql)) {
        while (query.next()) {
            allowedDatabases << query.value(0).toString();
        }
    }

    if (allowedDatabases.isEmpty()) {
        ui->statusbar->showMessage("Нет доступных баз для анализа!");
        return;
    }

    QString logPath = "/var/log/postgresql/postgresql-9.6-main.log";
    QString outputPath = qApp->applicationDirPath() + "/report_badger.html";
    QString pgbadgerPath = "/usr/local/bin/pgbadger";

    QStringList args;
    args << "-f" << "stderr";
    args << "--prefix" << "%t [%p]: [%l-1] user=%u,db=%d,app=%a,client=%h ";

    for (const QString &dbName : allowedDatabases) {
        args << "-d" << dbName;
    }

    args << "-j" << "4";

    args << logPath << "-o" << outputPath;

    ui->statusbar->showMessage("Запуск pgBadger (фильтрация по правам)...");

    QProcess *process = new QProcess(this);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [=](int exitCode){
        if (exitCode == 0 && QFile::exists(outputPath)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(outputPath));
            ui->statusbar->showMessage("Глубокий отчет готов!");
        } else {
            QString errorOutput = process->readAllStandardError();
            // Теперь здесь не будет кода 3 из-за diplom_restricted
            QMessageBox::critical(this, "Ошибка выполнения",
                "Код выхода: " + QString::number(exitCode) + "\n" +
                "Ошибка системы: " + errorOutput);
        }
    });

    process->start(pgbadgerPath, args);
}

void MainWindow::on_launchButton_clicked()
{
    QString scriptPath = qApp->applicationDirPath() + "/load_manager.sh";

    if (!QFile::exists(scriptPath)) {
        scriptPath = "/home/klimentiy/QTprojects/test/load_manager.sh";
    }

    if (!QFile::exists(scriptPath)) {
        QMessageBox::warning(this, "Ошибка", "Файл не найден: " + scriptPath);
        return;
    }

    QString terminal = "x-terminal-emulator";
    QStringList arguments;
    arguments << "-e" << "bash" << "-c" << scriptPath + "; exec bash";

    bool success = QProcess::startDetached(terminal, arguments);

    if (success) {
        ui->statusbar->showMessage("Генератор запущен", 3000);
    } else {
        QMessageBox::critical(this, "Ошибка", "Не удалось запустить терминал");
    }
}

void MainWindow::logEvent(QString message, QString context)
{
    QString logPath = qApp->applicationDirPath() + "/events_log.log";
    QFile logFile(logPath);

    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&logFile);
        out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")
            << " | " << message;
        if (!context.isEmpty()) {
            out << " | КОНТЕКСТ: " << context;
        }
        out << "\n";
        logFile.close();
    }
}

void MainWindow::refreshData()
{
    QString configPath = qApp->applicationDirPath() + "/config.ini";
    if (!QFile::exists(configPath)) {
        configPath = "/home/klimentiy/QTprojects/test/config.ini";
    }

    QSettings settings(configPath, QSettings::IniFormat);

    if (QSqlDatabase::database().isOpen()) {
        QElapsedTimer timer;
        timer.start();

        double currentHitRatio = 100.0;
        double currentTps = 0.0;
        int currentLocks = 0;
        qint64 currentFsyncs = 0;
        double currentRbRate = 0.0;
        int currentConns = 0;

        QSqlQuery queryCache("SELECT COALESCE(round(100.0 * sum(heap_blks_hit) / (NULLIF(sum(heap_blks_hit) + sum(heap_blks_read), 0)), 2), 100.0) FROM pg_statio_user_tables");
        if (queryCache.next()) {
            currentHitRatio = queryCache.value(0).toDouble();
            ui->labelCache->setText("Эффективность кэша:\n" + QString::number(currentHitRatio) + "%");
            if (currentHitRatio < this->cacheBorder) {
                ui->labelCache->setStyleSheet("color: #E67E22; font-weight: bold;");
            } else {
                ui->labelCache->setStyleSheet("color: #27AE60;");
            }
        }

        static qint64 prevXacts = 0;
        QSqlQuery queryTPS("SELECT sum(xact_commit + xact_rollback) FROM pg_stat_database");
        if (queryTPS.next()) {
            qint64 currentXacts = queryTPS.value(0).toLongLong();
            if (prevXacts > 0) {
                currentTps = (currentXacts - prevXacts) / 5.0;
                ui->labelTPS->setText(QString("Нагрузка (TPS): %1").arg(currentTps));
            }
            prevXacts = currentXacts;
        }

        QSqlQuery queryRB("SELECT round(100.0 * sum(xact_rollback) / NULLIF(sum(xact_commit + xact_rollback), 0), 2) FROM pg_stat_database");
        if (queryRB.next()) {
            currentRbRate = queryRB.value(0).toDouble();
            ui->labelRollback->setText(QString("Откаты (RB): %1%").arg(currentRbRate));
            if (currentRbRate > 5.0) ui->labelRollback->setStyleSheet("color: red;");
            else ui->labelRollback->setStyleSheet("");
        }

        QSqlQuery queryLocks("SELECT count(*) FROM pg_locks WHERE granted = false");
        if (queryLocks.next()) {
            currentLocks = queryLocks.value(0).toInt();
        }

        QSqlQuery queryBg("SELECT buffers_backend_fsync FROM pg_stat_bgwriter");
        if (queryBg.next()) {
            currentFsyncs = queryBg.value(0).toLongLong();
            ui->labelFsync->setText("Синхр. диска: " + QString::number(currentFsyncs));
        }

        QSqlQuery queryTotalConns("SELECT count(*) FROM pg_stat_activity");
        if (queryTotalConns.next()) {
            currentConns = queryTotalConns.value(0).toInt();
            ui->labelTotalConns->setText("Всего сессий:\n" + QString::number(currentConns));
        }

        QString context = QString("TPS: %1 | Кэш: %2% | Блок-ки: %3 | Fsync: %4 | RB: %5% | Сессии: %6")
                          .arg(currentTps).arg(currentHitRatio).arg(currentLocks).arg(currentFsyncs).arg(currentRbRate).arg(currentConns);

        if (currentHitRatio < this->cacheBorder && currentTps > 1.0) {
            logEvent("ПРОБЛЕМА_ПРОИЗВОДИТЕЛЬНОСТИ: Вероятен дефицит индексов или тяжелые JOIN", context);
            ui->statusbar->showMessage("Внимание: Проверьте индексы и SQL-запросы", 5000);
        }

        static double lastTps = 0;
        if (currentLocks > 0 && currentTps < (lastTps * 0.7) && lastTps > 0) {
            logEvent("КРИТИЧЕСКАЯ_БЛОКИРОВКА: Падение TPS из-за блокировок таблиц", context);
            ui->statusbar->showMessage("Критично: Обнаружены блокировки таблиц!", 5000);
        }
        lastTps = currentTps;

        if (currentFsyncs > 1000 && currentHitRatio >= this->cacheBorder) {
            logEvent("ПРОБЛЕМА_РЕСУРСОВ: Высокая задержка записи на диск (fsync)", context);
        }

        if (currentRbRate > 10.0) {
            logEvent("ОШИБКА_ПРИЛОЖЕНИЯ: Аномально высокий процент откатов транзакций", context);
        }

        QSqlQuery queryVer("SELECT VERSION()");
        if (queryVer.next()) ui->labelVersion->setText("Версия СУБД:\n" + queryVer.value(0).toString().section(' ', 0, 1));

        QSqlQuery queryUptime("SELECT current_timestamp - pg_postmaster_start_time()");
        if (queryUptime.next()) ui->labelUptime->setText("Время работы:\n" + queryUptime.value(0).toString().section('.', 0, 0));

        QString currentUser = settings.value("db/user", "postgres").toString();

        QSqlQuery queryTotalSize;
        QString totalSizeSql = QString(
            "SELECT pg_size_pretty(sum(pg_database_size(datname))) "
            "FROM pg_database "
            "WHERE has_database_privilege('%1', datname, 'CONNECT')").arg(currentUser);

        if (queryTotalSize.exec(totalSizeSql) && queryTotalSize.next()) {
            ui->labelTotalSize->setText("Общий объем:\n" + queryTotalSize.value(0).toString());
        }

        QSqlQuery queryList;
        QString sql = QString(
            "SELECT d.datname, u.usename, "
            "CASE WHEN has_database_privilege('%1', d.datname, 'CONNECT') "
            "     THEN pg_size_pretty(pg_database_size(d.datname)) "
            "     ELSE 'Нет доступа' END, "
            "has_database_privilege('%1', d.datname, 'CONNECT'), "
            "CASE WHEN has_database_privilege('%1', d.datname, 'CONNECT') "
            "     THEN pg_database_size(d.datname) "
            "     ELSE 0 END, "
            "(SELECT count(*) FROM pg_stat_activity WHERE datname = d.datname) "
            "FROM pg_database d JOIN pg_user u ON d.datdba = u.usesysid "
            "WHERE d.datistemplate = false;").arg(currentUser);

        if (!queryList.exec(sql)) {
            qDebug() << "ОШИБКА SQL (Список баз):" << queryList.lastError().text();
        }

        if (ui->tableView->model()) delete ui->tableView->model();
        QStandardItemModel *model = new QStandardItemModel(this);
        model->setHorizontalHeaderLabels({"Имя базы", "Владелец", "Размер", "Доступ", "Сессии"});

        while (queryList.next()) {
            QList<QStandardItem*> row;
            QString dbName = queryList.value(0).toString();
            QString owner = queryList.value(1).toString();
            QString sizeStr = queryList.value(2).toString();
            bool isAllowed = queryList.value(3).toBool();
            qint64 sizeBytes = queryList.value(4).toLongLong();
            QString sessions = queryList.value(5).toString();

            row << new QStandardItem(dbName);
            row << new QStandardItem(owner);
            row << new QStandardItem(sizeStr);
            row << new QStandardItem(isAllowed ? "Разрешен" : "Запрещен");
            row << new QStandardItem(sessions);

            if (!isAllowed) {
                for (auto item : row) {
                    item->setBackground(QColor(220, 220, 220));
                    item->setForeground(Qt::darkGray);
                }
            }

            if (isAllowed && sizeBytes > this->sizeLimit) {
                for (auto item : row) {
                    item->setBackground(Qt::red);
                    item->setForeground(Qt::white);
                }
            }

            model->appendRow(row);
        }

        ui->tableView->setModel(model);
        ui->tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

        qint64 elapsed = timer.elapsed();
        this->setWindowTitle("Система мониторинга PostgreSQL");

        QString statusMessage = QString("Статус: Мониторинг активен | Отклик: %1 мс | Сессий: %2 | Нагрузка: %3 TPS")
                                    .arg(elapsed)
                                    .arg(currentConns)
                                    .arg(currentTps, 0, 'f', 1);

        ui->statusbar->showMessage(statusMessage);

        if (elapsed > this->latencyBorder) {
            ui->statusbar->setStyleSheet("color: #C0392B; font-weight: bold; background-color: #FDEDEC;");
        } else {
            ui->statusbar->setStyleSheet("color: #2C3E50;");
        }

        analizer->addSnapshot(currentTps, currentHitRatio, currentFsyncs, currentLocks, currentRbRate);
        QString expertVerdict = analizer->getExpertConclusion();
        ui->expertLog->setText(expertVerdict);

        if (expertVerdict.contains("КОНТЕКСТ") || expertVerdict.contains("ВНИМАНИЕ")) {
            ui->expertLog->setStyleSheet("background-color: #FDEDEC; color: #7B241C; border: 1px solid red;");
        } else {
            ui->expertLog->setStyleSheet("background-color: #EAFAF1; color: #145A32;");
        }

        ui->statusbar->showMessage(statusMessage + " | " + analizer->getShortStatus());
    }
}

