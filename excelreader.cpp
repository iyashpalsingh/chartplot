#include "excelreader.h"
#include "xlsxreader.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <cmath>
#include <limits>

ExcelReader::ExcelReader(QObject *parent) : QObject(parent) {}

// ─────────────────────────────────────────────────────────────────────────────
bool ExcelReader::load(const QString &filePath)
{
    m_data  = BatteryData{};
    m_error = QString{};

    QString ext = QFileInfo(filePath).suffix().toLower();

    if (ext == "xlsx" || ext == "xls")
        return loadXlsx(filePath);

    if (ext == "csv" || ext == "tsv" || ext == "txt")
        return loadCsv(filePath);

    m_error = "Unsupported file type: " + ext
            + "\nSupported: .xlsx, .csv, .tsv, .txt";
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// XLSX loader — uses our built-in XlsxReader (no external library needed)
// ─────────────────────────────────────────────────────────────────────────────
bool ExcelReader::loadXlsx(const QString &filePath)
{
    emit progress(5);

    XlsxSheet sheet = XlsxReader::read(filePath);

    if (!sheet.isValid()) {
        m_error = sheet.errorString;
        return false;
    }

    emit progress(60);

    // Copy XlsxSheet -> BatteryData
    m_data.headers  = sheet.headers;
    m_data.numeric  = sheet.numeric;
    m_data.text     = sheet.text;
    m_data.rowCount = sheet.rowCount;

    emit progress(100);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// CSV / TSV loader
// ─────────────────────────────────────────────────────────────────────────────
bool ExcelReader::loadCsv(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_error = "Cannot open file: " + file.errorString();
        return false;
    }

    emit progress(5);

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    if (in.atEnd()) { m_error = "File is empty."; return false; }
    QString headerLine = in.readLine();

    // Auto-detect delimiter: tab preferred, else comma
    QChar delim = headerLine.contains('\t') ? '\t' : ',';

    auto splitLine = [&](const QString &line) -> QStringList {
        QStringList result;
        QString field;
        bool inQuote = false;
        for (int i = 0; i < line.size(); ++i) {
            QChar c = line[i];
            if (c == '"') {
                inQuote = !inQuote;
            } else if (c == delim && !inQuote) {
                result.append(field.trimmed());
                field.clear();
            } else {
                field.append(c);
            }
        }
        result.append(field.trimmed());
        return result;
    };

    m_data.headers = splitLine(headerLine);
    int colCount = m_data.headers.size();

    for (const QString &h : m_data.headers) {
        m_data.numeric[h] = QVector<double>{};
        m_data.text[h]    = QStringList{};
    }

    emit progress(10);

    // Count lines for progress reporting
    qint64 startPos = in.pos();
    int totalLines = 0;
    while (!in.atEnd()) { in.readLine(); ++totalLines; }
    in.seek(startPos);

    int row = 0;
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;
        QStringList fields = splitLine(line);

        for (int col = 0; col < colCount; ++col) {
            const QString &header = m_data.headers[col];
            QString val = (col < fields.size()) ? fields[col] : QString{};
            m_data.text[header].append(val);
            bool ok = false;
            double d = val.toDouble(&ok);
            m_data.numeric[header].append(ok ? d : std::numeric_limits<double>::quiet_NaN());
        }
        ++row;

        if (totalLines > 0 && row % 50 == 0)
            emit progress(10 + static_cast<int>(85.0 * row / totalLines));
    }

    m_data.rowCount = row;

    // Remove all-NaN columns from numeric map
    for (const QString &h : m_data.headers) {
        bool anyValid = false;
        for (double v : m_data.numeric[h])
            if (!std::isnan(v)) { anyValid = true; break; }
        if (!anyValid)
            m_data.numeric.remove(h);
    }

    emit progress(100);
    return true;
}
