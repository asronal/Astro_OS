#ifndef ASTRO_UI_AUTO_DETECT_H
#define ASTRO_UI_AUTO_DETECT_H

#include <QByteArray>
#include <QString>

struct DetectedLocation {
    double latitude;
    double longitude;
    bool has_utc_offset;
    int utc_offset_minutes;
    QString place;
};

// Reads a reply from the location service. Returns false, with the reason in
// problem, if it's an error or doesn't contain a usable position.
bool parse_location_reply(const QByteArray& reply, DetectedLocation* location, QString* problem);

#endif
