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
#include <QDir>
#include <QPdfWriter>
#include <QPainter>
#include <QFileDialog>
#include <QTextDocument>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>

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

    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, &QNetworkAccessManager::finished, this, &MainWindow::onAiReplyFinished);

    QString fullLegend = "ОПИСАНИЕ ЦВЕТОВОЙ ИНДИКАЦИИ:\n\n"
                         "● БЕЛЫЙ: База данных в пределах нормы.\n"
                         "● СЕРЫЙ: Доступ к базе запрещен (datallowconn = false).\n"
                         "● КРАСНЫЙ: Критический объем данных (превышен лимит).\n"
                         "● ТЕКСТ ЗЕЛЕНЫЙ: Высокая эффективность кэша.\n"
                         "● ТЕКСТ ОРАНЖЕВЫЙ: Проблемы с производительностью.";

    ui->tableView->setToolTip(fullLegend);
    ui->statusbar->setToolTip(fullLegend);

    analizer = new DbAnalizer();

    ui->expertLog->setPlaceholderText("Здесь будет отображаться анализ событий...");
    ui->expertLog->setMaximumHeight(100);
    ui->expertLog->setStyleSheet("background-color: #f0f0f0; color: #333; font-family: Consolas;");


    ui->barRead->setStyleSheet(
                "QProgressBar { border: 1px solid #bdc3c7; border-radius: 4px; text-align: center; font-weight: bold; color: #2c3e50; }"
                "QProgressBar::chunk { background-color: #3498db; border-radius: 3px; }"
                );
    ui->barWrite->setStyleSheet(
                "QProgressBar { border: 1px solid #bdc3c7; border-radius: 4px; text-align: center; font-weight: bold; color: #2c3e50; }"
                "QProgressBar::chunk { background-color: #e74c3c; border-radius: 3px; }"
                );
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_pushButton_clicked()
{
    QString configPath = qApp->applicationDirPath() + "/config.ini";

    if (!QFile::exists(configPath)) {
        QString sourcePath = "/home/klimentiy/QTprojects/test/config.ini";
        if (QFile::exists(sourcePath)) {
            configPath = sourcePath;
        } else {
            QMessageBox::critical(this, "Ошибка", "Файл конфигурации не найден!\nПуть: " + qApp->applicationDirPath());
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
        qDebug() << "Успешное подключение к БД!";
        refreshData();
        updateTimer->start(5000);
        ui->pushButton->setText("Мониторинг запущен...");
        ui->pushButton->setEnabled(false);
    } else {
        qDebug() << "ОШИБКА ПОДКЛЮЧЕНИЯ:" << db.lastError().text();
        qDebug() << "Доступные драйверы:" << QSqlDatabase::drivers();
        QMessageBox::critical(this, "Ошибка подключения", "Сообщение от системы: " + db.lastError().text());
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
        QMessageBox::critical(this, QString::fromUtf8("Ошибка"), QString::fromUtf8("Не удалось создать файл отчета!"));
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
            QMessageBox::critical(this, "Ошибка выполнения", "Код выхода: " + QString::number(exitCode) + "\n" + "Ошибка системы: " + errorOutput);
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
        out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << " | " << message;
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

        static qint64 prevReturned = 0;
        static qint64 prevModified = 0;

        QSqlQuery queryProfile(
            "SELECT sum(tup_returned), "
            "sum(tup_inserted + tup_updated + tup_deleted) "
            "FROM pg_stat_database"
        );

        if (queryProfile.next()) {
            qint64 currentReturned = queryProfile.value(0).toLongLong();
            qint64 currentModified = queryProfile.value(1).toLongLong();

            if (prevReturned > 0) {
                qint64 deltaRead = currentReturned - prevReturned;
                qint64 deltaWrite = currentModified - prevModified;
                qint64 totalOperations = deltaRead + deltaWrite;

                if (totalOperations > 0) {
                    int readPct = static_cast<int>((deltaRead * 100) / totalOperations);
                    int writePct = static_cast<int>((deltaWrite * 100) / totalOperations);

                    ui->barRead->setValue(readPct);
                    ui->barWrite->setValue(writePct);
                } else {
                    ui->barRead->setValue(0);
                    ui->barWrite->setValue(0);
                }
            }

            prevReturned = currentReturned;
            prevModified = currentModified;
        }

        analizer->addSnapshot(currentTps, currentHitRatio, currentFsyncs, currentLocks, currentRbRate);
        QString expertVerdict = analizer->getExpertConclusion();
        ui->expertLog->setText(expertVerdict);

        if (expertVerdict.contains("КОНТЕКСТ") || expertVerdict.contains("ВНИМАНИЕ")) {
            ui->expertLog->setStyleSheet("background-color: #FDEDEC; color: #7B241C; border: 1px solid red;");
        } else {
            ui->expertLog->setStyleSheet("background-color: #EAFAF1; color: #145A32;");
        }

        qint64 elapsed = timer.elapsed();
        this->setWindowTitle("Система мониторинга PostgreSQL");

        QString statusMessage = QString("Статус: Мониторинг активен | Отклик: %1 мс | Сессий: %2 | Нагрузка: %3 TPS")
                                    .arg(elapsed)
                                    .arg(currentConns)
                                    .arg(currentTps, 0, 'f', 1);

        ui->statusbar->showMessage(statusMessage + " | " + analizer->getShortStatus());

        if (elapsed > this->latencyBorder) {
            ui->statusbar->setStyleSheet("color: #C0392B; font-weight: bold; background-color: #FDEDEC;");
        } else {
            ui->statusbar->setStyleSheet("color: #2C3E50;");
        }
    }
}

void MainWindow::on_exportToPdfButton_clicked()
{
    if (!QSqlDatabase::database().isOpen()) {
        QMessageBox::warning(this, "Внимание", "Нет активного подключения к СУБД для выгрузки метрик!");
        return;
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString pdfPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Расширенный_отчет_" + timestamp + ".pdf";

    QString v_version   = ui->labelVersion->text().section('\n', 1).trimmed();
    QString v_size      = ui->labelTotalSize->text().section('\n', 1).trimmed();
    QString v_cache     = ui->labelCache->text().section('\n', 1).trimmed();
    QString v_tps       = ui->labelTPS->text().section(':', 1).trimmed();
    QString v_rollback  = ui->labelRollback->text().section(':', 1).trimmed();

    if (v_version.isEmpty()) v_version = "PostgreSQL (Astra Linux SE)";
    if (v_tps.isEmpty()) v_tps = "0.0";
    if (v_rollback.isEmpty()) v_rollback = "0.0%";

    int activeConnections = 0;
    int waitingLocks = 0;
    qint64 tempFilesBytes = 0;
    QString sharedBuffersConfig = "Неизвестно";

    QSqlQuery sqlQuery;

    if (sqlQuery.exec("SELECT count(*) FROM pg_stat_activity WHERE state = 'active'")) {
        if (sqlQuery.next()) activeConnections = sqlQuery.value(0).toInt();
    }

    if (sqlQuery.exec("SELECT count(*) FROM pg_locks WHERE granted = false")) {
        if (sqlQuery.next()) waitingLocks = sqlQuery.value(0).toInt();
    }

    if (sqlQuery.exec("SELECT sum(temp_bytes) FROM pg_stat_database")) {
        if (sqlQuery.next()) tempFilesBytes = sqlQuery.value(0).toLongLong();
    }
    double tempFilesMb = static_cast<double>(tempFilesBytes) / (1024.0 * 1024.0);

    if (sqlQuery.exec("SHOW shared_buffers")) {
        if (sqlQuery.next()) sharedBuffersConfig = sqlQuery.value(0).toString();
    }

    QString htmlContent = QString(
        "<html><head><style>"
        "body { font-family: 'Liberation Sans', Arial, sans-serif; margin: 10px; color: #2C3E50; line-height: 1.3; }"
        ".header { text-align: center; border-bottom: 3px solid #2980B9; padding-bottom: 8px; margin-bottom: 15px; }"
        "h1 { color: #2980B9; font-size: 20px; margin: 0; text-transform: uppercase; }"
        ".company { font-size: 12px; color: #7F8C8D; margin-top: 4px; font-weight: bold; }"
        ".date { color: #95A5A6; font-size: 11px; margin-top: 2px; }"
        "h3 { color: #2C3E50; border-left: 4px solid #2980B9; padding-left: 6px; margin-top: 15px; margin-bottom: 5px; font-size: 13px; text-transform: uppercase; }"
        ".metric-table { width: 100%; border-collapse: collapse; margin-top: 5px; font-size: 12px; }"
        ".metric-table th, .metric-table td { border: 1px solid #BDC3C7; padding: 6px; text-align: left; }"
        ".metric-table th { background-color: #F2F4F4; color: #34495E; font-weight: bold; }"
        ".verdict-box { background-color: #EAFAF1; border: 1px solid #27AE60; border-left: 6px solid #27AE60; "
        "                padding: 10px; margin-top: 8px; font-size: 12px; border-radius: 4px; white-space: pre-wrap; }"
        "</style></head><body>"
        "<div class='header'>"
        "  <h1>Технический отчет комплексного monitoring СУБД</h1>"
        "  <div class='company'>Подсистема контроля транзакционной активности</div>"
        "  <div class='date'>Дата и время генерации пакета: %1</div>"
        "</div>"
        "<h3>1. Идентификация целевой среды</h3>"
        "<table class='metric-table'>"
        "  <tr><td style='width: 40%%;'>Версия ядра СУБД</td><td><b>%2</b></td></tr>"
        "  <tr><td>Контролируемый объем дискового пространства баз</td><td>%3</td></tr>"
        "  <tr><td>Выделенный объем shared_buffers (конфигурация)</td><td><b>%4</b></td></tr>"
        "</table>"
        "<h3>2. Метрики эффективности подсистемы памяти и транзакций</h3>"
        "<table class='metric-table'>"
        "  <tr><th style='width: 40%%;'>Контролируемый параметр</th><th>Текущее значение</th><th>Статус</th></tr>"
        "  <tr><td>Эффективность кэширования строк (Cache Hit Ratio)</td><td>%5</td><td>Штатно</td></tr>"
        "  <tr><td>Интенсивность транзакционной нагрузки (TPS)</td><td>%6 вызовов/сек</td><td>Под нагрузкой</td></tr>"
        "  <tr><td>Коэффициент аварийных откатов (Rollback Rate)</td><td>%7</td><td>Штатно</td></tr>"
        "</table>"
        "<h3>3. Метрики параллелизма и дисковой подсистемы (I/O)</h3>"
        "<table class='metric-table'>"
        "  <tr><th style='width: 40%%;'>Параметр параллелизма / диска</th><th>Текущее значение</th><th>Влияние на производительность</th></tr>"
        "  <tr><td>Активные сессии (Active Connections)</td><td>%8 параллельных процессов</td><td>Низкое</td></tr>"
        "  <tr><td>Взаимные блокировки транзакций (Deadlocks/Locks)</td><td><span style='color: %9;'><b>%10 шт.</b></span></td><td>%11</td></tr>"
        "  <tr><td>Объем генерации Temp Files на диске</td><td>%12 Мб</td><td>%13</td></tr>"
        "</table>"
        "<h3>4. Автоматическое экспертное заключение</h3>"
        "<div class='verdict-box'>"
        "  %14"
        "</div>"
        "</body></html>"
    )
    .arg(QDateTime::currentDateTime().toString("dd.MM.yyyy hh:mm:ss"))
    .arg(v_version)
    .arg(v_size)
    .arg(sharedBuffersConfig)
    .arg(v_cache)
    .arg(v_tps)
    .arg(v_rollback)
    .arg(activeConnections)
    .arg(waitingLocks > 0 ? "#C0392B" : "#27AE60")
    .arg(waitingLocks)
    .arg(waitingLocks > 0 ? "КРИТИЧЕСКОЕ: Требуется оптимизация логики приложений" : "Отсутствует")
    .arg(QString::number(tempFilesMb, 'f', 2))
    .arg(tempFilesMb > 50.0 ? "ВНИМАНИЕ: Рекомендуется увеличить work_mem" : "Оптимально")
    .arg(analizer->getExpertConclusion().replace("\n", "<br>"));

    QPdfWriter pdfWriter(pdfPath);
    pdfWriter.setPageSize(QPdfWriter::A4);
    pdfWriter.setPageMargins(QMarginsF(10, 10, 10, 10));
    pdfWriter.setResolution(96);

    QTextDocument doc;
    doc.setTextWidth(pdfWriter.width());
    doc.setHtml(htmlContent);
    doc.print(&pdfWriter);

    ui->statusbar->showMessage("Отчет успешно сохранен и открыт: " + pdfPath, 5000);
    QDesktopServices::openUrl(QUrl::fromLocalFile(pdfPath));
}

void MainWindow::on_aiAnalysisButton_clicked()
{
    if (!networkManager) {
        networkManager = new QNetworkAccessManager(this);
        connect(networkManager, &QNetworkAccessManager::finished, this, &MainWindow::onAiReplyFinished);
    }

    QString badgerReportPath = qApp->applicationDirPath() + "/report_badger.html";

    if (!QFile::exists(badgerReportPath)) {
        QMessageBox::warning(this, "Анализ невозможен", "Сначала необходимо сгенерировать базовый отчет pgBadger!");
        return;
    }

    QFile file(badgerReportPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Ошибка", "Не удалось открыть отчет pgBadger для анализа.");
        return;
    }

    QTextStream in(&file);
    QString htmlContent = in.readAll();
    file.close();

    ui->statusbar->showMessage("Экстракция признаков из pgBadger и подготовка промпта...");

    QString extractedContext = "Данные из pgBadger:\n";
    if (htmlContent.contains("slowest", Qt::CaseInsensitive)) extractedContext += "- Обнаружены медленные SQL-запросы (Slowest queries)\n";
    if (htmlContent.contains("deadlock", Qt::CaseInsensitive)) extractedContext += "- Зафиксированы критические блокировки (Deadlocks/Locks waiting)\n";
    if (htmlContent.contains("checkpoint", Qt::CaseInsensitive)) extractedContext += "- Высокая частота контрольных точек (Checkpoints)\n";
    if (htmlContent.contains("fatal", Qt::CaseInsensitive)) extractedContext += "- Ошибки в логах: FATAL/ERROR\n";

    QString prompt = QString(
        "Ты — ведущий инженер СУБД PostgreSQL на операционной системе Astra Linux. "
        "Проанализируй следующие агрегированные результаты утилиты pgBadger и сформируй "
        "краткое экспертное заключение и конкретные рекомендации по тюнингу конфигурационного файла postgresql.conf.\n\n"
        "Входные данные отчета:\n%1\n"
        "Ответь строго на русском языке, профессионально, кратко и по пунктам."
    ).arg(extractedContext);

    QJsonObject json;
    json["model"] = "llama3:latest";
    json["prompt"] = prompt;
    json["stream"] = false;

    QJsonDocument doc(json);
    QByteArray data = doc.toJson();

    QUrl url("http://127.0.0.1:11434/api/generate");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    ui->statusbar->showMessage("Ожидание ответа от локальной LLM-модели...");
    ui->expertLog->setText("ИИ думает... Пожалуйста, подождите.");
    ui->expertLog->setStyleSheet("background-color: #FCF3CF; color: #7E5109; font-family: Consolas;");

    ui->aiAnalysisButton->setEnabled(false);
    networkManager->post(request, data);
}

void MainWindow::onAiReplyFinished(QNetworkReply *reply)
{
    ui->statusbar->showMessage("Мониторинг активен");
    ui->aiAnalysisButton->setEnabled(true);

    if (!reply) return;

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseBytes = reply->readAll();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(responseBytes);
        QJsonObject jsonObj = jsonDoc.object();

        QString result = jsonObj["response"].toString();

        ui->expertLog->setStyleSheet("background-color: #f0f0f0; color: #333; font-family: Consolas;");
        ui->expertLog->setText("Анализ успешно завершен. Отчет открыт в отдельном окне.");

        QDialog *reportDialog = new QDialog(this);
        reportDialog->setWindowTitle("Экспертное заключение ИИ (Llama 3)");
        reportDialog->setMinimumSize(750, 550);

        QTextEdit *textEdit = new QTextEdit(reportDialog);
        textEdit->setReadOnly(true);

        QString htmlFormatted = result;
        QRegularExpression rx("\\*\\*(.*?)\\*\\*");
        htmlFormatted.replace(rx, "<b>\\1</b>");
        htmlFormatted.replace("\n", "<br>");

        textEdit->setHtml(htmlFormatted);

        textEdit->setStyleSheet(
            "QTextEdit {"
            "   font-family: 'Consolas', 'Courier New', monospace;"
            "   font-size: 11pt;"
            "   color: #222222;"
            "   background-color: #ffffff;"
            "   border: 1px solid #ccc;"
            "   padding: 10px;"
            "}"
        );

        QPushButton *closeButton = new QPushButton("Понятно", reportDialog);
        closeButton->setStyleSheet("padding: 6px 20px; font-weight: bold;");
        connect(closeButton, &QPushButton::clicked, reportDialog, &QDialog::accept);

        QVBoxLayout *mainLayout = new QVBoxLayout(reportDialog);
        mainLayout->addWidget(textEdit);

        QHBoxLayout *buttonLayout = new QHBoxLayout();
        buttonLayout->addStretch();
        buttonLayout->addWidget(closeButton);

        mainLayout->addLayout(buttonLayout);
        reportDialog->setLayout(mainLayout);

        reportDialog->exec();
        reportDialog->deleteLater();
    } else {
        ui->expertLog->setStyleSheet("background-color: #ffcccc; color: #cc0000;");
        ui->expertLog->setText("Ошибка сети при получении анализа от ИИ.");
        QMessageBox::critical(this, "Ошибка сети", "Не удалось получить ответ от локальной модели Ollama: " + reply->errorString());
    }

    reply->deleteLater();
}
