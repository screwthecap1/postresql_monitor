#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlQueryModel>
#include <QTime>
#include <QSettings>

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
        QSqlQueryModel *model = new QSqlQueryModel();
        model->setQuery("SELECT datname, usename, pg_size_pretty(pg_database_size(datname)), datallowconn FROM pg_database JOIN pg_user ON datdba = usesysid");

        ui->tableView->setModel(model);

        model->setHeaderData(0, Qt::Horizontal, "Base name");
        model->setHeaderData(1, Qt::Horizontal, "Owner");
        model->setHeaderData(2, Qt::Horizontal, "Size on disk");
        model->setHeaderData(3, Qt::Horizontal, "Access");

        ui->tableView->resizeColumnsToContents();

        QString lastUpdate = "Последнее обновление: " + QTime::currentTime().toString();
        this->setWindowTitle(lastUpdate);
    }
}
