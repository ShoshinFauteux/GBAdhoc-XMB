# psp/Makefile -- GBAdhoc XMB, the standalone PSP app that makes a
# game's own XMB entry (docs/XMB-LAUNCH.md).  Build in the pspdev image:
#   docker run --rm -v <repo>:/build -w /build/psp pspdev/pspdev make
# Generated headers (commit them; regenerate with the tools/ scripts):
#   gen/xmb_fonts.h  tools/bake_fonts.py     gen/xmb_art.h  tools/bake_art.py
#   gen/xmb_names.h  tools/make_names.py     gen/launcher_pbp.h tools/embed_launcher.py
#
# LIBS names only what neither psp-gcc's spec (-lpsputility -lpsprtc
# -lpspnet_inet -lpspnet_resolver) nor build.mak's tail (-lpspnet
# -lpspnet_apctl) already links (memory: psp-stub-order-trap).
TARGET = xmbapp
OBJS   = main.o gfx.o theme.o ui.o ui_kit.o jobs.o model.o names.o art.o util.o net.o xw_core_psp.o
CFLAGS = -O2 -G0 -MMD -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -Wno-missing-field-initializers
ASFLAGS = $(CFLAGS)
LDFLAGS = -Wl,-Map,xmbapp.map
LIBS   = -lpspgu -lmbedtls -lmbedx509 -lmbedcrypto -lz -lm -lpspwlan -lpsppower
EXTRA_TARGETS   = EBOOT.PBP
PSP_EBOOT_TITLE = GBAdhoc XMB
PSP_EBOOT_ICON  = assets/ICON0.PNG
PSP_EBOOT_PIC1  = assets/PIC1.PNG

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

check: EBOOT.PBP
	@! grep -q "fixup-imports" build.log 2>/dev/null

-include $(OBJS:.o=.d)
