#pragma once

#include <QObject>
#include <QWidget>
#include <QStringList>
#include <QColor>

#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QValueAxis>

#include "excelreader.h"

class ChartManager : public QObject
{
    Q_OBJECT
public:
    explicit ChartManager(QWidget *container, QObject *parent = nullptr);

    void plot(const QString     &xColumn,
              const QStringList &yColumns,
              const BatteryData &data);

    void showPlaceholder(const QString &msg = "Load a file and select columns to plot");

private:
    void setupChartView();
    void clearChart();

    QWidget    *m_container = nullptr;
    QChartView *m_chartView = nullptr;
    QChart     *m_chart     = nullptr;

    static const QList<QColor> s_palette;
};
