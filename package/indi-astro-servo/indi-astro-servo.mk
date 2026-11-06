################################################################################
#
# indi-astro-servo
#
################################################################################

INDI_ASTRO_SERVO_VERSION = $(shell cat $(BR2_EXTERNAL_ASTRO_PATH)/VERSION)
INDI_ASTRO_SERVO_SITE = $(BR2_EXTERNAL_ASTRO_PATH)/package/indi-astro-servo
INDI_ASTRO_SERVO_SITE_METHOD = local
INDI_ASTRO_SERVO_DEPENDENCIES = libindi libnova

$(eval $(cmake-package))
