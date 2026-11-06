################################################################################
#
# astro-ui
#
################################################################################

ASTRO_UI_VERSION = $(shell cat $(BR2_EXTERNAL_ASTRO_PATH)/VERSION)
ASTRO_UI_SITE = $(BR2_EXTERNAL_ASTRO_PATH)/package/astro-ui
ASTRO_UI_SITE_METHOD = local
ASTRO_UI_SUPPORTS_IN_SOURCE_BUILD = NO
ASTRO_UI_CMAKE_BACKEND = ninja
ASTRO_UI_DEPENDENCIES = astro-core libindi qt6base qt6declarative qt6virtualkeyboard qt6wayland readline tcl
ASTRO_UI_CONF_OPTS = -DQT_HOST_PATH=$(HOST_DIR)

$(eval $(cmake-package))
