#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>

// ── Column lists ──────────────────────────────────────────────────────────────
const QStringList MainWindow::s_voltageCols = {
    "Cell1","Cell2","Cell3","Cell4","Cell5","Cell6","Cell7","Cell8",
    "Cell9","Cell10","Cell11","Cell12","Cell13","Cell14","Cell15","Cell16"
};
const QStringList MainWindow::s_tempCols = {
    "T1","T2","T3","T4","T5","T6","T7","T8",
    "T9","T10","T11","T12","T13","T14"
};
const QStringList MainWindow::s_paramCols = {"Current","Capacity","SOC"};

// ── LoadWorker ────────────────────────────────────────────────────────────────
void LoadWorker::run()
{
    ExcelReader reader;
    QObject::connect(&reader, &ExcelReader::progress,
                     this,    &LoadWorker::progress);
    bool ok = reader.load(m_path);
    emit finished(reader.data(), ok, reader.lastError());
}

// ── MainWindow ────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Tab 1
    connect(ui->pushButton,   &QPushButton::clicked, this, &MainWindow::onLoadFile);
    connect(ui->pushButton_2, &QPushButton::clicked, this, &MainWindow::onPlotChart);

    // Tab 2
    connect(ui->btnPlot, &QPushButton::clicked, this, &MainWindow::onPlotTab2);

    // Build checkbox groups
    buildCheckBoxGroup(ui->scrollVoltageContents,     s_voltageCols, m_voltageChecks);
    buildCheckBoxGroup(ui->scrollTemperatureContents, s_tempCols,    m_tempChecks);
    buildCheckBoxGroup(ui->scrollParameterContents,   s_paramCols,   m_paramChecks);

    // Embed chart into chartContainer
    m_chartManager = new ChartManager(ui->chartContainer, this);
}

MainWindow::~MainWindow()
{
    if (m_thread && m_thread->isRunning()) {
        m_thread->quit();
        m_thread->wait();
    }
    delete ui;
}

void MainWindow::buildCheckBoxGroup(QWidget *container,
                                    const QStringList &items,
                                    QMap<QString, QCheckBox*> &checkMap)
{
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setSpacing(2);
    layout->setContentsMargins(4, 4, 4, 4);
    for (const QString &item : items) {
        QCheckBox *cb = new QCheckBox(item, container);
        layout->addWidget(cb);
        checkMap[item] = cb;
    }
    layout->addStretch();
    container->setLayout(layout);
}

QStringList MainWindow::checkedItems(const QMap<QString, QCheckBox*> &map) const
{
    QStringList result;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        if (it.value()->isChecked())
            result.append(it.key());
    return result;
}

// ── SLOT: Load File ───────────────────────────────────────────────────────────
void MainWindow::onLoadFile()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Open Battery Data File", "",
        "Data Files (*.xlsx *.csv *.tsv *.txt);;Excel Files (*.xlsx);;CSV Files (*.csv *.tsv *.txt);;All Files (*)"
    );
    if (path.isEmpty()) return;

    ui->pushButton->setEnabled(false);
    ui->progressBar->setValue(0);
    ui->lblFileInfo->setText("Loading: " + path);

    m_thread = new QThread(this);
    m_worker = new LoadWorker(path);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started,    m_worker, &LoadWorker::run);
    connect(m_worker, &LoadWorker::progress, ui->progressBar, &QProgressBar::setValue);
    connect(m_worker, &LoadWorker::finished, this,    &MainWindow::onLoadFinished);
    connect(m_worker, &LoadWorker::finished, m_thread,&QThread::quit);
    connect(m_thread, &QThread::finished,   m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished,   m_thread, &QObject::deleteLater);

    m_thread->start();
}

// ── SLOT: Load finished ───────────────────────────────────────────────────────
void MainWindow::onLoadFinished(BatteryData data, bool ok, QString error)
{
    ui->pushButton->setEnabled(true);

    if (!ok) {
        ui->progressBar->setValue(0);
        ui->lblFileInfo->setText("Error: " + error);
        QMessageBox::critical(this, "Load Error", error);
        return;
    }

    m_data       = data;
    m_dataLoaded = true;

    QString serial;
    if (data.text.contains("SerialNumber") && !data.text["SerialNumber"].isEmpty())
        serial = data.text["SerialNumber"].first();

    ui->lblFileInfo->setText(
        QString("Loaded  |  %1 rows  |  %2 columns  |  Serial: %3")
            .arg(data.rowCount)
            .arg(data.headers.size())
            .arg(serial.isEmpty() ? "N/A" : serial)
    );

    ui->pushButton_2->setEnabled(true);
    ui->btnPlot->setEnabled(true);

    // Show only checkboxes that exist in the file
    auto updateVisibility = [&](QMap<QString, QCheckBox*> &checkMap) {
        for (auto it = checkMap.begin(); it != checkMap.end(); ++it)
            it.value()->setVisible(data.hasColumn(it.key()));
    };
    updateVisibility(m_voltageChecks);
    updateVisibility(m_tempChecks);
    updateVisibility(m_paramChecks);
}

// ── SLOT: Plot Chart (Tab 1 button) ──────────────────────────────────────────
void MainWindow::onPlotChart()
{
    if (!m_dataLoaded) return;
    ui->tabWidget->setCurrentIndex(1);
    onPlotTab2();
}

// ── SLOT: Plot (Tab 2 button) ─────────────────────────────────────────────────
void MainWindow::onPlotTab2()
{
    if (!m_dataLoaded) {
        QMessageBox::information(this, "No Data", "Please load a file first (Tab 1).");
        return;
    }

    QString xCol = ui->comboXAxis->currentText();

    QStringList yCols;
    yCols += checkedItems(m_voltageChecks);
    yCols += checkedItems(m_tempChecks);
    yCols += checkedItems(m_paramChecks);

    if (yCols.isEmpty()) {
        QMessageBox::information(this, "Nothing Selected",
            "Please check at least one column in the Y-Axis panel.");
        return;
    }

    m_chartManager->plot(xCol, yCols, m_data);
}
