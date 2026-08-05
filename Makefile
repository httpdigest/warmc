CC      ?= clang
CFLAGS  ?= -std=c11 -O3 -mcpu=apple-m4 -flto=thin -fvisibility=hidden -fvisibility-inlines-hidden -fno-exceptions -fno-rtti -g -Wall -Wextra -Wno-unused-parameter
BUILD   ?= build

XCC     ?= clang

RT_TUNE ?=

XFLAGS  ?= --target=aarch64-linux-gnu -O2 $(RT_TUNE) -ffreestanding -fno-stack-protector \
           -fno-builtin -nostdlib -mno-outline-atomics \
           -ffunction-sections -fdata-sections \
           -Wall -Wextra -Wno-unused-parameter
LD      ?= ld.lld

WARMC_SRC := src/main.c src/util.c src/a64.c src/parse.c src/opcodes.c \
             src/validate.c src/component.c src/wit.c src/wat.c \
             src/module_data.c src/elf.c src/macho.c src/ir.c src/ir_build.c \
             src/opt.c src/isel.c src/inline.c src/compile.c src/link.c src/canon.c src/witgen.c \
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

all: $(BUILD)/warmc $(BUILD)/warmrt.a $(BUILD)/warmrt-macos.a \
     $(BUILD)/warmgcth.a $(BUILD)/warmgcth-macos.a \
     $(BUILD)/warmwasi.a $(BUILD)/warmwasi-macos.a \
     $(BUILD)/warmwasip2.a $(BUILD)/warmwasip2-macos.a $(BUILD)/a64_test

$(BUILD)/obj/%.o: src/%.c
	@mkdir -p $(BUILD)/obj
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD)/warmc: $(WARMC_OBJS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(WARMC_OBJS)

$(BUILD)/rt/rt.o: rt/rt.c $(RT_HDRS)
	@mkdir -p $(BUILD)/rt
	$(XCC) $(XFLAGS) -c -o $@ rt/rt.c

$(BUILD)/rt/gcthook.o: rt/gcthook.c
	@mkdir -p $(BUILD)/rt
	$(XCC) $(XFLAGS) -c -o $@ rt/gcthook.c

$(BUILD)/rt/gcthook-macos.o: rt/gcthook.c
	@mkdir -p $(BUILD)/rt
	$(XCC) $(MACFLAGS) -c -o $@ rt/gcthook.c

$(BUILD)/rt/gcthtest.o: rt/gcthtest.c
	@mkdir -p $(BUILD)/rt
	$(XCC) $(XFLAGS) -c -o $@ rt/gcthtest.c

$(BUILD)/rt/gcthtest-macos.o: rt/gcthtest.c
	@mkdir -p $(BUILD)/rt
	$(XCC) $(MACFLAGS) -c -o $@ rt/gcthtest.c

$(BUILD)/rt/hook.o: rt/hook.c
	@mkdir -p $(BUILD)/rt
	$(XCC) $(XFLAGS) -c -o $@ rt/hook.c

$(BUILD)/rt/hook-macos.o: rt/hook.c
	@mkdir -p $(BUILD)/rt
	$(XCC) $(MACFLAGS) -c -o $@ rt/hook.c

$(BUILD)/rt/modules.o: rt/modules.c $(RT_HDRS)
	@mkdir -p $(BUILD)/rt
	$(XCC) $(XFLAGS) -c -o $@ rt/modules.c

$(BUILD)/rt/modules-macos.o: rt/modules.c $(RT_HDRS)
	@mkdir -p $(BUILD)/rt
	$(XCC) $(MACFLAGS) -c -o $@ rt/modules.c

$(BUILD)/rt/component.o: rt/component.c $(RT_HDRS)
	@mkdir -p $(BUILD)/rt
	$(XCC) $(XFLAGS) -c -o $@ rt/component.c

$(BUILD)/rt/component-macos.o: rt/component.c $(RT_HDRS)
	@mkdir -p $(BUILD)/rt
	$(XCC) $(MACFLAGS) -c -o $@ rt/component.c

$(BUILD)/rt/start.o: rt/start.S
	@mkdir -p $(BUILD)/rt
	$(XCC) --target=aarch64-linux-gnu -c -o $@ rt/start.S

$(BUILD)/warmrt.a: $(RT_OBJS)
	@rm -f $@
	llvm-ar rcs $@ $(RT_OBJS) 2>/dev/null || ar rcs $@ $(RT_OBJS)

$(BUILD)/rt/rt-macos.o: rt/rt.c $(RT_HDRS)
	@mkdir -p $(BUILD)/rt
	$(XCC) $(MACFLAGS) -c -o $@ rt/rt.c

$(BUILD)/rt/start-macos.o: rt/start.S
	@mkdir -p $(BUILD)/rt
	$(XCC) --target=arm64-apple-macos11 -DWARMC_DARWIN=1 -c -o $@ rt/start.S

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

$(BUILD)/rt/wasi-stub.o: rt/wasi.wat $(BUILD)/warmc
	@mkdir -p $(BUILD)/rt
	$(BUILD)/warmc -O2 -c --symbol-prefix=warmc_wasi_stub -o $@ rt/wasi.wat

$(BUILD)/rt/wasi-stub-macos.o: rt/wasi.wat $(BUILD)/warmc
	@mkdir -p $(BUILD)/rt
	$(BUILD)/warmc -O2 -c --target=macos --symbol-prefix=warmc_wasi_stub \
	  -o $@ rt/wasi.wat

$(BUILD)/rt/wasi.o: rt/wasi.c $(RT_HDRS)
	@mkdir -p $(BUILD)/rt
	$(XCC) $(XFLAGS) -c -o $@ rt/wasi.c

$(BUILD)/rt/wasi-macos.o: rt/wasi.c $(RT_HDRS)
	@mkdir -p $(BUILD)/rt
	$(XCC) $(MACFLAGS) -c -o $@ rt/wasi.c

$(BUILD)/rt/wasifs.o: rt/wasifs.c $(RT_HDRS)
	@mkdir -p $(BUILD)/rt
	$(XCC) $(XFLAGS) -c -o $@ rt/wasifs.c

$(BUILD)/rt/wasifs-macos.o: rt/wasifs.c $(RT_HDRS)
	@mkdir -p $(BUILD)/rt
	$(XCC) $(MACFLAGS) -c -o $@ rt/wasifs.c

WASI_OBJS := $(BUILD)/rt/wasi.o $(BUILD)/rt/wasifs.o $(BUILD)/rt/wasi-stub.o
WASI_MOBJS := $(BUILD)/rt/wasi-macos.o $(BUILD)/rt/wasifs-macos.o \
              $(BUILD)/rt/wasi-stub-macos.o

$(BUILD)/warmwasi.a: $(WASI_OBJS)
	@rm -f $@
	llvm-ar rcs $@ $(WASI_OBJS) 2>/dev/null || ar rcs $@ $(WASI_OBJS)

$(BUILD)/warmwasi-macos.a: $(WASI_MOBJS)
	@rm -f $@
	llvm-ar rcs $@ $(WASI_MOBJS)

$(BUILD)/rt/wasip2.o: rt/wasip2.c $(RT_HDRS)
	@mkdir -p $(BUILD)/rt
	$(XCC) $(XFLAGS) -c -o $@ rt/wasip2.c

$(BUILD)/rt/wasip2-macos.o: rt/wasip2.c $(RT_HDRS)
	@mkdir -p $(BUILD)/rt
	$(XCC) $(MACFLAGS) -c -o $@ rt/wasip2.c

P2_OBJS := $(BUILD)/rt/wasip2.o $(BUILD)/rt/wasifs.o
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

$(BUILD)/warmc-asan: $(ASAN_OBJS)
	@mkdir -p $(BUILD)
	$(ASAN_CC) $(CFLAGS) $(ASAN_FLAGS) -o $@ $(ASAN_OBJS)

asan: $(BUILD)/warmc-asan

$(BUILD)/obj-tsan/%.o: src/%.c
	@mkdir -p $(BUILD)/obj-tsan
	$(ASAN_CC) $(CFLAGS) $(TSAN_FLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD)/warmc-tsan: $(TSAN_OBJS)
	@mkdir -p $(BUILD)
	$(ASAN_CC) $(CFLAGS) $(TSAN_FLAGS) -o $@ $(TSAN_OBJS)

tsan: $(BUILD)/warmc-tsan

$(BUILD)/a64_test: test/a64_test.c $(WARMC_HDRS) \
                   $(BUILD)/obj/a64.o $(BUILD)/obj/util.o
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ test/a64_test.c $(BUILD)/obj/a64.o $(BUILD)/obj/util.o

check-encoder: $(BUILD)/a64_test
	@./test/run_a64_test.sh $(BUILD)

TESTSUITE_COMMIT := 65a43d2e9464b6967c98b23c8493765c4d124f4e

$(BUILD)/testsuite/README.md:
	@mkdir -p $(BUILD)
	git clone -q https://github.com/WebAssembly/testsuite.git $(BUILD)/testsuite
	@cd $(BUILD)/testsuite && git checkout -q $(TESTSUITE_COMMIT)

testsuite: $(BUILD)/testsuite/README.md

WASM_TOOLS_VERSION := 1.257.1

$(BUILD)/tools/bin/wasm-tools:
	@command -v cargo >/dev/null 2>&1 || { \
	  echo "make wasm-tools: cargo not found; it is what builds the .wast converter"; \
	  exit 1; }
	cargo install --root $(BUILD)/tools --locked \
	  --version $(WASM_TOOLS_VERSION) wasm-tools

wasm-tools: $(BUILD)/tools/bin/wasm-tools

KOTLIN_VERSION := $(shell kotlinc -version 2>&1 | sed -n 's/.*kotlinc-jvm \([0-9.]*\).*/\1/p')
KOTLIN_WASI_KLIB := $(BUILD)/tools/kotlin-stdlib-wasm-wasi.klib

$(KOTLIN_WASI_KLIB):
	@command -v kotlinc-wasm >/dev/null 2>&1 || { \
	  echo "make kotlin-stdlib: kotlinc-wasm not found (brew install kotlin)"; \
	  exit 1; }
	@test -n "$(KOTLIN_VERSION)" || { \
	  echo "make kotlin-stdlib: cannot read the compiler version"; exit 1; }
	@mkdir -p $(BUILD)/tools
	curl -sSLf -o $@ \
	  "https://repo1.maven.org/maven2/org/jetbrains/kotlin/kotlin-stdlib-wasm-wasi/$(KOTLIN_VERSION)/kotlin-stdlib-wasm-wasi-$(KOTLIN_VERSION).klib"

kotlin-stdlib: $(KOTLIN_WASI_KLIB)

check-spec: $(BUILD)/warmc $(BUILD)/warmrt.a $(BUILD)/warmrt-macos.a \
            $(BUILD)/testsuite/README.md $(BUILD)/tools/bin/wasm-tools
	@python3 test/spec/run.py "--levels=-O1,-O2,-O2 --profile=deterministic"

conformance: $(BUILD)/warmc $(BUILD)/warmrt.a $(BUILD)/warmrt-macos.a \
             $(BUILD)/testsuite/README.md $(BUILD)/tools/bin/wasm-tools
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

check: all asan tsan check-encoder
	@./test/run_all.sh

check-linux: all
	@rc=0; ./test/run_gowasi_linux.sh || rc=1; ./test/run_gc_linux.sh || rc=1; exit $$rc

BENCH_CORPORA = gobench vecbench asbench rsbench ktbench zigbench

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

fuzz: $(BUILD)/warmc $(BUILD)/warmrt.a $(BUILD)/warmrt-macos.a
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

.PHONY: all asan tsan check check-linux check-encoder check-spec conformance testsuite wasm-tools fuzz clean \
        bench-compare bench-record gc-jitter gc-jitter-full

-include $(WARMC_DEPS)
