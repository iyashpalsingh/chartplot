#pragma once
/**
 * xlsxreader.h / xlsxreader.cpp
 *
 * Pure C++ XLSX reader — no external libraries needed.
 * Uses only:
 *   - zlib  (ships with Qt6 on all platforms, link with -lz or Qt6::Core)
 *   - QXmlStreamReader (Qt6::Core)
 *   - Standard POSIX file I/O
 *
 * An .xlsx file is a ZIP archive containing XML files.
 * We read the ZIP local-file entries using zlib inflate, then parse
 * the relevant XML sheets with QXmlStreamReader.
 *
 * Supported: string cells, numeric cells, date cells (stored as numbers).
 * Limitation: only reads the first worksheet.
 */

#include <QString>
#include <QStringList>
#include <QVector>
#include <QByteArray>
#include <QMap>

// ── Result structure ──────────────────────────────────────────────────────────
struct XlsxSheet
{
    QStringList              headers;   // row 0 treated as header
    QMap<QString, QVector<double>> numeric; // column -> numeric values (NaN if not numeric)
    QMap<QString, QStringList>     text;    // column -> raw string values
    int rowCount = 0;
    QString errorString;
    bool isValid() const { return errorString.isEmpty(); }
};

// ── Parser class ──────────────────────────────────────────────────────────────
class XlsxReader
{
public:
    // Reads the first sheet of an xlsx file.
    // Returns a filled XlsxSheet; check isValid() / errorString on result.
    static XlsxSheet read(const QString &filePath);

private:
    // ZIP internals
    struct ZipEntry {
        QString    name;
        QByteArray data;   // decompressed
    };

    static QVector<ZipEntry> readZip(const QString &path, QString &error);
    static bool inflateEntry(const uchar *compData, quint32 compSize,
                             quint32 uncompSize, QByteArray &out);

    // XML parsers
    static QStringList parseSharedStrings(const QByteArray &xml);
    static XlsxSheet   parseSheet(const QByteArray &sheetXml,
                                   const QStringList &sharedStrings);
    static QString     cellColId(const QString &cellRef);  // "B3" -> "B"
    static int         colLetterToIndex(const QString &col); // "A"->0 "Z"->25 "AA"->26
};
