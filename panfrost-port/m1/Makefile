# M1 bring-up tool -- kbase via this device's r28p0 UAPI.
#
#   make host      build + run the host simulation (no device needed)
#   make android   cross-compile for the device (needs ANDROID_NDK_HOME)
#
# SPDX-License-Identifier: MIT

NDK ?= $(ANDROID_NDK_HOME)
TOOLCHAIN := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin
CC_A64 := $(TOOLCHAIN)/aarch64-linux-android31-clang
CC_A32 := $(TOOLCHAIN)/armv7a-linux-androideabi31-clang

CFLAGS_COMMON := -O2 -Wall -Wextra -std=gnu11
CFLAGS_HOST   := $(CFLAGS_COMMON) -g
CFLAGS_DEV    := $(CFLAGS_COMMON) -fPIE -pie

UAPI := ../uapi

.PHONY: all host android clean

all: host

host: verify_layout host_sim
	@echo
	@echo "== layout verification =="
	@./verify_layout
	@echo
	@echo "== simulated bring-up =="
	@./host_sim

verify_layout: $(UAPI)/verify_layout.c $(UAPI)/kbase_uapi_r28p0.h
	$(CC) $(CFLAGS_HOST) -o $@ $<

host_sim: kbase_probe.c host_sim.c kb_sys.h $(UAPI)/kbase_uapi_r28p0.h
	$(CC) $(CFLAGS_HOST) -DHOST_SIM -o $@ kbase_probe.c host_sim.c

android:
	@test -n "$(NDK)" || { echo "set ANDROID_NDK_HOME (or NDK=)"; exit 1; }
	$(CC_A64) $(CFLAGS_DEV) -o kbase_probe_arm64 kbase_probe.c
	$(CC_A32) $(CFLAGS_DEV) -o kbase_probe_arm32 kbase_probe.c
	@echo "built kbase_probe_arm64 and kbase_probe_arm32"

clean:
	rm -f verify_layout host_sim kbase_probe_arm64 kbase_probe_arm32
