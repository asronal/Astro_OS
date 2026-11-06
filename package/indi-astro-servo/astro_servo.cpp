// INDI driver for pointing a telescope with two RC servos on a PCA9685 board.
// For pointing and visual use only: RC servos are too coarse and jittery to
// track during long exposures.

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstring>
#include <memory>

#include <indicom.h>
#include <inditelescope.h>
#include <libnova/julian_day.h>
#include <libnova/transform.h>

#include "servo_math.h"

namespace {

const char kI2cDevice[] = "/dev/i2c-1";
const int kPcaAddress = 0x40;
const int kAzChannel = 0;
const int kAltChannel = 1;

const uint8_t kRegMode1 = 0x00;
const uint8_t kRegLed0OnLow = 0x06;
const uint8_t kRegPrescale = 0xFE;
const uint8_t kMode1Sleep = 0x10;
const uint8_t kMode1AutoIncrement = 0x20;
const uint8_t kMode1Restart = 0x80;

const uint32_t kPollMs = 200;
const double kMaxDegreesPerSecond = 10.0;
const double kJogDegreesPerSecond = 3.0;
const double kArrivedDeg = 0.05;

// Azimuth 180 (south) at the servo's centre, reaching east and west. Altitude
// from the horizon to the zenith, so the telescope cannot point at the ground.
const servo::AxisLimits kAzLimits = {180.0, 90.0};
const servo::AxisLimits kAltLimits = {45.0, 45.0};

} // namespace

class AstroServo : public INDI::Telescope
{
public:
    AstroServo()
    {
        setVersion(1, 0);
    }

    const char *getDefaultName() override
    {
        return "Astro RC Servo";
    }

    bool initProperties() override
    {
        INDI::Telescope::initProperties();
        SetTelescopeCapability(TELESCOPE_CAN_GOTO | TELESCOPE_CAN_SYNC | TELESCOPE_CAN_ABORT |
                               TELESCOPE_CAN_CONTROL_TRACK | TELESCOPE_HAS_LOCATION | TELESCOPE_HAS_TIME, 0);
        setTelescopeConnection(CONNECTION_NONE);
        setDefaultPollingPeriod(kPollMs);
        addDebugControl();
        return true;
    }

    bool Connect() override
    {
        m_fd = open(kI2cDevice, O_RDWR);
        if (m_fd < 0 || ioctl(m_fd, I2C_SLAVE, kPcaAddress) < 0) {
            LOGF_ERROR("Could not open the PCA9685 at %s address 0x%02x: %s", kI2cDevice, kPcaAddress, strerror(errno));
            closeDevice();
            return false;
        }

        // The prescale can only change while the chip is asleep
        if (!writeRegister(kRegMode1, kMode1Sleep) ||
            !writeRegister(kRegPrescale, uint8_t(servo::pca_prescale())) ||
            !writeRegister(kRegMode1, kMode1AutoIncrement)) {
            LOG_ERROR("The PCA9685 did not respond.");
            closeDevice();
            return false;
        }
        usleep(1000);
        writeRegister(kRegMode1, kMode1AutoIncrement | kMode1Restart);

        // Start from the centre of travel
        m_az = m_target_az = kAzLimits.center_deg;
        m_alt = m_target_alt = kAltLimits.center_deg;
        if (!writeServos())
            return false;

        LOG_WARN("RC servos are for pointing and visual use, and are not accurate enough for long exposures.");
        SetTimer(getCurrentPollingPeriod());
        return true;
    }

    bool Disconnect() override
    {
        closeDevice();
        return true;
    }

    bool ReadScopeStatus() override
    {
        double jd = ln_get_julian_from_sys();
        double max_step = kMaxDegreesPerSecond * kPollMs / 1000.0;

        if (m_following)
            equatorialToHorizontal(m_target_ra, m_target_dec, jd, &m_target_az, &m_target_alt);

        if (m_jog_az != 0)
            m_target_az = servo::clamp_to_axis(m_target_az + m_jog_az * kJogDegreesPerSecond * kPollMs / 1000.0, kAzLimits);
        if (m_jog_alt != 0)
            m_target_alt = servo::clamp_to_axis(m_target_alt + m_jog_alt * kJogDegreesPerSecond * kPollMs / 1000.0, kAltLimits);

        // Stop following when the sky carries the target out of reach
        if (m_following && (!servo::within_axis(m_target_az, kAzLimits) || !servo::within_axis(m_target_alt, kAltLimits))) {
            LOG_WARN("The target has moved outside the servo travel, so tracking has stopped.");
            m_following = false;
            m_target_az = m_az;
            m_target_alt = m_alt;
            TrackState = SCOPE_IDLE;
        }

        m_az = servo::step_towards(m_az, m_target_az, max_step);
        m_alt = servo::step_towards(m_alt, m_target_alt, max_step);
        if (!writeServos()) {
            LOG_ERROR("Lost contact with the PCA9685.");
            return false;
        }

        bool arrived = std::fabs(m_az - m_target_az) < kArrivedDeg && std::fabs(m_alt - m_target_alt) < kArrivedDeg;
        if (TrackState == SCOPE_SLEWING && arrived) {
            TrackState = m_following ? SCOPE_TRACKING : SCOPE_IDLE;
            LOG_INFO("Arrived at the target.");
        }

        double ra_hours;
        double dec_deg;
        horizontalToEquatorial(m_az - m_sync_az, m_alt - m_sync_alt, jd, &ra_hours, &dec_deg);
        NewRaDec(ra_hours, dec_deg);
        return true;
    }

    bool Goto(double ra_hours, double dec_deg) override
    {
        double az;
        double alt;

        equatorialToHorizontal(ra_hours, dec_deg, ln_get_julian_from_sys(), &az, &alt);
        if (!servo::within_axis(az, kAzLimits) || !servo::within_axis(alt, kAltLimits)) {
            LOGF_ERROR("Azimuth %.1f, altitude %.1f is outside the servo travel.", az, alt);
            return false;
        }

        m_target_ra = ra_hours;
        m_target_dec = dec_deg;
        m_target_az = az;
        m_target_alt = alt;
        m_following = true;
        TrackState = SCOPE_SLEWING;
        return true;
    }

    bool Sync(double ra_hours, double dec_deg) override
    {
        double az;
        double alt;

        // The offset between where the servos are and where the sky says they are
        equatorialToHorizontal(ra_hours, dec_deg, ln_get_julian_from_sys(), &az, &alt);
        m_sync_az = servo::wrap_180(m_az - az);
        m_sync_alt = m_alt - alt;
        NewRaDec(ra_hours, dec_deg);
        return true;
    }

    bool Abort() override
    {
        m_following = false;
        m_jog_az = 0;
        m_jog_alt = 0;
        m_target_az = m_az;
        m_target_alt = m_alt;
        TrackState = SCOPE_IDLE;
        return true;
    }

    bool MoveNS(INDI_DIR_NS dir, TelescopeMotionCommand command) override
    {
        m_following = false;
        m_jog_alt = command == MOTION_START ? (dir == DIRECTION_NORTH ? 1 : -1) : 0;
        return true;
    }

    bool MoveWE(INDI_DIR_WE dir, TelescopeMotionCommand command) override
    {
        m_following = false;
        m_jog_az = command == MOTION_START ? (dir == DIRECTION_WEST ? 1 : -1) : 0;
        return true;
    }

    bool SetTrackEnabled(bool enabled) override
    {
        if (!enabled) {
            m_following = false;
            return true;
        }
        m_target_ra = EqNP[AXIS_RA].getValue();
        m_target_dec = EqNP[AXIS_DE].getValue();
        m_following = true;
        return true;
    }

    bool updateLocation(double latitude, double longitude, double) override
    {
        m_latitude = latitude;
        m_longitude = longitude;
        return true;
    }

private:
    void closeDevice()
    {
        if (m_fd >= 0)
            close(m_fd);
        m_fd = -1;
    }

    bool writeRegister(uint8_t reg, uint8_t value)
    {
        uint8_t message[2] = {reg, value};
        return write(m_fd, message, sizeof(message)) == ssize_t(sizeof(message));
    }

    bool writeChannel(int channel, double pulse_us)
    {
        int off = servo::pulse_to_pca_counts(pulse_us);
        uint8_t message[5] = {
            uint8_t(kRegLed0OnLow + 4 * channel), 0, 0, uint8_t(off & 0xFF), uint8_t((off >> 8) & 0x0F),
        };
        return write(m_fd, message, sizeof(message)) == ssize_t(sizeof(message));
    }

    bool writeServos()
    {
        return writeChannel(kAzChannel, servo::angle_to_pulse_us(m_az, kAzLimits)) &&
               writeChannel(kAltChannel, servo::angle_to_pulse_us(m_alt, kAltLimits));
    }

    ln_lnlat_posn observer() const
    {
        ln_lnlat_posn position;
        position.lng = m_longitude > 180.0 ? m_longitude - 360.0 : m_longitude;
        position.lat = m_latitude;
        return position;
    }

    void equatorialToHorizontal(double ra_hours, double dec_deg, double jd, double *az_deg, double *alt_deg) const
    {
        ln_equ_posn equatorial;
        ln_hrz_posn horizontal;
        ln_lnlat_posn position = observer();

        equatorial.ra = ra_hours * 15.0;
        equatorial.dec = dec_deg;
        ln_get_hrz_from_equ(&equatorial, &position, jd, &horizontal);
        // libnova measures azimuth from the south
        *az_deg = std::fmod(horizontal.az + 180.0, 360.0);
        *alt_deg = horizontal.alt;
    }

    void horizontalToEquatorial(double az_deg, double alt_deg, double jd, double *ra_hours, double *dec_deg) const
    {
        ln_hrz_posn horizontal;
        ln_equ_posn equatorial;
        ln_lnlat_posn position = observer();

        horizontal.az = std::fmod(az_deg + 180.0, 360.0);
        horizontal.alt = alt_deg;
        ln_get_equ_from_hrz(&horizontal, &position, jd, &equatorial);
        *ra_hours = equatorial.ra / 15.0;
        *dec_deg = equatorial.dec;
    }

    int m_fd = -1;
    double m_latitude = 0.0;
    double m_longitude = 0.0;
    double m_az = kAzLimits.center_deg;
    double m_alt = kAltLimits.center_deg;
    double m_target_az = kAzLimits.center_deg;
    double m_target_alt = kAltLimits.center_deg;
    double m_target_ra = 0.0;
    double m_target_dec = 0.0;
    double m_sync_az = 0.0;
    double m_sync_alt = 0.0;
    int m_jog_az = 0;
    int m_jog_alt = 0;
    bool m_following = false;
};

static std::unique_ptr<AstroServo> astroServo(new AstroServo());
