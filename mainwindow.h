#pragma once

#include <QMainWindow>
#include <QThread>
#include <QCheckBox>
#include <QMap>

#include "excelreader.h"
#include "chartmanager.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class LoadWorker : public QObject
{
    Q_OBJECT
public:
    explicit LoadWorker(const QString &path, QObject *parent = nullptr)
        : QObject(parent), m_path(path) {}
public slots:
    void run();
signals:
    void progress(int pct);
    void finished(BatteryData data, bool ok, QString error);
private:
    QString m_path;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onLoadFile();
    void onLoadFinished(BatteryData data, bool ok, QString error);
    void onPlotChart();
    void onPlotTab2();
    void onApplyScale();
    void onAutoScale();
    void onResetZoom();
    void onRangeChanged(AxisRange x, AxisRange yL, AxisRange yR);

private:
    void buildCheckBoxGroup(QWidget *container,
                            const QStringList &items,
                            QMap<QString, QCheckBox*> &checkMap);
    QStringList checkedItems(const QMap<QString, QCheckBox*> &map) const;
    void setSpinBoxesFromRanges(AxisRange x, AxisRange yL, AxisRange yR);

    Ui::MainWindow *ui;

    BatteryData  m_data;
    bool         m_dataLoaded  = false;
    bool         m_blockSpinSignals = false;  // prevent feedback loops

    ChartManager *m_chartManager = nullptr;

    QMap<QString, QCheckBox*> m_voltageChecks;
    QMap<QString, QCheckBox*> m_tempChecks;
    QMap<QString, QCheckBox*> m_paramChecks;

    QThread    *m_thread = nullptr;
    LoadWorker *m_worker = nullptr;

    static const QStringList s_voltageCols;
    static const QStringList s_tempCols;
    static const QStringList s_paramCols;
};
