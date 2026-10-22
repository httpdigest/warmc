UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# Host personality: MINGW64_NT / CLANG64_NT / MSYS_NT come from MSYS2-family
# shells on Windows; the host build must be static there because the test
# harness spawns warmc with a scrubbed PATH (no DLL resolution).
ifneq (,$(findstring NT,$(UNAME_S)))
  HOST_OS     := windows
  EXE         := .exe
  HOST_TUNE   := -march=x86-64-v3
  # 16 MiB stack: mingw's 2 MiB default overflows on parser recursion that
  # the 8 MiB POSIX default absorbs; thread stacks inherit the same reserve.
  HOST_LDLIBS := -lpthread -static -Wl,--stack,16777216
else ifeq ($(UNAME_S)/$(UNAME_M),Darwin/arm64)
  HOST_OS     := macos
  EXE         :=
  HOST_TUNE   := -mcpu=apple-m4
  HOST_LDLIBS :=
else
  HOST_OS     := linux
  EXE         :=
  HOST_TUNE   :=
  # glibc keeps POSIX and BSD declarations -- clock_gettime, MAP_ANONYMOUS --
  # from a strict -std=c11 build unless it is asked for them.
  HOST_DEFS   := -D_GNU_SOURCE
  # -flto=thin objects are LLVM bitcode, which GNU ld, the usual default
  # linker, cannot read.
  HOST_LDLIBS := -lpthread -fuse-ld=lld
endif

# make predefines CC as cc, which `?=` leaves alone -- on Linux that is gcc,
# which knows neither -flto=thin nor the cross targets -- so replace only
# that built-in default, and keep a CC from the command line or environment.
ifeq ($(origin CC),default)
  CC := clang
endif
CFLAGS  ?= -std=c11 -O3 $(HOST_TUNE) $(HOST_DEFS) -flto=thin -fvisibility=hidden -fvisibility-inlines-hidden -fno-exceptions -fno-rtti -g -Wall -Wextra -Wno-unused-parameter
BUILD   ?= build

XCC     ?= clang

# Appended to every link of a host program, for flags a build wants without
# replacing HOST_LDLIBS -- CI's ThinLTO cache directory, for one.
EXTRA_LDFLAGS ?=

RT_TUNE ?=

XFLAGS  ?= --target=aarch64-linux-gnu -O2 $(RT_TUNE) -ffreestanding -fno-stack-protector \
           -fno-builtin -nostdlib -mno-outline-atomics \
           -ffunction-sections -fdata-sections \
           -Wall -Wextra -Wno-unused-parameter
LD      ?= ld.lld

WARMC_SRC := src/main.c src/util.c src/a64.c src/x64.c src/parse.c src/opcodes.c \
             src/validate.c src/component.c src/wit.c src/wat.c \
             src/module_data.c src/elf.c src/macho.c src/coff.c src/pe.c src/ir.c src/ir_build.c \
             src/opt.c src/isel.c src/isel_x64.c src/codegen.c src/inline.c src/compile.c src/link.c src/canon.c src/witgen.c \
             src/clink.c
RT_OBJS   := $(BUILD)/rt/rt.o $(BUILD)/rt/start.o $(BUILD)/rt/hook.o \
             $(BUILD)/rt/gcthook.o \
             $(BUILD)/rt/modules.o $(BUILD)/rt/component.o

WARMC_HDRS := $(wildcard src/*.h src/*.inc) rt/const_ops.inc
RT_HDRS    := $(wildcard rt/*.h rt/*.inc)

# One object per source, so an edit recompiles that file alone.  Header
# dependencies come from the .d files clang writes with -MMD -MP, included
# at the foot of this file -- WARMC_HDRS is no longer a prerequisite of
# every object, which is what made one header touch rebuild all of src.
# The compile of the 22 sources is parallel work: build with `make -j`.
WARMC_OBJS := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(WARMC_SRC))
ASAN_OBJS  := $(patsubst src/%.c,$(BUILD)/obj-asan/%.o,$(WARMC_SRC))
TSAN_OBJS  := $(patsubst src/%.c,$(BUILD)/obj-tsan/%.o,$(WARMC_SRC))
WARMC_DEPS := $(WARMC_OBJS:.o=.d) $(ASAN_OBJS:.o=.d) $(TSAN_OBJS:.o=.d)

DEPFLAGS   := -MMD -MP

MAC_OBJS  := $(BUILD)/rt/rt-macos.o $(BUILD)/rt/start-macos.o \
             $(BUILD)/rt/hook-macos.o $(BUILD)/rt/gcthook-macos.o \
             $(BUILD)/rt/modules-macos.o \
             $(BUILD)/rt/component-macos.o
MACFLAGS  ?= --target=arm64-apple-macos11 -DWARMC_DARWIN=1 -O2 $(RT_TUNE) -ffreestanding \
             -fno-stack-protector -fno-builtin -nostdlib -mno-outline-atomics \
             -Wall -Wextra -Wno-unused-parameter

# Windows x64 target runtime.
WIN_OBJS  := $(BUILD)/rt/rt-windows.o $(BUILD)/rt/start-windows.o \
             $(BUILD)/rt/hook-windows.o $(BUILD)/rt/gcthook-windows.o \
             $(BUILD)/rt/modules-windows.o \
             $(BUILD)/rt/component-windows.o
WINFLAGS  ?= --target=x86_64-pc-windows-gnu -DWARMC_WINDOWS=1 -O2 $(RT_TUNE) -ffreestanding \
             -fno-stack-protector -fno-builtin -nostdlib \
             -ffunction-sections -fdata-sections \
             -Wall -Wextra -Wno-unused-parameter

all: $(BUILD)/warmc$(EXE) $(BUILD)/warmrt.a $(BUILD)/warmrt-macos.a \
     $(BUILD)/warmrt-windows.a \
     $(BUILD)/warmgcth.a $(BUILD)/warmgcth-macos.a \
     $(BUILD)/warmgcth-windows.a \
     $(BUILD)/warmwasi.a $(BUILD)/warmwasi-macos.a \
     $(BUILD)/warmwasi-windows.a \
     $(BUILD)/warmwasip2.a $(BUILD)/warmwasip2-macos.a $(BUILD)/a64_test$(EXE)

$(BUILD)/obj/%.o: src/%.c
	@mkdir -p $(BUILD)/obj
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD)/warmc$(EXE): $(WARMC_OBJS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(WARMC_OBJS) $(HOST_LDLIBS) $(EXTRA_LDFLAGS)

# One pattern per runtime flavor; make picks the pattern with the shortest
# stem, so the suffixed ones win for suffixed objects.  start*.o (assembly)
# and wasi-stub*.o (built by warmc itself) keep explicit rules below.
$(BUILD)/rt/%.o: rt/%.c $(RT_HDRS)
	@mkdir -p $(BUILD)/rt
	$(XCC) $(XFLAGS) -c -o $@ $<

$(BUILD)/rt/%-macos.o: rt/%.c $(RT_HDRS)
	@mkdir -p $(BUILD)/rt
	$(XCC) $(MACFLAGS) -c -o $@ $<

$(BUILD)/rt/%-windows.o: rt/%.c $(RT_HDRS)
	@mkdir -p $(BUILD)/rt
	$(XCC) $(WINFLAGS) -c -o $@ $<

$(BUILD)/rt/start.o: rt/start.S
	@mkdir -p $(BUILD)/rt
	$(XCC) --target=aarch64-linux-gnu -c -o $@ rt/start.S

$(BUILD)/rt/start-macos.o: rt/start.S
	@mkdir -p $(BUILD)/rt
	$(XCC) --target=arm64-apple-macos11 -DWARMC_DARWIN=1 -c -o $@ rt/start.S

$(BUILD)/rt/start-windows.o: rt/start_x64.S
	@mkdir -p $(BUILD)/rt
	$(XCC) --target=x86_64-pc-windows-gnu -DWARMC_WINDOWS=1 -c -o $@ rt/start_x64.S

$(BUILD)/warmrt.a: $(RT_OBJS)
	@rm -f $@
	llvm-ar rcs $@ $(RT_OBJS) 2>/dev/null || ar rcs $@ $(RT_OBJS)

$(BUILD)/warmrt-windows.a: $(WIN_OBJS)
	@mkdir -p $(BUILD)
	llvm-ar rcs $@ $(WIN_OBJS)

$(BUILD)/warmrt-macos.a: $(MAC_OBJS)
	@rm -f $@
	llvm-ar rcs $@ $(MAC_OBJS)

$(BUILD)/warmgcth.a: $(BUILD)/rt/gcthtest.o $(RT_OBJS)
	@rm -f $@
	llvm-ar rcs $@ $(BUILD)/rt/gcthtest.o $(RT_OBJS) 2>/dev/null || \
	  ar rcs $@ $(BUILD)/rt/gcthtest.o $(RT_OBJS)

$(BUILD)/warmgcth-macos.a: $(BUILD)/rt/gcthtest-macos.o $(MAC_OBJS)
	@rm -f $@
	llvm-ar rcs $@ $(BUILD)/rt/gcthtest-macos.o $(MAC_OBJS)

$(BUILD)/warmgcth-windows.a: $(BUILD)/rt/gcthtest-windows.o $(WIN_OBJS)
	@rm -f $@
	llvm-ar rcs $@ $(BUILD)/rt/gcthtest-windows.o $(WIN_OBJS)

$(BUILD)/rt/wasi-stub.o: rt/wasi.wat $(BUILD)/warmc$(EXE)
	@mkdir -p $(BUILD)/rt
	$(BUILD)/warmc$(EXE) -O2 -c --symbol-prefix=warmc_wasi_stub -o $@ rt/wasi.wat

$(BUILD)/rt/wasi-stub-macos.o: rt/wasi.wat $(BUILD)/warmc$(EXE)
	@mkdir -p $(BUILD)/rt
	$(BUILD)/warmc$(EXE) -O2 -c --target=macos --symbol-prefix=warmc_wasi_stub \
	  -o $@ rt/wasi.wat

$(BUILD)/rt/wasi-stub-windows.o: rt/wasi.wat $(BUILD)/warmc$(EXE)
	@mkdir -p $(BUILD)/rt
	$(BUILD)/warmc$(EXE) -O2 -c --target=windows --symbol-prefix=warmc_wasi_stub \
	  -o $@ rt/wasi.wat

# A Linux program links no libc, so the Linux hosts carry rt/linuxposix.c:
# the POSIX surface rt/wasi.c and rt/wasifs.c call, over raw system calls.
WASI_OBJS := $(BUILD)/rt/wasi.o $(BUILD)/rt/wasifs.o $(BUILD)/rt/linuxposix.o \
             $(BUILD)/rt/wasi-stub.o
WASI_MOBJS := $(BUILD)/rt/wasi-macos.o $(BUILD)/rt/wasifs-macos.o \
              $(BUILD)/rt/wasi-stub-macos.o
# Windows has no libc behind the POSIX names rt/wasi.c and rt/wasifs.c call,
# so the archive carries rt/winposix.c, which is that whole surface over Win32.
WASI_WOBJS := $(BUILD)/rt/wasi-windows.o $(BUILD)/rt/wasifs-windows.o \
              $(BUILD)/rt/winposix-windows.o $(BUILD)/rt/wasi-stub-windows.o

$(BUILD)/warmwasi.a: $(WASI_OBJS)
	@rm -f $@
	llvm-ar rcs $@ $(WASI_OBJS) 2>/dev/null || ar rcs $@ $(WASI_OBJS)

$(BUILD)/warmwasi-macos.a: $(WASI_MOBJS)
	@rm -f $@
	llvm-ar rcs $@ $(WASI_MOBJS)

$(BUILD)/warmwasi-windows.a: $(WASI_WOBJS)
	@rm -f $@
	llvm-ar rcs $@ $(WASI_WOBJS)

P2_OBJS := $(BUILD)/rt/wasip2.o $(BUILD)/rt/wasifs.o $(BUILD)/rt/linuxposix.o
P2_MOBJS := $(BUILD)/rt/wasip2-macos.o $(BUILD)/rt/wasifs-macos.o

$(BUILD)/warmwasip2.a: $(P2_OBJS)
	@rm -f $@
	llvm-ar rcs $@ $(P2_OBJS) 2>/dev/null || ar rcs $@ $(P2_OBJS)

$(BUILD)/warmwasip2-macos.a: $(P2_MOBJS)
	@rm -f $@
	llvm-ar rcs $@ $(P2_MOBJS)

ASAN_CC ?= $(shell test -x /opt/homebrew/opt/llvm/bin/clang && \
                   echo /opt/homebrew/opt/llvm/bin/clang || echo $(CC))

ASAN_FLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer \
              -fno-sanitize-recover=all
TSAN_FLAGS ?= -fsanitize=thread -fno-omit-frame-pointer

$(BUILD)/obj-asan/%.o: src/%.c
	@mkdir -p $(BUILD)/obj-asan
	$(ASAN_CC) $(CFLAGS) $(ASAN_FLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD)/warmc-asan$(EXE): $(ASAN_OBJS)
	@mkdir -p $(BUILD)
	$(ASAN_CC) $(CFLAGS) $(ASAN_FLAGS) -o $@ $(ASAN_OBJS) $(HOST_LDLIBS) $(EXTRA_LDFLAGS)

asan: $(BUILD)/warmc-asan$(EXE)

$(BUILD)/obj-tsan/%.o: src/%.c
	@mkdir -p $(BUILD)/obj-tsan
	$(ASAN_CC) $(CFLAGS) $(TSAN_FLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD)/warmc-tsan$(EXE): $(TSAN_OBJS)
	@mkdir -p $(BUILD)
	$(ASAN_CC) $(CFLAGS) $(TSAN_FLAGS) -o $@ $(TSAN_OBJS) $(HOST_LDLIBS) $(EXTRA_LDFLAGS)

tsan: $(BUILD)/warmc-tsan$(EXE)

$(BUILD)/a64_test$(EXE): test/a64_test.c $(WARMC_HDRS) \
                   $(BUILD)/obj/a64.o $(BUILD)/obj/util.o
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ test/a64_test.c $(BUILD)/obj/a64.o $(BUILD)/obj/util.o $(HOST_LDLIBS) $(EXTRA_LDFLAGS)

$(BUILD)/x64_test$(EXE): test/x64_test.c $(WARMC_HDRS) \
                   $(BUILD)/obj/x64.o $(BUILD)/obj/util.o
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ test/x64_test.c $(BUILD)/obj/x64.o $(BUILD)/obj/util.o $(HOST_LDLIBS) $(EXTRA_LDFLAGS)

check-encoder: $(BUILD)/a64_test$(EXE) $(BUILD)/x64_test$(EXE)
	@./test/run_a64_test.sh $(BUILD)
	@./test/run_x64_test.sh $(BUILD)

# The testsuite checkout, wasm-tools and the Kotlin stdlib are cached under
# $(BUILD), and a cached copy is only good while it is the pinned version.
# Each rule leaves a stamp naming the version it fetched, so bumping a pin
# (or upgrading kotlinc) fetches again instead of testing against whatever
# an older pin left behind.
TESTSUITE_COMMIT := 9b9fbf8260507169066bf3e8de7b01fb8c145e55
TESTSUITE_STAMP  := $(BUILD)/testsuite/.commit-$(TESTSUITE_COMMIT)

$(TESTSUITE_STAMP):
	@mkdir -p $(BUILD)
	@test -d $(BUILD)/testsuite/.git || \
	  git clone -q https://github.com/WebAssembly/testsuite.git $(BUILD)/testsuite
	@cd $(BUILD)/testsuite && { git checkout -q $(TESTSUITE_COMMIT) 2>/dev/null || \
	  { git fetch -q origin && git checkout -q $(TESTSUITE_COMMIT); }; }
	@rm -f $(BUILD)/testsuite/.commit-*
	@touch $@

testsuite: $(TESTSUITE_STAMP)

# test/spec/run.py reads this line to know which version to insist on.
WASM_TOOLS_VERSION := 1.262.0
WASM_TOOLS_STAMP   := $(BUILD)/tools/.wasm-tools-$(WASM_TOOLS_VERSION)

$(WASM_TOOLS_STAMP):
	@command -v cargo >/dev/null 2>&1 || { \
	  echo "make wasm-tools: cargo not found; it is what builds the .wast converter"; \
	  exit 1; }
	cargo install --root $(BUILD)/tools --locked --force \
	  --version $(WASM_TOOLS_VERSION) wasm-tools
	@rm -f $(BUILD)/tools/.wasm-tools-*
	@touch $@

wasm-tools: $(WASM_TOOLS_STAMP)

KOTLIN_VERSION := $(shell kotlinc -version 2>&1 | sed -n 's/.*kotlinc-jvm \([0-9.]*\).*/\1/p')
KOTLIN_WASI_KLIB := $(BUILD)/tools/kotlin-stdlib-wasm-wasi.klib
# test/gobench.py looks for this stamp to tell a stale klib from a missing one.
KOTLIN_STAMP     := $(BUILD)/tools/.kotlin-stdlib-$(KOTLIN_VERSION)

$(KOTLIN_STAMP):
	@command -v kotlinc-wasm >/dev/null 2>&1 || { \
	  echo "make kotlin-stdlib: kotlinc-wasm not found (brew install kotlin)"; \
	  exit 1; }
	@test -n "$(KOTLIN_VERSION)" || { \
	  echo "make kotlin-stdlib: cannot read the compiler version"; exit 1; }
	@mkdir -p $(BUILD)/tools
	curl -sSLf -o $(KOTLIN_WASI_KLIB) \
	  "https://repo1.maven.org/maven2/org/jetbrains/kotlin/kotlin-stdlib-wasm-wasi/$(KOTLIN_VERSION)/kotlin-stdlib-wasm-wasi-$(KOTLIN_VERSION).klib"
	@rm -f $(BUILD)/tools/.kotlin-stdlib-*
	@touch $@

kotlin-stdlib: $(KOTLIN_STAMP)

# clang, wasm-ld and a WASI sysroot for test/cbench and test/cppbench.  The
# release, not a distribution's packages: its C++ library comes built both with
# and without exceptions, and the kernels that throw need the former.
WASI_SDK_VERSION := 34
WASI_SDK_HOST    := $(strip \
  $(if $(filter Darwin-arm64,$(UNAME_S)-$(UNAME_M)),arm64-macos) \
  $(if $(filter Darwin-x86_64,$(UNAME_S)-$(UNAME_M)),x86_64-macos) \
  $(if $(filter Linux-aarch64 Linux-arm64,$(UNAME_S)-$(UNAME_M)),arm64-linux) \
  $(if $(filter Linux-x86_64,$(UNAME_S)-$(UNAME_M)),x86_64-linux))
# Versioned, so raising WASI_SDK_VERSION replaces the old release.
WASI_SDK_STAMP   := $(BUILD)/tools/.wasi-sdk-$(WASI_SDK_VERSION)

$(WASI_SDK_STAMP):
	@test -n "$(WASI_SDK_HOST)" || { \
	  echo "make wasi-sdk: wasi-sdk publishes no release for this host"; exit 1; }
	@mkdir -p $(BUILD)/tools
	@rm -rf $(BUILD)/tools/wasi-sdk
	curl -sSLf -o $(BUILD)/tools/wasi-sdk.tar.gz \
	  "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-$(WASI_SDK_VERSION)/wasi-sdk-$(WASI_SDK_VERSION).0-$(WASI_SDK_HOST).tar.gz"
	@mkdir -p $(BUILD)/tools/wasi-sdk
	@tar xzf $(BUILD)/tools/wasi-sdk.tar.gz -C $(BUILD)/tools/wasi-sdk --strip-components=1
	@rm -f $(BUILD)/tools/wasi-sdk.tar.gz $(BUILD)/tools/.wasi-sdk-*
	@touch $@

wasi-sdk: $(WASI_SDK_STAMP)

# `make check` brings the pinned tools up to date first; ktbench skips itself
# without a Kotlin compiler, so the stdlib is only fetched when there is one,
# and wasi-sdk only where it publishes a release.  hsbench's GHC is not
# fetched: ghc-wasm-meta installs it, and the corpus skips itself without.
CHECK_TOOLS := wasm-tools $(if $(KOTLIN_VERSION),kotlin-stdlib) \
               $(if $(WASI_SDK_HOST),wasi-sdk)

SPEC_LEVELS ?= -O1,-O2,-O2 --profile=deterministic

check-spec: $(BUILD)/warmc$(EXE) $(BUILD)/warmrt.a $(BUILD)/warmrt-macos.a \
            $(TESTSUITE_STAMP) $(WASM_TOOLS_STAMP)
	@python3 test/spec/run.py "--levels=$(SPEC_LEVELS)"

conformance: $(BUILD)/warmc$(EXE) $(BUILD)/warmrt.a $(BUILD)/warmrt-macos.a \
             $(TESTSUITE_STAMP) $(WASM_TOOLS_STAMP)
	@{ \
	  echo '# Conformance'; \
	  echo; \
	  echo 'Generated by `make conformance`. Do not edit.'; \
	  echo; \
	  echo 'The upstream WebAssembly testsuite, pinned, grouped by proposal'; \
	  echo '(see `test/spec/proposals.py` for how a file is assigned to one).'; \
	  echo; \
	  python3 test/spec/run.py --by-proposal; \
	} > CONFORMANCE.md
	@echo "wrote CONFORMANCE.md"

# TSan does not exist on Windows; ASan on mingw is tried separately.  The
# fixture is the other way round: it needs the real windows.h, so it is the
# one check that only a Windows host can run.
ifeq ($(HOST_OS),windows)
SAN_TARGETS :=
WIN_CHECKS  := check-win-fixture
else
SAN_TARGETS := asan tsan
WIN_CHECKS  :=
endif

# Compiling it is the test: it asserts every Win32 structure offset that
# rt/win.inc and rt/winposix.c hard-code against the SDK headers.
check-win-fixture:
	@mkdir -p $(BUILD)
	@$(CC) -o $(BUILD)/win_fixture$(EXE) test/win_fixture.c
	@echo "win-fixture: OK (the Win32 offsets the runtime hard-codes match windows.h)"

check: all $(SAN_TARGETS) $(WIN_CHECKS) check-encoder $(CHECK_TOOLS)
	@./test/run_all.sh

# A few rows of `make check` that reach most of warmc in a minute of a small
# machine (SMOKE_ROWS in test/run_all.sh), without the sanitizer builds or
# the language corpora: what CI runs on every push.
smoke: all $(WIN_CHECKS) check-encoder wasm-tools
	@CHECK_SMOKE=1 ./test/run_all.sh

# The pinned tools `make check` fetches, alone: CI fetches them before the
# tests so that its cache of them is saved whatever the tests find.
check-tools: $(CHECK_TOOLS)

check-linux: all
	@rc=0; ./test/run_gowasi_linux.sh || rc=1; ./test/run_gc_linux.sh || rc=1; exit $$rc

BENCH_CORPORA = gobench stdgo vecbench asbench rsbench ktbench zigbench cbench cppbench \
                hsbench

bench-compare: all
	@rc=0; t=$$(mktemp); for c in $(BENCH_CORPORA); do \
	  python3 test/gobench.py --corpus $$c --rounds 5 --compare > $$t || rc=1; \
	  tail -n +2 $$t; \
	done; rm -f $$t; exit $$rc

bench-record: all
	@for c in $(BENCH_CORPORA); do \
	  python3 test/gobench.py --corpus $$c --rounds 5 --record | tail -1; \
	done

FUZZ_SEED  ?= 1
FUZZ_COUNT ?= 300

fuzz: $(BUILD)/warmc$(EXE) $(BUILD)/warmrt.a $(BUILD)/warmrt-macos.a
	@python3 test/fuzz.py --seed $(FUZZ_SEED) --count $(FUZZ_COUNT)

JITTER_SEEDS ?= 64
JITTER_RATE  ?= 8

GC_JITTER_JOBS ?= 6

gc-jitter: all wasm-tools
	@JITTER_SEEDS=$(JITTER_SEEDS) JITTER_RATE=$(JITTER_RATE) \
	  GC_JITTER_JOBS=$(GC_JITTER_JOBS) ./test/run_gc_jitter.sh

gc-jitter-full: all wasm-tools
	@JITTER_FULL=1 JITTER_SEEDS=$(JITTER_SEEDS) JITTER_RATE=$(JITTER_RATE) \
	  GC_JITTER_JOBS=$(GC_JITTER_JOBS) ./test/run_gc_jitter.sh

clean:
	rm -rf $(BUILD)

.PHONY: all asan tsan check smoke check-tools check-linux check-encoder check-spec conformance testsuite wasm-tools \
        kotlin-stdlib wasi-sdk fuzz clean bench-compare bench-record gc-jitter gc-jitter-full

-include $(WARMC_DEPS)
