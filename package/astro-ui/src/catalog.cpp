#include "catalog.h"

#include <QFile>
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

bool Catalog::load(const QString& directory, double jd)
{
    QFile star_file(directory + QStringLiteral("/stars.csv"));
    QFile object_file(directory + QStringLiteral("/deepsky.csv"));

    if (!star_file.open(QIODevice::ReadOnly | QIODevice::Text) ||
        !object_file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    double ra;
    double dec;

    QTextStream stars(&star_file);
    stars.readLine();
    while (!stars.atEnd()) {
        QStringList f = split_csv(stars.readLine());
        if (f.size() < 4)
            continue;
        astro::precess_from_j2000(f[0].toDouble(), f[1].toDouble(), jd, &ra, &dec);
        m_stars.append({float(ra), float(dec), f[2].toFloat(), f[3]});
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
        m_objects.append({f[0], f[1], f[2], float(ra), float(dec), f[5].toFloat(), !f[5].isEmpty(), is_messier});
    }
    return true;
}
