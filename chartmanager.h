#pragma once

#include <QObject>
#include <QWidget>
#include <QStringList>
#include <QColor>
#include <QPointF>

#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLineSeries>

#include "excelreader.h"

// ─────────────────────────────────────────────────────────────────────────────
// Axis range info — returned after plotting so UI can populate spin boxes
// ─────────────────────────────────────────────────────────────────────────────
struct AxisRange {
    double min = 0.0;
    double max = 1.0;
};

// ─────────────────────────────────────────────────────────────────────────────
// ChartView with mouse-wheel zoom and middle-button pan
// ─────────────────────────────────────────────────────────────────────────────
class InteractiveChartView : public QChartView
{
    Q_OBJECT
public:
    explicit InteractiveChartView(QChart *chart, QWidget *parent = nullptr);

signals:
    void axisRangeChanged();   // emitted after zoom / pan so spinboxes can sync

protected:
    void wheelEvent      (QWheelEvent   *event) override;
    void mousePressEvent (QMouseEvent   *event) override;
    void mouseMoveEvent  (QMouseEvent   *event) override;
    void mouseReleaseEvent(QMouseEvent  *event) override;
    void keyPressEvent   (QKeyEvent     *event) override;

private:
    bool   m_panning  = false;
    QPoint m_lastPan;
};

// ─────────────────────────────────────────────────────────────────────────────
// ChartManager
// ─────────────────────────────────────────────────────────────────────────────
class ChartManager : public QObject
{
    Q_OBJECT
public:
    explicit ChartManager(QWidget *container, QObject *parent = nullptr);

    void plot(const QString     &xColumn,
              const QStringList &yColumns,
              const BatteryData &data);

    void showPlaceholder(const QString &msg = "Load a file and select columns to plot");

    // Scale control — called from UI spinboxes
    void setXRange (double min, double max);
    void setYLRange(double min, double max);
    void setYRRange(double min, double max);
    void autoScale();
    void resetZoom();

    // Getters so UI can read current axis values
    AxisRange xRange()  const;
    AxisRange yLRange() const;
    AxisRange yRRange() const;
    bool hasDualAxis()  const { return m_axisYR != nullptr; }

signals:
    void rangeChanged(AxisRange x, AxisRange yL, AxisRange yR);

private slots:
    void onViewRangeChanged();

private:
    void setupChartView();
    void clearChart();

    QWidget              *m_container  = nullptr;
    InteractiveChartView *m_chartView  = nullptr;
    QChart               *m_chart      = nullptr;

    QValueAxis *m_axisX  = nullptr;
    QValueAxis *m_axisYL = nullptr;
    QValueAxis *m_axisYR = nullptr;

    // Full-data ranges (for auto-scale / reset)
    AxisRange m_fullX, m_fullYL, m_fullYR;

    static const QList<QColor> s_palette;
};
