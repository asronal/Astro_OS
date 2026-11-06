#ifndef ASTRO_UI_SKY_CHART_H
#define ASTRO_UI_SKY_CHART_H

#include <QPointF>
#include <QQuickPaintedItem>
#include <QVariant>

#include "sky_backend.h"

// A touchable chart of the sky above the observer, drawn with a stereographic
// projection so that shapes stay recognisable near the edge of the view
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

    void paint(QPainter* painter) override;

    Q_INVOKABLE void panBy(double dx, double dy);
    Q_INVOKABLE void centerOn(double ra_deg, double dec_deg);
    Q_INVOKABLE QVariantMap objectAt(double x, double y) const;

signals:
    void viewChanged();

private:
    bool project(double az, double alt, QPointF* point) const;
    void draw_ground(QPainter* painter, const QColor& color, const QColor& line) const;
    void draw_grid(QPainter* painter, const QColor& color) const;
    void equatorial_to_screen_inputs(double* lst_rad, double* sin_lat, double* cos_lat) const;

    SkyBackend* m_sky;
    double m_center_az;
    double m_center_alt;
    double m_fov;
    bool m_night_mode;
    QVariantMap m_target;
    bool m_mount_visible;
    double m_mount_ra;
    double m_mount_dec;
};

#endif
