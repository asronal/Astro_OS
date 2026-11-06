#ifndef ASTRO_UI_CATALOG_H
#define ASTRO_UI_CATALOG_H

#include <QString>
#include <QVector>

struct CatalogStar {
    float ra_deg;
    float dec_deg;
    float mag;
    QString name;
    float colour_index;  // B-V: bluer stars are lower, redder stars higher
    bool proper_name;    // Sirius, as opposed to a designation like Kap Cen
};

struct ConstellationSegment {
    float ra1_deg;
    float dec1_deg;
    float ra2_deg;
    float dec2_deg;
};

struct ConstellationName {
    QString name;
    float ra_deg;
    float dec_deg;
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
    float major_arcmin;  // Real size on the sky, or 0 if unknown
    float minor_arcmin;
    float angle_deg;     // Long axis, from north through east
};

struct FaintStar {
    float ra_deg;
    float dec_deg;
    float mag;
    float colour_index;
};

// The sky split into blocks, so that a zoomed-in view only looks at nearby stars
struct FaintStarCell {
    float center_ra_deg;
    float center_dec_deg;
    QVector<FaintStar> stars;  // Brightest first
};

// Bright stars and deep-sky objects, with positions precessed to the given date
class Catalog {
public:
    bool load(const QString& directory, double jd);

    const QVector<CatalogStar>& stars() const { return m_stars; }
    const QVector<CatalogObject>& objects() const { return m_objects; }
    const QVector<FaintStarCell>& faintCells() const { return m_faint_cells; }
    const QVector<ConstellationSegment>& constellationLines() const { return m_lines; }
    const QVector<ConstellationName>& constellationNames() const { return m_names; }
    const QString& directory() const { return m_directory; }

private:
    void load_faint_stars(const QString& path, double jd);

    QVector<CatalogStar> m_stars;
    QVector<CatalogObject> m_objects;
    QVector<FaintStarCell> m_faint_cells;
    QVector<ConstellationSegment> m_lines;
    QVector<ConstellationName> m_names;
    QString m_directory;
};

#endif
