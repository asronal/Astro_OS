#ifndef ASTRO_UI_SKY_CHART_H
#define ASTRO_UI_SKY_CHART_H

#include <QBrush>
#include <QImage>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QVariant>
#include <QVector>

#include <vector>

#include "chart_math.h"
#include "sky_backend.h"

// A touchable chart of the sky above the observer.
//
// It is drawn in layers, from the back: the sky's colour and the Milky Way, then
// stars, constellations, deep-sky objects and planets, then the landscape, which
// hides anything below the horizon, and finally labels and markers.
class SkyChart : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(SkyBackend* backend READ backend WRITE setBackend NOTIFY viewChanged)
    Q_PROPERTY(double centerAz READ centerAz WRITE setCenterAz NOTIFY viewChanged)
    Q_PROPERTY(double centerAlt READ centerAlt WRITE setCenterAlt NOTIFY viewChanged)
    Q_PROPERTY(double fieldOfView READ fieldOfView WRITE setFieldOfView NOTIFY viewChanged)
    Q_PROPERTY(bool nightMode READ nightMode WRITE setNightMode NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap target READ target WRITE setTarget NOTIFY viewChanged)
    Q_PROPERTY(bool mountVisible READ mountVisible WRITE setMountVisible NOTIFY viewChanged)
    Q_PROPERTY(double mountRa READ mountRa WRITE setMountRa NOTIFY viewChanged)
    Q_PROPERTY(double mountDec READ mountDec WRITE setMountDec NOTIFY viewChanged)
    Q_PROPERTY(bool showConstellations READ showConstellations WRITE setShowConstellations NOTIFY viewChanged)
    Q_PROPERTY(bool showMilkyWay READ showMilkyWay WRITE setShowMilkyWay NOTIFY viewChanged)
    Q_PROPERTY(bool showLandscape READ showLandscape WRITE setShowLandscape NOTIFY viewChanged)
    Q_PROPERTY(bool showAtmosphere READ showAtmosphere WRITE setShowAtmosphere NOTIFY viewChanged)
    // Labels stay out of these strips, where the header and navigation sit on top
    Q_PROPERTY(double safeTop READ safeTop WRITE setSafeTop NOTIFY viewChanged)
    Q_PROPERTY(double safeBottom READ safeBottom WRITE setSafeBottom NOTIFY viewChanged)
    Q_PROPERTY(double timeOffsetHours READ timeOffsetHours WRITE setTimeOffsetHours NOTIFY viewChanged)
    // From SkyBackend::eyepieces. Those switched on are drawn as rings of true size.
    Q_PROPERTY(QVariantList rings READ rings WRITE setRings NOTIFY viewChanged)
    // Marks the middle of the view, which is what the keyboard selects
    Q_PROPERTY(bool showCenterMark READ showCenterMark WRITE setShowCenterMark NOTIFY viewChanged)

public:
    explicit SkyChart(QQuickItem* parent = nullptr);

    SkyBackend* backend() const { return m_sky; }
    void setBackend(SkyBackend* backend);
    double centerAz() const { return m_center_az; }
    void setCenterAz(double az);
    double centerAlt() const { return m_center_alt; }
    void setCenterAlt(double alt);
    double fieldOfView() const { return m_fov; }
    void setFieldOfView(double fov);
    bool nightMode() const { return m_night_mode; }
    void setNightMode(bool enabled);
    QVariantMap target() const { return m_target; }
    void setTarget(const QVariantMap& target);
    bool mountVisible() const { return m_mount_visible; }
    void setMountVisible(bool visible);
    double mountRa() const { return m_mount_ra; }
    void setMountRa(double ra);
    double mountDec() const { return m_mount_dec; }
    void setMountDec(double dec);
    bool showConstellations() const { return m_show_constellations; }
    void setShowConstellations(bool show);
    bool showMilkyWay() const { return m_show_milky_way; }
    void setShowMilkyWay(bool show);
    bool showLandscape() const { return m_show_landscape; }
    void setShowLandscape(bool show);
    bool showAtmosphere() const { return m_show_atmosphere; }
    void setShowAtmosphere(bool show);
    double safeTop() const { return m_safe_top; }
    void setSafeTop(double pixels);
    double safeBottom() const { return m_safe_bottom; }
    void setSafeBottom(double pixels);
    // Only moves what the chart shows. GoTo and its safety checks use the real time.
    double timeOffsetHours() const { return m_time_offset_hours; }
    void setTimeOffsetHours(double hours);
    QVariantList rings() const { return m_rings; }
    void setRings(const QVariantList& rings);
    bool showCenterMark() const { return m_show_center_mark; }
    void setShowCenterMark(bool show);

    void paint(QPainter* painter) override;

    Q_INVOKABLE void panBy(double dx, double dy);
    Q_INVOKABLE void centerOn(double ra_deg, double dec_deg);
    // Turns to the target, wherever it is at the time being shown
    Q_INVOKABLE void centerOnTarget();
    // Zooms while keeping the same part of the sky under this point, such as the pointer
    Q_INVOKABLE void zoomAt(double x, double y, double factor);
    // The object a tap chooses. Tapping the same place again moves on to the next one
    // there, and cycleIndex and cycleCount say how far along that is. The position it
    // returns is always the real one for now, whatever time the chart is showing.
    Q_INVOKABLE QVariantMap nextObjectAt(double x, double y);

signals:
    void viewChanged();

private:
    // Keeps labels from landing on top of one another
    class LabelPlacer;

    chart::View view() const;
    chart::Site site() const;
    double julian_date() const;
    double star_limit() const { return m_frame_star_limit; }
    double daylight() const { return m_frame_daylight; }
    double moon_light() const { return m_frame_moon_light; }
    void prepare_frame(const chart::Site& site);
    double ridge_height(const std::vector<double>& ridge, double az) const;
    void refresh_bodies();
    void build_star_sprites();
    QRgb sky_colour(double az, double alt, double ra, double dec, double sun_az, double sun_alt) const;
    double milky_way_at(double ra, double dec) const;

    void draw_sky(QPainter* painter, const chart::View& view, const chart::Site& site);
    void draw_stars(QPainter* painter, const chart::View& view, const chart::Site& site);
    void draw_star(QPainter* painter, const chart::View& view, const chart::Site& site, double ra, double dec,
                   double mag, float colour_index, double limit);
    void draw_constellation_lines(QPainter* painter, const chart::View& view, const chart::Site& site) const;
    void draw_deep_sky(QPainter* painter, const chart::View& view, const chart::Site& site) const;
    void draw_bodies(QPainter* painter, const chart::View& view, const chart::Site& site);
    void draw_object(QPainter* painter, const chart::View& view, const chart::Site& site, const CatalogObject& object,
                     QPointF center) const;
    void draw_moon(QPainter* painter, const chart::View& view, const chart::Site& site, QPointF center, double radius);
    void draw_ridge(QPainter* painter, const chart::View& view, const std::vector<double>& ridge,
                    const QBrush& fill, const QColor& rim) const;
    void draw_landscape(QPainter* painter, const chart::View& view) const;
    void draw_labels(QPainter* painter, const chart::View& view, const chart::Site& site) const;
    void draw_markers(QPainter* painter, const chart::View& view, const chart::Site& site) const;
    void draw_rings(QPainter* painter, const chart::View& view, const chart::Site& site) const;
    bool target_shown_at(double* ra, double* dec) const;

    SkyBackend* m_sky;
    double m_center_az;
    double m_center_alt;
    double m_fov;
    bool m_night_mode;
    QVariantMap m_target;
    bool m_mount_visible;
    double m_mount_ra;
    double m_mount_dec;
    bool m_show_constellations;
    bool m_show_milky_way;
    bool m_show_landscape;
    bool m_show_atmosphere;
    double m_safe_top;
    double m_safe_bottom;
    double m_time_offset_hours;
    QVariantList m_rings;
    bool m_show_center_mark;

    // Where the last tap was, to tell tapping again from tapping somewhere new
    double m_last_tap_x;
    double m_last_tap_y;

    // Working out a planet's position is slow, so only one is updated per frame
    std::vector<astro::BodyReport> m_bodies;

    // Worked out once per frame, as the sky's colour needs them for every pixel
    double m_frame_star_limit;
    double m_frame_daylight;
    double m_frame_moon_light;

    std::size_t m_next_body;
    qint64 m_last_body_update_ms;

    // The sky's colour is worked out for a coarse grid of directions, and only
    // again once the view or the time has moved. Scaling it up also softens it.
    QImage m_sky_image;
    QImage m_milky_way;
    double m_sky_key[6];

    // Drawing stars from small pre-rendered images is far quicker than drawing
    // each one with antialiasing
    QVector<QImage> m_star_sprites;
    bool m_sprites_for_night;

    QImage m_moon_map;

    std::vector<double> m_ridge_far;
    std::vector<double> m_ridge_near;
};

#endif
