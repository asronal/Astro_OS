#ifndef ASTRO_UI_CATALOG_H
#define ASTRO_UI_CATALOG_H

#include <QString>
#include <QVector>

struct CatalogStar {
    float ra_deg;
    float dec_deg;
    float mag;
    QString name;
};

struct CatalogObject {
    QString name;
    QString common;
    QString type;
    float ra_deg;
    float dec_deg;
    float mag;
    bool has_mag;
    bool is_messier;
};

// Bright stars and deep-sky objects, with positions precessed to the given date
class Catalog {
public:
    bool load(const QString& directory, double jd);

    const QVector<CatalogStar>& stars() const { return m_stars; }
    const QVector<CatalogObject>& objects() const { return m_objects; }

private:
    QVector<CatalogStar> m_stars;
    QVector<CatalogObject> m_objects;
};

#endif
