# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_Package_Package,extras_fonts,$(gb_CustomTarget_workdir)/extras/fonts))

$(eval $(call gb_Package_use_customtarget,extras_fonts,extras/fonts))

$(eval $(call gb_Package_add_file,extras_fonts,$(LIBO_SHARE_FOLDER)/fonts/truetype/opens___.ttf,opens___.ttf))

# Microsoft Aptos fonts: added to the package only when the pre-build
# download placed them in the source tree (see
# extras/CustomTarget_opensymbol.mk for the copy rule). The guard keeps a
# failed download from failing the build; when it trips, the MSI simply
# lacks the fonts and the non-fatal verification step warns.
ifneq (,$(wildcard $(SRCDIR)/aptos/Aptos.ttf))
APTOS_FONTS := Aptos.ttf \
	Aptos-Black.ttf \
	Aptos-Black-Italic.ttf \
	Aptos-Bold.ttf \
	Aptos-Bold-Italic.ttf \
	Aptos-Display.ttf \
	Aptos-Display-Bold.ttf \
	Aptos-Display-Bold-Italic.ttf \
	Aptos-Display-Italic.ttf \
	Aptos-ExtraBold.ttf \
	Aptos-ExtraBold-Italic.ttf \
	Aptos-Italic.ttf \
	Aptos-Light.ttf \
	Aptos-Light-Italic.ttf \
	Aptos-Mono.ttf \
	Aptos-Mono-Bold.ttf \
	Aptos-Mono-Bold-Italic.ttf \
	Aptos-Mono-Italic.ttf \
	Aptos-Narrow.ttf \
	Aptos-Narrow-Bold.ttf \
	Aptos-Narrow-Bold-Italic.ttf \
	Aptos-Narrow-Italic.ttf \
	Aptos-SemiBold.ttf \
	Aptos-SemiBold-Italic.ttf \
	Aptos-Serif.ttf \
	Aptos-Serif-Bold.ttf \
	Aptos-Serif-Bold-Italic.ttf \
	Aptos-Serif-Italic.ttf
$(eval $(call gb_Package_add_files,extras_fonts,$(LIBO_SHARE_FOLDER)/fonts/truetype,$(APTOS_FONTS)))
endif

# vim: set noet sw=4 ts=4:
