#ifndef ASTRO_UI_CAMERA_VIEW_H
#define ASTRO_UI_CAMERA_VIEW_H

#include <QImage>
#include <QPainter>
#include <QQuickPaintedItem>

#include <algorithm>

// Shows the latest picture from the camera, keeping its shape
class CameraView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY imageChanged)

public:
    explicit CameraView(QQuickItem* parent = nullptr) : QQuickPaintedItem(parent) { }

    QImage image() const { return m_image; }
    void setImage(const QImage& image)
    {
        m_image = image;
        emit imageChanged();
        update();
    }

    void paint(QPainter* painter) override
    {
        if (m_image.isNull())
            return;
        double scale = std::min(width() / m_image.width(), height() / m_image.height());
        QSizeF size(m_image.width() * scale, m_image.height() * scale);
        QRectF area(QPointF((width() - size.width()) / 2.0, (height() - size.height()) / 2.0), size);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, scale < 1.0);
        painter->drawImage(area, m_image);
    }

signals:
    void imageChanged();

private:
    QImage m_image;
};

#endif
