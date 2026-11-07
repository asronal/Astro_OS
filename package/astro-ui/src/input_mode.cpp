#include "input_mode.h"

#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>

InputMode::InputMode(QObject* parent)
    : QObject(parent), m_keyboard_in_use(false), m_on_screen_keyboard_wanted(true), m_night_pointer(false)
{
    QCoreApplication::instance()->installEventFilter(this);
}

InputMode::~InputMode()
{
    if (m_night_pointer)
        QGuiApplication::restoreOverrideCursor();
}

void InputMode::set_state(bool keyboard_in_use, bool on_screen_keyboard_wanted)
{
    if (keyboard_in_use == m_keyboard_in_use && on_screen_keyboard_wanted == m_on_screen_keyboard_wanted)
        return;
    m_keyboard_in_use = keyboard_in_use;
    m_on_screen_keyboard_wanted = on_screen_keyboard_wanted;
    emit changed();
}

bool InputMode::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::TouchBegin:
        set_state(false, true);
        break;
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
        // Touch also arrives as made-up mouse events, which say nothing new
        if (static_cast<QMouseEvent*>(event)->source() == Qt::MouseEventNotSynthesized)
            set_state(false, m_on_screen_keyboard_wanted);
        break;
    case QEvent::KeyPress:
        // Only a real key is spontaneous. The on-screen keyboard's are sent by Qt itself.
        if (event->spontaneous())
            set_state(true, false);
        break;
    case QEvent::Shortcut:
        // A key used as a shortcut never arrives as a KeyPress, and the on-screen
        // keyboard doesn't set off shortcuts
        set_state(true, false);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

QImage InputMode::nightPointerImage()
{
    QImage image(24, 24, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    const QPointF arrow[] = {{2, 2}, {2, 18}, {6.5, 14}, {9.5, 21}, {12.5, 19.7}, {9.5, 13}, {15.5, 13}};
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(70, 6, 4), 1.2));
    painter.setBrush(QColor(226, 44, 30));
    painter.drawPolygon(arrow, int(std::size(arrow)));
    return image;
}

void InputMode::setNightPointer(bool enabled)
{
    if (enabled == m_night_pointer)
        return;
    m_night_pointer = enabled;
    if (enabled)
        QGuiApplication::setOverrideCursor(QCursor(QPixmap::fromImage(nightPointerImage()), 2, 2));
    else
        QGuiApplication::restoreOverrideCursor();
    emit changed();
}
