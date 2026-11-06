// Checks how replies from the location service are read, including bad ones.
#include <cmath>
#include <cstdio>

#include "auto_detect.h"

static int failures = 0;
static int checks = 0;

static void check(const char* label, bool ok)
{
    checks++;
    if (!ok) {
        failures++;
        std::printf("FAIL %s\n", label);
    }
}

int main()
{
    DetectedLocation l;
    QString problem;

    check("A normal reply is accepted", parse_location_reply(R"({"city":"Mumbai","country_name":"India","latitude":19.0728,"longitude":72.8826,"utc_offset":"+0530"})", &l, &problem));
    check("Latitude", std::fabs(l.latitude - 19.0728) < 1e-9);
    check("Longitude", std::fabs(l.longitude - 72.8826) < 1e-9);
    check("A half-hour offset", l.has_utc_offset && l.utc_offset_minutes == 330);
    check("The place", l.place == QStringLiteral("Mumbai, India"));

    check("A western reply is accepted", parse_location_reply(R"({"city":"San Francisco","latitude":37.7697,"longitude":-122.3933,"utc_offset":"-0800"})", &l, &problem));
    check("A negative offset", l.has_utc_offset && l.utc_offset_minutes == -480);
    check("A negative longitude", l.longitude < 0.0);
    check("A place with no country", l.place == QStringLiteral("San Francisco"));

    check("A position with no offset is still useful", parse_location_reply(R"({"latitude":1.5,"longitude":2.5})", &l, &problem));
    check("A missing offset is reported as missing", !l.has_utc_offset);

    check("A malformed offset is ignored", parse_location_reply(R"({"latitude":1.5,"longitude":2.5,"utc_offset":"soon"})", &l, &problem) && !l.has_utc_offset);
    check("An impossible offset is ignored", parse_location_reply(R"({"latitude":1.5,"longitude":2.5,"utc_offset":"+9900"})", &l, &problem) && !l.has_utc_offset);

    // Latitude 0 and longitude 0 are valid places, so must not count as missing
    check("Zero is a real position", parse_location_reply(R"({"latitude":0,"longitude":0})", &l, &problem));

    check("A rate limit reply is refused", !parse_location_reply(R"({"error":true,"reason":"RateLimited","message":"slow down"})", &l, &problem));
    check("The reason is reported", problem.contains(QStringLiteral("RateLimited")));
    check("A reserved address reply is refused", !parse_location_reply(R"({"ip":"10.0.0.1","error":true,"reason":"Reserved IP Address"})", &l, &problem));
    check("A reply with no position is refused", !parse_location_reply(R"({"city":"Nowhere"})", &l, &problem));
    check("Text instead of numbers is refused", !parse_location_reply(R"({"latitude":"51","longitude":"0"})", &l, &problem));
    check("A latitude out of range is refused", !parse_location_reply(R"({"latitude":91,"longitude":0})", &l, &problem));
    check("A longitude out of range is refused", !parse_location_reply(R"({"latitude":0,"longitude":181})", &l, &problem));
    check("Something that isn't JSON is refused", !parse_location_reply("<html>Sign in to this network</html>", &l, &problem));
    check("An empty reply is refused", !parse_location_reply("", &l, &problem));

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
