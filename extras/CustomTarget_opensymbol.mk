# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CustomTarget_CustomTarget,extras/fonts))

$(eval $(call gb_CustomTarget_register_targets,extras/fonts,opens___.ttf))

ifneq (,$(FONTFORGE))
$(gb_CustomTarget_workdir)/extras/fonts/opens___.ttf : \
		$(SRCDIR)/extras/source/truetype/symbol/OpenSymbol.sfd
	$(call gb_Output_announce,$(subst $(WORKDIR)/,,$@),$(true),FNT,1)
	$(call gb_Trace_StartRange,$(subst $(WORKDIR)/,,$@),FNT)
	$(FONTFORGE) -lang=ff -c 'Open($$1); Generate($$2)' $< $@
	$(call gb_Trace_EndRange,$(subst $(WORKDIR)/,,$@),FNT)
else
$(gb_CustomTarget_workdir)/extras/fonts/opens___.ttf : \
		$(TARFILE_LOCATION)/$(OPENSYMBOL_TTF)
	cp $< $@
endif

# Microsoft Aptos fonts: copied from the source tree (downloaded by the CI
# workflow before the build) into the package, so gbuild emits them into the
# package file list (openoffice.lst) and they reach the MSI. The guard
# (wildcard) makes this conditional on the fonts being present, so a failed
# pre-build download does not fail the whole build (the MSI then simply
# lacks the fonts, and the non-fatal verification step warns).
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

$(eval $(call gb_CustomTarget_register_targets,extras/fonts,$(APTOS_FONTS)))

define aptos_font_rule
$(gb_CustomTarget_workdir)/extras/fonts/$(1) : $(SRCDIR)/aptos/$(1)
	cp $$< $$@
endef
$(foreach font,$(APTOS_FONTS),$(eval $(call aptos_font_rule,$(font))))
endif
