CMAKE ?= cmake
CTEST ?= ctest
CMAKE_BUILD_TYPE ?= Debug
BUILD_DIR ?= $(CURDIR)/build
DRIVER_DIR ?= $(CURDIR)/driver
LINUX_SRC_DIR ?= $(abspath $(CURDIR)/../../linux-src)
KDIR ?= $(LINUX_SRC_DIR)

.PHONY: all configure build test clean \
	model-configure model-build model-test model-clean \
	compiler-build compiler-test runtime-build runtime-test \
	driver-build driver-clean help

all: model-build

configure: model-configure

build: model-build

test: model-test

help:
	@printf '%s\n' \
	'Targets from virtualization/:' \
	'  make build          Configure and build host-side CMake targets in virtualization/build' \
	'  make test           Run host-side CTest suite from virtualization/build' \
	'  make compiler-build Build tinygpu-cc, tinygpu-as, and tinygpu-frontend into virtualization/build' \
	'  make compiler-test  Run compiler tests from virtualization/build' \
	'  make runtime-build  Build runtime library/examples into virtualization/build' \
	'  make runtime-test   Run runtime tests from virtualization/build' \
	'  make driver-build   Build the guest Linux driver against linux-src' \
	'  make clean          Clean CMake artifacts under virtualization/build' \
	'  make driver-clean   Clean guest driver artifacts under virtualization/build/driver'

model-configure:
	$(CMAKE) -S $(CURDIR) -B $(BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) \
		-DTINYGPU_BUILD_DEVICE=ON \
		-DBUILD_TESTING=ON

model-build: model-configure
	$(CMAKE) --build $(BUILD_DIR) -j

model-test: model-build
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

compiler-build: model-configure
	$(CMAKE) --build $(BUILD_DIR) --target tinygpu-cc tinygpu-as tinygpu-frontend -j

compiler-test: model-configure
	$(CMAKE) --build $(BUILD_DIR) --target tinygpu-frontend-tests tinygpu-codegen-tests tinygpu-assembler-tests -j
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure -R 'tinygpu-(frontend|codegen|assembler)-tests'

runtime-build: model-configure
	$(CMAKE) --build $(BUILD_DIR) --target tinygpu_runtime tinygpu-runtime-matadd -j

runtime-test: model-configure
	$(CMAKE) --build $(BUILD_DIR) --target tinygpu-runtime-tests -j
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure -R 'tinygpu-runtime-tests'

driver-build:
	@test -f "$(KDIR)/Makefile" || { \
		echo "missing guest kernel source tree: $(KDIR)" >&2; \
		echo "place linux-src beside tiny-gpu as described in virtualization/README.md" >&2; \
		exit 1; \
	}
	@test -f "$(KDIR)/.config" || { \
		echo "guest kernel is not configured: $(KDIR)/.config" >&2; \
		echo "configure and build linux-src before building the driver" >&2; \
		exit 1; \
	}
	@test -f "$(KDIR)/Module.symvers" || { \
		echo "missing kernel symbol table: $(KDIR)/Module.symvers" >&2; \
		echo "run: make -C $(KDIR) -j\"\$$(nproc)\"" >&2; \
		exit 1; \
	}
	$(MAKE) -C $(DRIVER_DIR) \
		BUILD_DIR=$(BUILD_DIR)/driver \
		KDIR=$(KDIR)

driver-clean:
	$(MAKE) -C $(DRIVER_DIR) \
		BUILD_DIR=$(BUILD_DIR)/driver \
		KDIR=$(KDIR) clean

model-clean:
	$(CMAKE) --build $(BUILD_DIR) --target clean

clean: model-clean
