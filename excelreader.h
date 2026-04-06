#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>

struct BatteryData
{
    QStringList                    headers;
    QMap<QString, QVector<double>> numeric;
    QMap<QString, QStringList>     text;
    int rowCount = 0;

    bool hasColumn(const QString &col) const {
        return numeric.contains(col) || text.contains(col);
    }
    bool isNumeric(const QString &col) const {
        return numeric.contains(col);
    }
};

class ExcelReader : public QObject
{
    Q_OBJECT
public:
    explicit ExcelReader(QObject *parent = nullptr);

    // Supports .xlsx, .csv, .tsv, .txt
    bool load(const QString &filePath);

    const BatteryData &data()  const { return m_data;  }
    QString lastError()        const { return m_error; }

signals:
    void progress(int percent);

private:
    bool loadXlsx(const QString &path);
    bool loadCsv (const QString &path);

    BatteryData m_data;
    QString     m_error;
};
