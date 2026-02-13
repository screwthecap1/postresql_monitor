#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlQueryModel>
#include <QTime>
#include <QSettings>
#include <QStandardItemModel>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &MainWindow::refreshData);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_pushButton_clicked()
{
    QSettings settings("config.ini", QSettings::IniFormat);

    QSqlDatabase db = QSqlDatabase::addDatabase("QPSQL");
    db.setHostName(settings.value("db/host", "127.0.0.1").toString());
    db.setDatabaseName(settings.value("db/name", "postgres").toString());
    db.setUserName(settings.value("db/user", "postgres").toString());
    db.setPassword(settings.value("db/password", "").toString());
    this->sizeLimit = settings.value("settings/limit", 52428800).toLongLong();

    if (db.open()) {
        refreshData();

        updateTimer->start(5000);

        ui->pushButton->setText("Monitoring is started...");
        ui->pushButton->setEnabled(false);
    } else {
        QMessageBox::critical(this, "Error", db.lastError().text());
    }
}

void MainWindow::refreshData()
{
    if (QSqlDatabase::database().isOpen()) {

        QSqlQuery queryVer("SELECT VERSION()");
        if (queryVer.next()) {
            QString ver = queryVer.value(0).toString().section(' ', 0, 1);
            ui->labelVersion->setText("DBMS Version:\n" + ver);
        }

        QSqlQuery queryUptime("SELECT current_timestamp - pg_postmaster_start_time()");
        if (queryUptime.next()) {
            ui->labelUptime->setText("Uptime:\n" + queryUptime.value(0).toString().section('.', 0, 0));
        }

        QSqlQuery queryTotalSize("SELECT pg_size_pretty(sum(pg_database_size(datname))) FROM pg_database");
        if (queryTotalSize.next()) {
            ui->labelTotalSize->setText("Overall volume:\n" + queryTotalSize.value(0).toString());
        }

        QSqlQuery queryTotalConns("SELECT count(*) FROM pg_stat_activity");
        if (queryTotalConns.next()) {
            ui->labelTotalConns->setText("Total sessions:\n" + queryTotalConns.value(0).toString());
        }

        QSqlQuery query("SELECT d.datname, u.usename, "
                        "pg_size_pretty(pg_database_size(d.datname)), "
                        "d.datallowconn, "
                        "pg_database_size(d.datname), "
                        "(SELECT count(*) FROM pg_stat_activity WHERE datname = d.datname) "
                        "FROM pg_database d JOIN pg_user u ON d.datdba = u.usesysid");

        QStandardItemModel *model = new QStandardItemModel(this);

        model->setHorizontalHeaderLabels({"Base name", "Owner", "Size on disk", "Access", "Active Users"});

        while (query.next()) {
            QString name = query.value(0).toString();
            QString owner = query.value(1).toString();
            QString sizePretty = query.value(2).toString();
            QString access = query.value(3).toBool() ? "Allowed" : "Forbidden";
            QString activeUsers = query.value(5).toString();

            long long sizeInBytes = query.value(4).toLongLong();

            QList<QStandardItem*> rowItems;
            rowItems << new QStandardItem(name);
            rowItems << new QStandardItem(owner);
            rowItems << new QStandardItem(sizePretty);
            rowItems << new QStandardItem(access);
            rowItems << new QStandardItem(activeUsers);

            if (sizeInBytes > this->sizeLimit) { // 52428800 кб = 50 мб
                for (auto item : rowItems) {
                    item->setBackground(Qt::red);
                    item->setForeground(Qt::white);
                }
            }
            model->appendRow(rowItems);
        }

        ui->tableView->setModel(model);

        ui->tableView->resizeColumnsToContents();

        QString lastUpdate = "Last update: " + QTime::currentTime().toString();
        this->setWindowTitle(lastUpdate);
    }
}
