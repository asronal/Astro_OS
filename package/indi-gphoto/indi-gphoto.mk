################################################################################
#
# indi-gphoto
#
################################################################################

INDI_GPHOTO_VERSION = 2.2.5
INDI_GPHOTO_SOURCE = indi-3rdparty-$(INDI_GPHOTO_VERSION).tar.gz
INDI_GPHOTO_SITE = $(call github,indilib,indi-3rdparty,v$(INDI_GPHOTO_VERSION))
INDI_GPHOTO_DL_SUBDIR = indi-3rdparty
INDI_GPHOTO_SUBDIR = indi-gphoto
INDI_GPHOTO_LICENSE = LGPL-2.1+
INDI_GPHOTO_LICENSE_FILES = indi-gphoto/COPYING.LIB
INDI_GPHOTO_DEPENDENCIES = libgphoto2 libindi libraw
# The default comes from the staging path, which would be installed on the target
INDI_GPHOTO_CONF_OPTS = -DINDI_DATA_DIR=/usr/share/indi

$(eval $(cmake-package))
