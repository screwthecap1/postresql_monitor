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
        QSqlQuery query("SELECT datname, usename, pg_size_pretty(pg_database_size(datname)), "
                        "datallowconn, pg_database_size(datname) "
                        "FROM pg_database JOIN pg_user ON datdba=usesysid");

        QStandardItemModel *model = new QStandardItemModel(this);

        model->setHorizontalHeaderLabels({"Base name", "Owner", "Size on disk", "Access"});

        while (query.next()) {
            QString name = query.value(0).toString();
            QString owner = query.value(1).toString();
            QString sizePretty = query.value(2).toString();
            QString access = query.value(3).toBool() ? "Allowed" : "Forbidden";

            long long sizeInBytes = query.value(4).toLongLong();

            QList<QStandardItem*> rowItems;
            rowItems << new QStandardItem(name);
            rowItems << new QStandardItem(owner);
            rowItems << new QStandardItem(sizePretty);
            rowItems << new QStandardItem(access);

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
