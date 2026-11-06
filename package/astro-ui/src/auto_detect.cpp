#include "auto_detect.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

// The service writes offsets as "+HHMM" or "-HHMM"
static bool parse_utc_offset(const QString& text, int* minutes)
{
    if (text.size() != 5 || (text.at(0) != QLatin1Char('+') && text.at(0) != QLatin1Char('-')))
        return false;
    bool hours_ok;
    bool minutes_ok;
    int hours = text.mid(1, 2).toInt(&hours_ok);
    int mins = text.mid(3, 2).toInt(&minutes_ok);
    if (!hours_ok || !minutes_ok || hours > 14 || mins > 59)
        return false;
    *minutes = (hours * 60 + mins) * (text.at(0) == QLatin1Char('-') ? -1 : 1);
    return true;
}

bool parse_location_reply(const QByteArray& reply, DetectedLocation* location, QString* problem)
{
    QJsonDocument document = QJsonDocument::fromJson(reply);
    if (!document.isObject()) {
        *problem = QStringLiteral("the location service sent an unreadable reply");
        return false;
    }

    QJsonObject object = document.object();
    if (object.value(QStringLiteral("error")).toBool()) {
        QString reason = object.value(QStringLiteral("reason")).toString();
        *problem = QStringLiteral("the location service refused the request (%1)").arg(reason.isEmpty() ? QStringLiteral("no reason given") : reason);
        return false;
    }

    QJsonValue latitude = object.value(QStringLiteral("latitude"));
    QJsonValue longitude = object.value(QStringLiteral("longitude"));
    if (!latitude.isDouble() || !longitude.isDouble() || latitude.toDouble() < -90.0 || latitude.toDouble() > 90.0 ||
        longitude.toDouble() < -180.0 || longitude.toDouble() > 180.0) {
        *problem = QStringLiteral("the location service didn't give a usable position");
        return false;
    }

    location->latitude = latitude.toDouble();
    location->longitude = longitude.toDouble();
    location->has_utc_offset = parse_utc_offset(object.value(QStringLiteral("utc_offset")).toString(), &location->utc_offset_minutes);

    QStringList parts;
    for (const char* key : {"city", "country_name"}) {
        QString part = object.value(QLatin1String(key)).toString();
        if (!part.isEmpty())
            parts << part;
    }
    location->place = parts.join(QStringLiteral(", "));
    return true;
}
