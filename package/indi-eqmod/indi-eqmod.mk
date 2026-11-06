################################################################################
#
# indi-eqmod
#
################################################################################

INDI_EQMOD_VERSION = 2.2.5
INDI_EQMOD_SOURCE = indi-3rdparty-$(INDI_EQMOD_VERSION).tar.gz
INDI_EQMOD_SITE = $(call github,indilib,indi-3rdparty,v$(INDI_EQMOD_VERSION))
INDI_EQMOD_DL_SUBDIR = indi-3rdparty
INDI_EQMOD_SUBDIR = indi-eqmod
INDI_EQMOD_LICENSE = LGPL-2.1+
INDI_EQMOD_LICENSE_FILES = LICENSE
INDI_EQMOD_DEPENDENCIES = libindi

$(eval $(cmake-package))
