#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTextEdit>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QProgressBar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "db_analizer.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_pushButton_clicked();
    void  refreshData();
    void logEvent(QString message, QString context = "");
    void on_exportButton_clicked();
    void on_badgerButton_clicked();
    void on_launchButton_clicked();
    void on_exportToPdfButton_clicked();
    void on_aiAnalysisButton_clicked();
    void onAiReplyFinished(QNetworkReply *reply);

private:
    Ui::MainWindow *ui;
    QTimer *updateTimer;
    long long sizeLimit;
    int latencyBorder;
    double cacheBorder;
    DbAnalizer *analizer;
    QNetworkAccessManager *networkManager;
};
#endif // MAINWINDOW_H
