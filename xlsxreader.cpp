#include "xlsxreader.h"

#include <QFile>
#include <QXmlStreamReader>
#include <cmath>
#include <limits>
#include <zlib.h>

// ═══════════════════════════════════════════════════════════════════════════════
// ZIP reading
// An .xlsx file is a standard ZIP archive.
// ZIP format (PKWARE spec):
//   Each file entry starts with a Local File Header signature 0x04034b50
//   followed by metadata and compressed data.
//   We scan sequentially for these signatures.
// ═══════════════════════════════════════════════════════════════════════════════

static const quint32 ZIP_LOCAL_MAGIC = 0x04034b50;
static const quint16 COMP_STORED    = 0;
static const quint16 COMP_DEFLATED  = 8;

// Read little-endian integers from raw bytes
static quint16 readU16(const uchar *p) {
    return static_cast<quint16>(p[0]) | (static_cast<quint16>(p[1]) << 8);
}
static quint32 readU32(const uchar *p) {
    return static_cast<quint32>(p[0])
         | (static_cast<quint32>(p[1]) << 8)
         | (static_cast<quint32>(p[2]) << 16)
         | (static_cast<quint32>(p[3]) << 24);
}

bool XlsxReader::inflateEntry(const uchar *compData, quint32 compSize,
                               quint32 uncompSize, QByteArray &out)
{
    out.resize(static_cast<int>(uncompSize));

    z_stream zs{};
    zs.next_in   = const_cast<Bytef*>(compData);
    zs.avail_in  = compSize;
    zs.next_out  = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = uncompSize;

    // -MAX_WBITS = raw deflate (no zlib header) as used in ZIP
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;
    int ret = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    return (ret == Z_STREAM_END);
}

QVector<XlsxReader::ZipEntry> XlsxReader::readZip(const QString &path,
                                                     QString &error)
{
    QVector<ZipEntry> entries;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = "Cannot open file: " + f.errorString();
        return entries;
    }
    QByteArray raw = f.readAll();
    f.close();

    const uchar *data = reinterpret_cast<const uchar*>(raw.constData());
    qint64 size = raw.size();
    qint64 pos  = 0;

    while (pos + 30 <= size) {
        // Scan for local file header magic
        quint32 magic = readU32(data + pos);
        if (magic != ZIP_LOCAL_MAGIC) {
            ++pos;
            continue;
        }

        // Local File Header layout (offsets from start of header):
        //  0  signature        4 bytes
        //  4  version needed   2
        //  6  flags            2
        //  8  compression      2
        // 10  mod time         2
        // 12  mod date         2
        // 14  crc32            4
        // 18  compressed size  4
        // 22  uncompressed sz  4
        // 26  filename length  2
        // 28  extra length     2
        // 30  filename         (variable)
        // 30+fnLen+exLen  data

        if (pos + 30 > size) break;

        quint16 compression = readU16(data + pos + 8);
        quint32 compSize    = readU32(data + pos + 18);
        quint32 uncompSize  = readU32(data + pos + 22);
        quint16 fnLen       = readU16(data + pos + 26);
        quint16 exLen       = readU16(data + pos + 28);

        qint64 dataStart = pos + 30 + fnLen + exLen;
        if (dataStart + compSize > size) { pos += 4; continue; }

        QString entryName = QString::fromUtf8(
            reinterpret_cast<const char*>(data + pos + 30), fnLen);

        ZipEntry entry;
        entry.name = entryName;

        if (compression == COMP_STORED) {
            entry.data = QByteArray(reinterpret_cast<const char*>(data + dataStart),
                                    static_cast<int>(compSize));
        } else if (compression == COMP_DEFLATED) {
            if (!inflateEntry(data + dataStart, compSize, uncompSize, entry.data)) {
                // Skip corrupt entry
                pos = dataStart + compSize;
                continue;
            }
        } else {
            // Unsupported compression — skip
            pos = dataStart + compSize;
            continue;
        }

        entries.append(entry);
        pos = dataStart + compSize;
    }

    if (entries.isEmpty())
        error = "No entries found — file may not be a valid XLSX/ZIP.";

    return entries;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shared strings XML parser
// xl/sharedStrings.xml contains a list of <si><t>...</t></si> elements.
// Cells reference these by index instead of repeating the string.
// ═══════════════════════════════════════════════════════════════════════════════
QStringList XlsxReader::parseSharedStrings(const QByteArray &xml)
{
    QStringList strings;
    QXmlStreamReader reader(xml);

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("si")) {
            // Collect all <t> text inside this <si>
            QString value;
            int depth = 1;
            while (!reader.atEnd() && depth > 0) {
                reader.readNext();
                if (reader.isStartElement()) {
                    ++depth;
                    if (reader.name() == QLatin1String("t"))
                        value += reader.readElementText();
                } else if (reader.isEndElement()) {
                    --depth;
                }
            }
            strings.append(value);
        }
    }
    return strings;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Column reference helpers
// "A" -> 0, "B" -> 1, "Z" -> 25, "AA" -> 26, "AB" -> 27 ...
// ═══════════════════════════════════════════════════════════════════════════════
QString XlsxReader::cellColId(const QString &cellRef)
{
    // Extract leading letters from e.g. "AB12"
    QString col;
    for (QChar c : cellRef) {
        if (c.isLetter()) col += c.toUpper();
        else break;
    }
    return col;
}

int XlsxReader::colLetterToIndex(const QString &col)
{
    int result = 0;
    for (QChar c : col)
        result = result * 26 + (c.toUpper().unicode() - 'A' + 1);
    return result - 1;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Sheet XML parser  (xl/worksheets/sheet1.xml)
//
// Structure:
//   <sheetData>
//     <row r="1">
//       <c r="A1" t="s"><v>0</v></c>      ← shared string index
//       <c r="B1" t="str"><v>text</v></c>  ← inline string
//       <c r="C1"><v>3.14</v></c>           ← numeric
//     </row>
//   </sheetData>
// ═══════════════════════════════════════════════════════════════════════════════
XlsxSheet XlsxReader::parseSheet(const QByteArray &sheetXml,
                                   const QStringList &sharedStrings)
{
    XlsxSheet result;
    QXmlStreamReader xml(sheetXml);

    // First pass: collect all rows into a 2D string grid
    struct Cell { int col; QString value; };
    struct Row  { int rowIdx; QVector<Cell> cells; };
    QVector<Row> rows;

    int maxCol = 0;

    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;

        if (xml.name() == QLatin1String("row")) {
            Row row;
            row.rowIdx = xml.attributes().value("r").toInt() - 1; // 0-based

            // Parse <c> elements inside this row
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement() && xml.name() == QLatin1String("row"))
                    break;
                if (!xml.isStartElement() || xml.name() != QLatin1String("c"))
                    continue;

                QString cellRef  = xml.attributes().value("r").toString();
                QString cellType = xml.attributes().value("t").toString();

                QString colLetters = cellColId(cellRef);
                int colIdx = colLetterToIndex(colLetters);
                maxCol = qMax(maxCol, colIdx + 1);

                // Read <v> value inside <c>
                QString rawValue;
                while (!xml.atEnd()) {
                    xml.readNext();
                    if (xml.isEndElement() && xml.name() == QLatin1String("c"))
                        break;
                    if (xml.isStartElement() && xml.name() == QLatin1String("v"))
                        rawValue = xml.readElementText();
                    // <is><t> for inline strings
                    if (xml.isStartElement() && xml.name() == QLatin1String("t"))
                        rawValue = xml.readElementText();
                }

                // Resolve value
                QString displayValue;
                if (cellType == "s") {
                    // Shared string
                    bool ok = false;
                    int idx = rawValue.toInt(&ok);
                    displayValue = (ok && idx < sharedStrings.size())
                                   ? sharedStrings[idx] : rawValue;
                } else {
                    displayValue = rawValue;
                }

                row.cells.append({colIdx, displayValue});
            }
            rows.append(row);
        }
    }

    if (rows.isEmpty()) {
        result.errorString = "Sheet appears to be empty.";
        return result;
    }

    // Row 0 = headers
    Row &headerRow = rows[0];
    // Build header list sized to maxCol
    QVector<QString> headers(maxCol);
    for (const Cell &c : headerRow.cells) {
        if (c.col < maxCol) headers[c.col] = c.value;
    }
    // Fill empty header slots
    for (int i = 0; i < maxCol; ++i)
        if (headers[i].isEmpty()) headers[i] = QString("Col%1").arg(i + 1);

    result.headers = QStringList(headers.begin(), headers.end());

    // Initialise column storage
    for (const QString &h : result.headers) {
        result.text[h]    = QStringList{};
        result.numeric[h] = QVector<double>{};
    }

    // Data rows (skip row index 0)
    // Build a map from rowIdx for quick lookup
    int dataRows = 0;
    // Find max rowIdx
    int maxRowIdx = 0;
    for (const Row &r : rows) maxRowIdx = qMax(maxRowIdx, r.rowIdx);

    // For each data row (rowIdx >= 1), fill columns
    // Use a sparse approach: iterate rows in order
    for (const Row &r : rows) {
        if (r.rowIdx == 0) continue; // header

        // For this row build a col->value map
        QMap<int, QString> colVal;
        for (const Cell &c : r.cells)
            colVal[c.col] = c.value;

        // Fill each column
        for (int ci = 0; ci < maxCol; ++ci) {
            const QString &hdr = headers[ci];
            QString val = colVal.value(ci, QString{});
            result.text[hdr].append(val);
            bool ok = false;
            double d = val.toDouble(&ok);
            result.numeric[hdr].append(ok ? d : std::numeric_limits<double>::quiet_NaN());
        }
        ++dataRows;
    }

    result.rowCount = dataRows;

    // Remove columns with no numeric data from numeric map
    for (const QString &h : result.headers) {
        bool anyValid = false;
        for (double v : result.numeric[h])
            if (!std::isnan(v)) { anyValid = true; break; }
        if (!anyValid)
            result.numeric.remove(h);
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Main entry point
// ═══════════════════════════════════════════════════════════════════════════════
XlsxSheet XlsxReader::read(const QString &filePath)
{
    XlsxSheet result;
    QString zipError;

    QVector<ZipEntry> entries = readZip(filePath, zipError);
    if (!zipError.isEmpty()) {
        result.errorString = zipError;
        return result;
    }

    // Find the parts we need
    QByteArray sharedStringsXml;
    QByteArray sheet1Xml;

    for (const ZipEntry &e : entries) {
        if (e.name == "xl/sharedStrings.xml")
            sharedStringsXml = e.data;
        // Accept sheet1.xml (name varies but is usually sheet1)
        if (e.name == "xl/worksheets/sheet1.xml" && sheet1Xml.isEmpty())
            sheet1Xml = e.data;
    }

    // Fallback: use any worksheet
    if (sheet1Xml.isEmpty()) {
        for (const ZipEntry &e : entries) {
            if (e.name.startsWith("xl/worksheets/") && e.name.endsWith(".xml")) {
                sheet1Xml = e.data;
                break;
            }
        }
    }

    if (sheet1Xml.isEmpty()) {
        result.errorString = "No worksheet found in XLSX file.";
        return result;
    }

    QStringList sharedStrings;
    if (!sharedStringsXml.isEmpty())
        sharedStrings = parseSharedStrings(sharedStringsXml);

    result = parseSheet(sheet1Xml, sharedStrings);
    return result;
}
