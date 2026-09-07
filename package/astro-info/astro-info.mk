################################################################################
#
# astro-info
#
################################################################################

ASTRO_INFO_VERSION = 0.6.0
ASTRO_INFO_SITE = $(BR2_EXTERNAL_ASTRO_PATH)/package/astro-info
ASTRO_INFO_SITE_METHOD = local

define ASTRO_INFO_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-o $(@D)/astro-info $(@D)/astro-info.c
endef

define ASTRO_INFO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/astro-info \
		$(TARGET_DIR)/usr/bin/astro-info
endef

$(eval $(generic-package))
