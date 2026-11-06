################################################################################
#
# cfitsio
#
################################################################################

CFITSIO_VERSION = 4.7.0
CFITSIO_SITE = $(call github,HEASARC,cfitsio,cfitsio-$(CFITSIO_VERSION))
CFITSIO_LICENSE = CFITSIO
CFITSIO_LICENSE_FILES = licenses/License.txt
CFITSIO_INSTALL_STAGING = YES
CFITSIO_DEPENDENCIES = zlib
CFITSIO_CONF_OPTS = --disable-curl

$(eval $(autotools-package))
