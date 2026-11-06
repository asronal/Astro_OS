################################################################################
#
# astro-core
#
################################################################################

ASTRO_CORE_VERSION = $(shell cat $(BR2_EXTERNAL_ASTRO_PATH)/VERSION)
ASTRO_CORE_SITE = $(BR2_EXTERNAL_ASTRO_PATH)/package/astro-core
ASTRO_CORE_SITE_METHOD = local
ASTRO_CORE_INSTALL_STAGING = YES
ASTRO_CORE_DEPENDENCIES = libnova

$(eval $(cmake-package))
