# Copyright 2026, the Meshy for Haiku contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds on Haiku/Renku, VitruvianOS (V\OS), and Linux with Cosmoe.
#
#   make           build the application
#   make clean     remove build products
#   make tests     build and run the host-side core tests (any OS)
#   make install   copy the binary to your apps folder
#   make hpkg      build a Haiku .hpkg (on Haiku/Renku)
#   make deb       build a V\OS .deb (on V\OS or with VOS headers)

NAME := Meshy
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
UNAME    := $(shell uname -s)
VOS_HEADERS ?= /system/develop/headers
COSMOE_ROOT ?= /usr

SRC_DIR := src
CORE_DIR := $(SRC_DIR)/core
UI_DIR   := $(SRC_DIR)/haiku

CORE_SRCS := \
	Bytes.cpp Crypto.cpp Smaz.cpp Protocol.cpp MeshPacket.cpp \
	Models.cpp Storage.cpp Transport.cpp Engine.cpp

UI_SRCS := \
	MeshyApp.cpp MainWindow.cpp EngineBridge.cpp UiUtils.cpp \
	ConnectionView.cpp ContactsView.cpp ChatView.cpp ChannelsView.cpp \
	ChannelEditWindow.cpp MapView.cpp DeviceView.cpp RepeaterView.cpp \
	SensorView.cpp PacketLogView.cpp SettingsView.cpp

RDEFS := data/Meshy.rdef

# ── Platform detection ──

ifeq ($(UNAME),Haiku)

  PLATFORM     := Haiku
  MAKEFILE_ENGINE := /boot/system/data/build/scripts/makefile-engine
  LIBS         := be network sqlite3 crypto root
  SRCS         := $(addprefix $(CORE_DIR)/,$(CORE_SRCS)) $(addprefix $(UI_DIR)/,$(UI_SRCS))
  LOCAL_INCLUDE_PATHS := $(CORE_DIR) $(UI_DIR)
  OPTIMIZE     := FULL
  DEBUGGER     := FALSE
  WARNINGS     := ALL
  COMPILER_FLAGS := -std=c++17
  CXXFLAGS_EXTRA := -std=c++17
  INSTALL_DIR  := $(shell finddir B_USER_NONPACKAGED_DIRECTORY)/apps
  RESOURCES    = rc -o obj/Meshy.rsrc $(RDEFS) && xres -o $@ obj/Meshy.rsrc

  ifneq ($(wildcard $(MAKEFILE_ENGINE)),)
    DEVEL_DIRECTORY := /boot/system/develop
    include $(MAKEFILE_ENGINE)
  else
    # Fallback for Haiku without makefile-engine
    OBJS := $(patsubst %.cpp,obj/%.o,$(notdir $(CORE_SRCS) $(UI_SRCS)))
    VPATH := $(CORE_DIR):$(UI_DIR)

    all: $(NAME)

    obj/%.o: %.cpp
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c $< -o $@

    $(NAME): $(OBJS) $(RDEFS)
	$(CXX) $(LDFLAGS) $(OBJS) -o $@ $(LIBS)
	$(RESOURCES)

    clean:
	rm -rf obj $(NAME)
  endif

else
  # VitruvianOS: the Haiku API on a Linux kernel.
  ifeq ($(shell pkg-config --exists vos 2>/dev/null && echo yes),yes)

    PLATFORM    := VitruvianOS
    override CXXFLAGS += $(shell pkg-config --cflags vos) -fPIC
    LIBS        := $(shell pkg-config --libs vos) -ltracker -ltranslation
    INSTALL_DIR := $(or $(shell finddir B_USER_NONPACKAGED_DIRECTORY 2>/dev/null),$(HOME)/config/non-packaged)/apps
    RESOURCES    = rc -o $(CURDIR)/obj/Meshy.rsrc $(CURDIR)/$(RDEFS) \
                   && xres -o $(CURDIR)/$@ $(CURDIR)/obj/Meshy.rsrc \
                   && resattr -O -o $(CURDIR)/$@ $(CURDIR)/obj/Meshy.rsrc

  else ifneq ($(wildcard $(VOS_HEADERS)/os/BeBuild.h),)
    # VitruvianOS 0.6.x — headers ship in /system/develop/headers.
    PLATFORM    := VitruvianOS
    VOS_ARCH := $(if $(filter aarch64,$(shell uname -m)),arm64,$(shell uname -m))
    VOS_DIRS    := . os os/app os/drivers os/kernel os/support os/storage \
                   os/translation os/config os/add-ons/file_system \
                   os/add-ons/graphics os/add-ons/registrar \
                   os/add-ons/network_settings os/arch/$(VOS_ARCH) os/device \
                   os/drivers/bus os/game os/interface os/locale \
                   os/locale/tools os/mail os/net os/posix/compat/sys \
                   os/add-ons/input_server os/add-ons/tracker os/opengl \
                   os/media2 posix build build/config_headers config
    override CXXFLAGS += $(addprefix -I$(VOS_HEADERS)/,$(VOS_DIRS)) \
                   -include LinuxBuildCompatibility.h -fPIC
    LIBS        := -lbe -lroot -ltracker -ltranslation
    INSTALL_DIR := $(or $(shell finddir B_USER_NONPACKAGED_DIRECTORY 2>/dev/null),$(HOME)/config/non-packaged)/apps
    RESOURCES    = rc -o $(CURDIR)/obj/Meshy.rsrc $(CURDIR)/$(RDEFS) \
                   && xres -o $(CURDIR)/$@ $(CURDIR)/obj/Meshy.rsrc \
                   && resattr -O -o $(CURDIR)/$@ $(CURDIR)/obj/Meshy.rsrc

  else ifneq ($(wildcard $(COSMOE_ROOT)/include/os/app/Application.h),)
    # Plain Linux with Cosmoe.
    PLATFORM    := Cosmoe
    COSMOE_DIRS := config os os/app os/game os/interface os/kernel os/locale \
                   os/media os/net os/storage os/support os/translation \
                   os/be_apps/Tracker private/interface private/shared \
                   private/support posix
    override CXXFLAGS += -I$(COSMOE_ROOT)/include \
                   $(addprefix -I$(COSMOE_ROOT)/include/,$(COSMOE_DIRS)) \
                   -DMESHY_COSMOE -fPIC
    COSMOE_RPATH ?= $$ORIGIN/../lib
    LIBS        := -L$(COSMOE_ROOT)/lib -lbe -ltracker \
                   -Wl,-rpath,'$(COSMOE_RPATH)'
    INSTALL_DIR := $(HOME)/.local/bin
    EXTRA_OBJS  := obj/Meshy.rsrc.o
    RESOURCES    = true

    obj/Meshy.rsrc.o: $(RDEFS)
	@mkdir -p obj
	LD_LIBRARY_PATH=$(COSMOE_ROOT)/lib $(COSMOE_ROOT)/bin/rc \
		-o obj/Meshy.rsrc $(RDEFS)
	cd obj && ld -r -b binary Meshy.rsrc -o Meshy.rsrc.o
	objcopy --rename-section .data=.cosmoe_rsrc \
		--add-section .note.GNU-stack=/dev/null \
		--set-section-flags .note.GNU-stack=noload,readonly obj/Meshy.rsrc.o

  else
    PLATFORM    := none
  endif
endif

# ── Common build rules (for non-Haiku platforms) ──

ifeq ($(PLATFORM),Haiku)
  # Haiku: use makefile-engine or fallback (defined above)
else ifeq ($(PLATFORM),none)

  all:
	@echo "Meshy needs Haiku, VitruvianOS, or Linux with Cosmoe."
	@echo "On V\OS it uses 'pkg-config vos' (vos-dev) or headers in $(VOS_HEADERS)."
	@echo "On Linux, Cosmoe installed under COSMOE_ROOT=$(COSMOE_ROOT)."
	@echo "(Core tests run anywhere: make tests)"
	@false

else

  OBJS := $(patsubst %.cpp,obj/%.o,$(notdir $(CORE_SRCS) $(UI_SRCS)))

  all: check-platform $(NAME)

  check-platform:
	@true

  obj/%.o: src/%.cpp src/*.h
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c $< -o $@

  $(NAME): $(OBJS) $(EXTRA_OBJS) $(RDEFS)
	$(CXX) $(LDFLAGS) $(OBJS) $(EXTRA_OBJS) -o $@ $(LIBS)
	$(RESOURCES)
	@if command -v mimeset >/dev/null 2>&1; then mimeset -f $@; fi

  deb: all
	sh packaging/build-deb.sh

  hpkg:
	sh packaging/haiku/build-hpkg.sh

  install: all
	mkdir -p "$(INSTALL_DIR)"
	cp $(NAME) "$(INSTALL_DIR)/"
	@if command -v mimeset >/dev/null 2>&1; then mimeset -f "$(INSTALL_DIR)/$(NAME)"; fi

  clean:
	rm -rf obj $(NAME)

  .PHONY: all check-platform deb hpkg install clean
endif

# ── Host-side core tests (no Haiku headers needed) ──
TEST_LDLIBS := -lsqlite3 -lcrypto -lpthread
TEST_CXXFLAGS := -std=c++17 -Wall -Wextra -I$(CORE_DIR)

tests:
	@mkdir -p build
	$(CXX) $(TEST_CXXFLAGS) tests/test_crypto.cpp $(addprefix $(CORE_DIR)/,$(CORE_SRCS)) $(TEST_LDLIBS) \
		-o build/test_crypto && ./build/test_crypto
	$(CXX) $(TEST_CXXFLAGS) tests/test_storage.cpp $(addprefix $(CORE_DIR)/,$(CORE_SRCS)) $(TEST_LDLIBS) \
		-o build/test_storage && ./build/test_storage /tmp/meshy_test.db
	$(CXX) $(TEST_CXXFLAGS) tests/test_transport.cpp $(addprefix $(CORE_DIR)/,$(CORE_SRCS)) $(TEST_LDLIBS) \
		-o build/test_transport && ./build/test_transport
	$(CXX) $(TEST_CXXFLAGS) tests/test_engine.cpp $(addprefix $(CORE_DIR)/,$(CORE_SRCS)) $(TEST_LDLIBS) \
		-o build/test_engine && ./build/test_engine

.PHONY: tests
