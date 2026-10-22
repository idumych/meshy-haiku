# Copyright 2026, the Meshy for Haiku contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Haiku build. Uses the system makefile-engine when it is present (the normal
# case on Haiku) and falls back to a plain g++ build otherwise.
#
#   make           build the application
#   make clean     remove build products
#   make tests     build and run the host-side core tests (Linux or Haiku)
#   make install   copy the binary to ~/config/non-packaged/bin

NAME := Meshy
TYPE := APP
APP_MIME_SIG := application/x-vnd.Meshy-Haiku

SRC_DIR := src
CORE_DIR := $(SRC_DIR)/core
HAIKU_DIR := $(SRC_DIR)/haiku

CORE_SRCS := \
	Bytes.cpp \
	Crypto.cpp \
	Smaz.cpp \
	Protocol.cpp \
	MeshPacket.cpp \
	Models.cpp \
	Storage.cpp \
	Transport.cpp \
	Engine.cpp

HAIKU_SRCS := \
	MeshyApp.cpp \
	MainWindow.cpp \
	EngineBridge.cpp \
	UiUtils.cpp \
	ConnectionView.cpp \
	ContactsView.cpp \
	ChatView.cpp \
	ChannelsView.cpp \
	ChannelEditWindow.cpp \
	MapView.cpp \
	DeviceView.cpp \
	RepeaterView.cpp \
	SensorView.cpp \
	PacketLogView.cpp \
	SettingsView.cpp

# makefile-engine wants bare file names plus a search path.
SRCS := $(addprefix $(CORE_DIR)/,$(CORE_SRCS)) $(addprefix $(HAIKU_DIR)/,$(HAIKU_SRCS))

RDEFS := data/Meshy.rdef
LIBS := be network sqlite3 crypto root
LIBPATHS :=
SYSTEM_INCLUDE_PATHS :=
LOCAL_INCLUDE_PATHS := $(CORE_DIR) $(HAIKU_DIR)
OPTIMIZE := FULL
DEBUGGER := FALSE
WARNINGS := ALL
COMPILER_FLAGS := -std=c++17
CXXFLAGS_EXTRA := -std=c++17

MAKEFILE_ENGINE := /boot/system/data/build/scripts/makefile-engine

ifneq ($(wildcard $(MAKEFILE_ENGINE)),)

DEVEL_DIRECTORY := /boot/system/develop
include $(MAKEFILE_ENGINE)

else

# ── Fallback build (also used for the host-side tests) ──
CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
INCLUDES := -I$(CORE_DIR) -I$(HAIKU_DIR)
OBJDIR := build/obj
BIN := build/$(NAME)

OBJS := $(patsubst %.cpp,$(OBJDIR)/%.o,$(notdir $(SRCS)))
VPATH := $(CORE_DIR):$(HAIKU_DIR)

LDLIBS_HAIKU := -lbe -lnetwork -lsqlite3 -lcrypto
LDLIBS_HOST := -lsqlite3 -lcrypto -lpthread

UNAME := $(shell uname -s)
ifeq ($(UNAME),Haiku)
LDLIBS := $(LDLIBS_HAIKU)
else
LDLIBS := $(LDLIBS_HOST)
endif

all: $(BIN)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BIN): $(OBJS)
	$(CXX) $(OBJS) $(LDLIBS) -o $@

clean:
	rm -rf build

install: $(BIN)
	@mkdir -p $(HOME)/config/non-packaged/bin
	cp $(BIN) $(HOME)/config/non-packaged/bin/$(NAME)

.PHONY: all clean install

endif

# ── Host-side core tests (no Haiku headers needed) ──
TEST_SRCS := $(addprefix $(CORE_DIR)/,$(CORE_SRCS))
TEST_LDLIBS := -lsqlite3 -lcrypto -lpthread
TEST_CXXFLAGS := -std=c++17 -Wall -Wextra -I$(CORE_DIR)

tests:
	@mkdir -p build
	$(CXX) $(TEST_CXXFLAGS) tests/test_crypto.cpp $(TEST_SRCS) $(TEST_LDLIBS) \
		-o build/test_crypto && ./build/test_crypto
	$(CXX) $(TEST_CXXFLAGS) tests/test_storage.cpp $(TEST_SRCS) $(TEST_LDLIBS) \
		-o build/test_storage && ./build/test_storage /tmp/meshy_test.db
	$(CXX) $(TEST_CXXFLAGS) tests/test_transport.cpp $(TEST_SRCS) $(TEST_LDLIBS) \
		-o build/test_transport && ./build/test_transport
	$(CXX) $(TEST_CXXFLAGS) tests/test_engine.cpp $(TEST_SRCS) $(TEST_LDLIBS) \
		-o build/test_engine && ./build/test_engine

.PHONY: tests
