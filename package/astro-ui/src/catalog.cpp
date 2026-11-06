#include "catalog.h"

#include <QByteArray>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

#include "astro/sky.h"

// Read one CSV line, allowing quoted fields that contain commas
static QStringList split_csv(const QString& line)
{
    QStringList fields;
    QString current;
    bool quoted = false;

    for (QChar c : line) {
        if (c == QLatin1Char('"'))
            quoted = !quoted;
        else if (c == QLatin1Char(',') && !quoted) {
            fields.append(current);
            current.clear();
        } else
            current.append(c);
    }
    fields.append(current);
    return fields;
}

// Each record is six little-endian bytes, as written by tools/make-sky-data.py
void Catalog::load_faint_stars(const QString& path, double jd)
{
    static const int kCellDeg = 10;
    static const int kColumns = 360 / kCellDeg;
    static const int kRows = 180 / kCellDeg;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QByteArray data = file.readAll();

    m_faint_cells.resize(kColumns * kRows);
    for (int row = 0; row < kRows; row++) {
        for (int column = 0; column < kColumns; column++) {
            FaintStarCell& cell = m_faint_cells[row * kColumns + column];
            cell.center_ra_deg = (column + 0.5f) * kCellDeg;
            cell.center_dec_deg = (row + 0.5f) * kCellDeg - 90.0f;
        }
    }

    const auto* bytes = reinterpret_cast<const unsigned char*>(data.constData());
    int count = data.size() / 6;
    double ra;
    double dec;
    for (int i = 0; i < count; i++) {
        const unsigned char* b = bytes + i * 6;
        double j2000_ra = (b[0] | (b[1] << 8)) / 65535.0 * 360.0;
        double j2000_dec = qint16(b[2] | (b[3] << 8)) / 32767.0 * 90.0;
        astro::precess_from_j2000(j2000_ra, j2000_dec, jd, &ra, &dec);
        int column = qBound(0, int(ra) / kCellDeg, kColumns - 1);
        int row = qBound(0, int(dec + 90.0) / kCellDeg, kRows - 1);
        m_faint_cells[row * kColumns + column].stars.append(
            {float(ra), float(dec), b[4] / 20.0f, qint8(b[5]) / 50.0f});
    }
}

bool Catalog::load(const QString& directory, double jd)
{
    QFile star_file(directory + QStringLiteral("/stars.csv"));
    QFile object_file(directory + QStringLiteral("/deepsky.csv"));

    if (!star_file.open(QIODevice::ReadOnly | QIODevice::Text) ||
        !object_file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    m_directory = directory;

    double ra;
    double dec;

    static const QRegularExpression designation(QStringLiteral("^[A-Z][a-z]{1,2} [A-Z][A-Za-z]{2}$"));

    QTextStream stars(&star_file);
    stars.readLine();
    while (!stars.atEnd()) {
        QStringList f = split_csv(stars.readLine());
        if (f.size() < 4)
            continue;
        astro::precess_from_j2000(f[0].toDouble(), f[1].toDouble(), jd, &ra, &dec);
        bool has_colour = f.size() > 4 && !f[4].isEmpty();
        bool proper = f[3].size() >= 4 && !f[3].at(0).isLower() && !designation.match(f[3]).hasMatch();
        m_stars.append({float(ra), float(dec), f[2].toFloat(), f[3], has_colour ? f[4].toFloat() : 0.5f, proper});
    }

    QTextStream objects(&object_file);
    objects.readLine();
    while (!objects.atEnd()) {
        QStringList f = split_csv(objects.readLine());
        if (f.size() < 7)
            continue;
        astro::precess_from_j2000(f[3].toDouble(), f[4].toDouble(), jd, &ra, &dec);
        // Other catalogs also have names starting with M, such as MWSC 3171
        bool is_messier = f[0].size() > 1 && f[0].at(0) == QLatin1Char('M') && f[0].at(1).isDigit();
        float major = f.size() > 7 ? f[7].toFloat() : 0.0f;
        float minor = f.size() > 8 ? f[8].toFloat() : major;
        float angle = f.size() > 9 ? f[9].toFloat() : 0.0f;
        m_objects.append({f[0], f[1], f[2], float(ra), float(dec), f[5].toFloat(), !f[5].isEmpty(), is_messier,
                          major, minor, angle});
    }

    load_faint_stars(directory + QStringLiteral("/stars-faint.bin"), jd);

    // The constellations are decoration, so the chart still works without them
    QFile line_file(directory + QStringLiteral("/constellations.csv"));
    if (line_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream lines(&line_file);
        lines.readLine();
        while (!lines.atEnd()) {
            QStringList f = split_csv(lines.readLine());
            if (f.size() < 5)
                continue;
            double ra2;
            double dec2;
            astro::precess_from_j2000(f[1].toDouble(), f[2].toDouble(), jd, &ra, &dec);
            astro::precess_from_j2000(f[3].toDouble(), f[4].toDouble(), jd, &ra2, &dec2);
            m_lines.append({float(ra), float(dec), float(ra2), float(dec2)});
        }
    }
    QFile name_file(directory + QStringLiteral("/constellation-names.csv"));
    if (name_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream names(&name_file);
        names.readLine();
        while (!names.atEnd()) {
            QStringList f = split_csv(names.readLine());
            if (f.size() < 4)
                continue;
            astro::precess_from_j2000(f[2].toDouble(), f[3].toDouble(), jd, &ra, &dec);
            m_names.append({f[1].toUpper(), float(ra), float(dec)});
        }
    }
    return true;
}
