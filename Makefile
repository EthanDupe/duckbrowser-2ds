#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET           := duckbrowser
BUILD            := build
SOURCES          := source
DATA             := data
INCLUDES         := include
ROMFS            := romfs

APP_TITLE        := DuckVid Browser 2DS
APP_DESCRIPTION  := DuckDuckGo + Bookmarks + Video for 2DS XL
APP_AUTHOR       := DuckVid 2DS Homebrew
APP_PRODUCT_CODE := CTR-H-DUCK
APP_UNIQUE_ID    := 0xF2D5B
APP_ICON         := $(TOPDIR)/assets/icon.png
BANNER_IMAGE     := $(TOPDIR)/assets/banner.png
BANNER_AUDIO     := $(TOPDIR)/assets/banner.wav
RSF_FILE         := $(TOPDIR)/app.rsf

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -g -Wall -O2 -mword-relocations \
           -fomit-frame-pointer -ffunction-sections \
           $(ARCH) $(INCLUDE) -D__3DS__ -D_GNU_SOURCE -std=gnu99

ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS    := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lcitro2d -lcitro3d -lctru -lm

LIBDIRS := $(PORTLIBS) $(CTRULIB)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export LD := $(CC)
export OFILES := $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export _3DSXDEPS := $(if $(NO_SMDH),,$(OUTPUT).smdh)
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh --romfs=$(CURDIR)/$(ROMFS)

.PHONY: $(BUILD) clean all cia

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

cia: all
	@bannertool makebanner -i $(BANNER_IMAGE) -a $(BANNER_AUDIO) -o $(BUILD)/banner.bnr
	@bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_TITLE) - $(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i $(APP_ICON) -o $(BUILD)/icon.icn
	@makerom -f cia -o $(TOPDIR)/$(TARGET).cia \
		-elf $(TOPDIR)/$(TARGET).elf \
		-rsf $(RSF_FILE) \
		-target t \
		-exefslogo \
		-icon $(BUILD)/icon.icn \
		-banner $(BUILD)/banner.bnr \
		-major 1 -minor 0 -micro 0 \
		-DAPP_TITLE="$(APP_TITLE)" \
		-DAPP_PRODUCT_CODE="$(APP_PRODUCT_CODE)" \
		-DAPP_UNIQUE_ID="$(APP_UNIQUE_ID)" \
		-DAPP_ROMFS="$(TOPDIR)/$(ROMFS)"
	@echo "Successfully built $(TARGET).cia!"

clean:
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf $(TARGET).cia

else
DEPENDS := $(OFILES:.o=.d)
$(OUTPUT).3dsx : $(OUTPUT).elf $(_3DSXDEPS)
$(OUTPUT).elf  : $(OFILES)
-include $(DEPENDS)
endif
