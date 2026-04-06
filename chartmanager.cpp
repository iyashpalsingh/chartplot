#include "chartmanager.h"

#include <QVBoxLayout>
#include <QPainter>
#include <QtCharts/QLineSeries>
#include <QtCharts/QLegend>
#include <QtCharts/QLegendMarker>
#include <cmath>
#include <limits>

const QList<QColor> ChartManager::s_palette = {
    {0x1f,0x77,0xb4},{0xff,0x7f,0x0e},{0x2c,0xa0,0x2c},{0xd6,0x27,0x28},
    {0x94,0x67,0xbd},{0x8c,0x56,0x4b},{0xe3,0x77,0xc2},{0x7f,0x7f,0x7f},
    {0xbc,0xbd,0x22},{0x17,0xbe,0xcf},{0xae,0xc7,0xe8},{0xff,0xbb,0x78},
    {0x98,0xdf,0x8a},{0xff,0x99,0x96},{0xc5,0xb0,0xd5},{0xc4,0x9c,0x94}
};

ChartManager::ChartManager(QWidget *container, QObject *parent)
    : QObject(parent), m_container(container)
{
    setupChartView();
}

void ChartManager::setupChartView()
{
    QVBoxLayout *layout = new QVBoxLayout(m_container);
    layout->setContentsMargins(0, 0, 0, 0);

    m_chart     = new QChart();
    m_chartView = new QChartView(m_chart, m_container);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setRubberBand(QChartView::RectangleRubberBand);

    layout->addWidget(m_chartView);
    m_container->setLayout(layout);

    showPlaceholder();
}

void ChartManager::clearChart()
{
    m_chart->removeAllSeries();
    const auto axes = m_chart->axes();
    for (QAbstractAxis *ax : axes)
        m_chart->removeAxis(ax);
}

void ChartManager::showPlaceholder(const QString &msg)
{
    clearChart();
    m_chart->setTitle(msg);
    m_chart->legend()->hide();
}

void ChartManager::plot(const QString     &xColumn,
                        const QStringList &yColumns,
                        const BatteryData &data)
{
    clearChart();

    if (yColumns.isEmpty()) {
        showPlaceholder("No Y-axis columns selected");
        return;
    }

    static const QStringList paramCols = {"Current","Capacity","SOC"};

    QStringList leftCols, rightCols;
    for (const QString &col : yColumns) {
        if (paramCols.contains(col)) rightCols.append(col);
        else                          leftCols.append(col);
    }

    bool dualAxis = !leftCols.isEmpty() && !rightCols.isEmpty();

    bool xIsNumeric = data.isNumeric(xColumn);
    const QVector<double> &xNum = xIsNumeric ? data.numeric.value(xColumn)
                                             : QVector<double>{};
    int N = data.rowCount;

    // Build axes
    QValueAxis *axisX  = new QValueAxis();
    QValueAxis *axisYL = new QValueAxis();
    QValueAxis *axisYR = dualAxis ? new QValueAxis() : nullptr;

    axisX->setTitleText(xColumn);
    axisX->setLabelFormat("%.1f");
    axisYL->setTitleText(dualAxis ? "Voltage / Temperature" : "Value");
    axisYL->setLabelFormat("%.3f");
    if (axisYR) {
        axisYR->setTitleText("Parameters");
        axisYR->setLabelFormat("%.2f");
    }

    m_chart->addAxis(axisX,  Qt::AlignBottom);
    m_chart->addAxis(axisYL, Qt::AlignLeft);
    if (axisYR)
        m_chart->addAxis(axisYR, Qt::AlignRight);

    // Initialise axis ranges to something sensible before first data
    double xMinAll =  std::numeric_limits<double>::max();
    double xMaxAll = -std::numeric_limits<double>::max();

    auto addSeries = [&](const QString &col, int colorIdx, QValueAxis *yAxis) {
        if (!data.isNumeric(col)) return;
        const QVector<double> &yVals = data.numeric.value(col);

        QLineSeries *series = new QLineSeries();
        series->setName(col);
        QPen pen(s_palette.at(colorIdx % s_palette.size()));
        pen.setWidthF(1.5);
        series->setPen(pen);

        double yMin =  std::numeric_limits<double>::max();
        double yMax = -std::numeric_limits<double>::max();

        for (int i = 0; i < N && i < yVals.size(); ++i) {
            double y = yVals[i];
            if (std::isnan(y)) continue;
            double x = (xIsNumeric && i < xNum.size() && !std::isnan(xNum[i]))
                       ? xNum[i] : static_cast<double>(i);
            series->append(x, y);
            xMinAll = qMin(xMinAll, x);
            xMaxAll = qMax(xMaxAll, x);
            yMin    = qMin(yMin, y);
            yMax    = qMax(yMax, y);
        }

        m_chart->addSeries(series);
        series->attachAxis(axisX);
        series->attachAxis(yAxis);

        if (yMin < yMax) {
            double pad = (yMax - yMin) * 0.05;
            if (yAxis->min() == yAxis->max()) {
                yAxis->setRange(yMin - pad, yMax + pad);
            } else {
                yAxis->setMin(qMin(yAxis->min(), yMin - pad));
                yAxis->setMax(qMax(yAxis->max(), yMax + pad));
            }
        }
    };

    // Reset before plotting
    axisYL->setRange(0, 1);
    if (axisYR) axisYR->setRange(0, 1);

    int idx = 0;
    for (const QString &col : leftCols)  addSeries(col, idx++, axisYL);
    for (const QString &col : rightCols) addSeries(col, idx++, axisYR ? axisYR : axisYL);

    if (xMinAll < xMaxAll)
        axisX->setRange(xMinAll, xMaxAll);
    else
        axisX->setRange(0, 1);

    m_chart->setTitle("Battery Data");
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);

    // Click legend to toggle series
    for (QLegendMarker *marker : m_chart->legend()->markers()) {
        QObject::connect(marker, &QLegendMarker::clicked, this, [marker]() {
            QAbstractSeries *s = marker->series();
            s->setVisible(!s->isVisible());
            marker->setVisible(true);
            qreal alpha = s->isVisible() ? 1.0 : 0.5;
            QColor c = marker->labelBrush().color();
            c.setAlphaF(alpha);
            marker->setLabelBrush(QBrush(c));
            marker->setBrush(QBrush(c));
        });
    }
}
