#include "chartmanager.h"

#include <QVBoxLayout>
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>

#include <QtCharts/QLineSeries>
#include <QtCharts/QLegend>
#include <QtCharts/QLegendMarker>

#include <cmath>
#include <limits>

// ── Colour palette ────────────────────────────────────────────────────────────
const QList<QColor> ChartManager::s_palette = {
    {0x1f,0x77,0xb4},{0xff,0x7f,0x0e},{0x2c,0xa0,0x2c},{0xd6,0x27,0x28},
    {0x94,0x67,0xbd},{0x8c,0x56,0x4b},{0xe3,0x77,0xc2},{0x7f,0x7f,0x7f},
    {0xbc,0xbd,0x22},{0x17,0xbe,0xcf},{0xae,0xc7,0xe8},{0xff,0xbb,0x78},
    {0x98,0xdf,0x8a},{0xff,0x99,0x96},{0xc5,0xb0,0xd5},{0xc4,0x9c,0x94}
};

// ═══════════════════════════════════════════════════════════════════════════════
// InteractiveChartView
// ═══════════════════════════════════════════════════════════════════════════════
InteractiveChartView::InteractiveChartView(QChart *chart, QWidget *parent)
    : QChartView(chart, parent)
{
    setRubberBand(QChartView::RectangleRubberBand);
    setDragMode(QGraphicsView::NoDrag);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void InteractiveChartView::wheelEvent(QWheelEvent *event)
{
    // Zoom in/out centred on cursor position
    double factor = (event->angleDelta().y() > 0) ? 0.85 : 1.0 / 0.85;
    chart()->zoom(factor);
    emit axisRangeChanged();
    event->accept();
}

void InteractiveChartView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton &&
         event->modifiers() & Qt::AltModifier))
    {
        m_panning = true;
        m_lastPan = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QChartView::mousePressEvent(event);
}

void InteractiveChartView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        QPoint delta = event->pos() - m_lastPan;
        chart()->scroll(-delta.x(), delta.y());
        m_lastPan = event->pos();
        emit axisRangeChanged();
        event->accept();
        return;
    }
    QChartView::mouseMoveEvent(event);
}

void InteractiveChartView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        emit axisRangeChanged();
        event->accept();
        return;
    }
    // After rubber-band zoom
    QChartView::mouseReleaseEvent(event);
    emit axisRangeChanged();
}

void InteractiveChartView::keyPressEvent(QKeyEvent *event)
{
    // Arrow keys pan, +/- zoom
    switch (event->key()) {
    case Qt::Key_Left:  chart()->scroll(-20, 0); break;
    case Qt::Key_Right: chart()->scroll( 20, 0); break;
    case Qt::Key_Up:    chart()->scroll(0,  20); break;
    case Qt::Key_Down:  chart()->scroll(0, -20); break;
    case Qt::Key_Plus:
    case Qt::Key_Equal: chart()->zoom(0.8);      break;
    case Qt::Key_Minus: chart()->zoom(1.0/0.8);  break;
    default: QChartView::keyPressEvent(event); return;
    }
    emit axisRangeChanged();
}

// ═══════════════════════════════════════════════════════════════════════════════
// ChartManager
// ═══════════════════════════════════════════════════════════════════════════════
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
    m_chartView = new InteractiveChartView(m_chart, m_container);
    m_chartView->setRenderHint(QPainter::Antialiasing);

    layout->addWidget(m_chartView);
    m_container->setLayout(layout);

    connect(m_chartView, &InteractiveChartView::axisRangeChanged,
            this, &ChartManager::onViewRangeChanged);

    showPlaceholder();
}

void ChartManager::clearChart()
{
    m_chart->removeAllSeries();
    const auto axes = m_chart->axes();
    for (QAbstractAxis *ax : axes)
        m_chart->removeAxis(ax);
    m_axisX  = nullptr;
    m_axisYL = nullptr;
    m_axisYR = nullptr;
}

void ChartManager::showPlaceholder(const QString &msg)
{
    clearChart();
    m_chart->setTitle(msg);
    m_chart->legend()->hide();
}

// ─────────────────────────────────────────────────────────────────────────────
// Main plot
// ─────────────────────────────────────────────────────────────────────────────
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

    bool dualAxis    = !leftCols.isEmpty() && !rightCols.isEmpty();
    bool xIsNumeric  = data.isNumeric(xColumn);
    const QVector<double> &xNum = xIsNumeric ? data.numeric.value(xColumn)
                                             : QVector<double>{};
    int N = data.rowCount;

    // Build axes
    m_axisX  = new QValueAxis();
    m_axisYL = new QValueAxis();
    m_axisYR = dualAxis ? new QValueAxis() : nullptr;

    m_axisX->setTitleText(xColumn);
    m_axisX->setLabelFormat("%.2f");
    m_axisX->setTickCount(10);
    m_axisYL->setTitleText(dualAxis ? "Voltage / Temperature" : "Value");
    m_axisYL->setLabelFormat("%.3f");
    m_axisYL->setTickCount(8);
    if (m_axisYR) {
        m_axisYR->setTitleText("Parameters");
        m_axisYR->setLabelFormat("%.2f");
        m_axisYR->setTickCount(8);
    }

    m_chart->addAxis(m_axisX,  Qt::AlignBottom);
    m_chart->addAxis(m_axisYL, Qt::AlignLeft);
    if (m_axisYR)
        m_chart->addAxis(m_axisYR, Qt::AlignRight);

    // Accumulate global ranges
    double xMin  =  std::numeric_limits<double>::max();
    double xMax  = -std::numeric_limits<double>::max();
    double yLMin =  std::numeric_limits<double>::max();
    double yLMax = -std::numeric_limits<double>::max();
    double yRMin =  std::numeric_limits<double>::max();
    double yRMax = -std::numeric_limits<double>::max();

    auto addSeries = [&](const QString &col, int colorIdx,
                         QValueAxis *yAxis, bool isRight)
    {
        if (!data.isNumeric(col)) return;
        const QVector<double> &yVals = data.numeric.value(col);

        QLineSeries *series = new QLineSeries();
        series->setName(col);
        QPen pen(s_palette.at(colorIdx % s_palette.size()));
        pen.setWidthF(1.5);
        series->setPen(pen);

        for (int i = 0; i < N && i < yVals.size(); ++i) {
            double y = yVals[i];
            if (std::isnan(y)) continue;
            double x = (xIsNumeric && i < xNum.size() && !std::isnan(xNum[i]))
                       ? xNum[i] : static_cast<double>(i);
            series->append(x, y);
            xMin = qMin(xMin, x);
            xMax = qMax(xMax, x);
            if (isRight) { yRMin = qMin(yRMin, y); yRMax = qMax(yRMax, y); }
            else          { yLMin = qMin(yLMin, y); yLMax = qMax(yLMax, y); }
        }

        m_chart->addSeries(series);
        series->attachAxis(m_axisX);
        series->attachAxis(yAxis);
    };

    int idx = 0;
    for (const QString &col : leftCols)  addSeries(col, idx++, m_axisYL, false);
    for (const QString &col : rightCols) addSeries(col, idx++,
                                         m_axisYR ? m_axisYR : m_axisYL, true);

    // Helper: apply range with 5% padding
    auto applyRange = [](QValueAxis *ax, double mn, double mx) {
        if (mn > mx) { mn = 0; mx = 1; }
        double pad = (mx - mn) * 0.05;
        if (pad == 0) pad = qAbs(mn) * 0.05 + 0.1;
        ax->setRange(mn - pad, mx + pad);
    };

    applyRange(m_axisX,  xMin,  xMax);
    applyRange(m_axisYL, yLMin, yLMax);
    if (m_axisYR)
        applyRange(m_axisYR, yRMin, yRMax);

    // Store full ranges for reset
    m_fullX  = { m_axisX->min(),  m_axisX->max()  };
    m_fullYL = { m_axisYL->min(), m_axisYL->max() };
    m_fullYR = m_axisYR ? AxisRange{m_axisYR->min(), m_axisYR->max()} : AxisRange{0,1};

    m_chart->setTitle("Battery Data");
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);

    // Clickable legend markers
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

    // Notify UI of initial ranges
    onViewRangeChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
// Scale setters (called from UI spinboxes)
// ─────────────────────────────────────────────────────────────────────────────
void ChartManager::setXRange(double min, double max)
{
    if (m_axisX && min < max) m_axisX->setRange(min, max);
}
void ChartManager::setYLRange(double min, double max)
{
    if (m_axisYL && min < max) m_axisYL->setRange(min, max);
}
void ChartManager::setYRRange(double min, double max)
{
    if (m_axisYR && min < max) m_axisYR->setRange(min, max);
}

void ChartManager::autoScale()
{
    if (!m_axisX) return;
    m_axisX->setRange (m_fullX.min,  m_fullX.max);
    m_axisYL->setRange(m_fullYL.min, m_fullYL.max);
    if (m_axisYR)
        m_axisYR->setRange(m_fullYR.min, m_fullYR.max);
    onViewRangeChanged();
}

void ChartManager::resetZoom()
{
    if (!m_axisX) return;
    m_chart->zoomReset();
    onViewRangeChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
// Getters
// ─────────────────────────────────────────────────────────────────────────────
AxisRange ChartManager::xRange()  const
{
    return m_axisX  ? AxisRange{m_axisX->min(),  m_axisX->max()}  : AxisRange{0,1};
}
AxisRange ChartManager::yLRange() const
{
    return m_axisYL ? AxisRange{m_axisYL->min(), m_axisYL->max()} : AxisRange{0,1};
}
AxisRange ChartManager::yRRange() const
{
    return m_axisYR ? AxisRange{m_axisYR->min(), m_axisYR->max()} : AxisRange{0,1};
}

// ─────────────────────────────────────────────────────────────────────────────
// Sync spinboxes after zoom/pan
// ─────────────────────────────────────────────────────────────────────────────
void ChartManager::onViewRangeChanged()
{
    emit rangeChanged(xRange(), yLRange(), yRRange());
}
