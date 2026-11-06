################################################################################
#
# libnova
#
################################################################################

LIBNOVA_VERSION = 0.15.0
LIBNOVA_SITE = https://downloads.sourceforge.net/project/libnova/libnova/v%200.15.0
LIBNOVA_LICENSE = LGPL-2.0+
LIBNOVA_LICENSE_FILES = COPYING
LIBNOVA_INSTALL_STAGING = YES
# The release tarball mixes libtool versions, so regenerate its build files
LIBNOVA_AUTORECONF = YES

$(eval $(autotools-package))
