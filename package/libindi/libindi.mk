################################################################################
#
# libindi
#
################################################################################

LIBINDI_VERSION = 2.2.5
LIBINDI_SITE = $(call github,indilib,indi,v$(LIBINDI_VERSION))
LIBINDI_LICENSE = LGPL-2.1+
LIBINDI_LICENSE_FILES = COPYING.LGPL
LIBINDI_INSTALL_STAGING = YES
LIBINDI_DEPENDENCIES = cfitsio gsl jpeg libcurl libev libnova libusb zlib
LIBINDI_CONF_OPTS = \
	-DINDI_BUILD_XISF=OFF \
	-DINDI_BUILD_QT_CLIENT=OFF \
	-DINDI_BUILD_EXAMPLES=OFF \
	-DINDI_BUILD_UNITTESTS=OFF \
	-DINDI_BUILD_INTEGTESTS=OFF \
	-DINDI_BUILD_STATIC=OFF

$(eval $(cmake-package))
