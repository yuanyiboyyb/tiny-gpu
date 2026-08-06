# TinyGPU Virtualization

This is the only README under `virtualization/`. It describes a complete path
from a fresh workspace to a TinyGPU test running inside QEMU.

Every command below assumes the current directory is the workspace root: the
directory that contains `tiny-gpu/`. Verify that first:

```sh
test -d tiny-gpu/virtualization
```

## 1. Workspace layout

```text
<workspace>/
├── tiny-gpu/
│   ├── rtl/             TinyGPU RTL source
│   ├── sim_rtl/         RTL simulation and sample kernel
│   └── virtualization/  Compiler, runtime, driver, QEMU integration and scripts
├── qemu-x86/            External QEMU source and build tree
└── tinygpu-linux/       Downloaded guest kernel, headers, rootfs and initramfs
```

`tiny-gpu/virtualization/build/` contains all TinyGPU-owned build results.
QEMU itself is the exception: it is built inside `qemu-x86/build/` because it
belongs to the external QEMU source tree.

The important directories under `virtualization/` are:

```text
common/    Shared PCI IDs, registers, BAR layout and ioctl definitions
compiler/  TinyGPU language frontend, code generator and assembler
device/    Verilator-backed device model linked into QEMU
driver/    Linux guest PCI driver
qemu/      QEMU device sources and environment/boot helper scripts
runtime/   Linux guest userspace runtime and examples
test/      Host-side device-model tests
build/     TinyGPU virtualization build output
```

## 2. Install host dependencies

The documented guest packages are Ubuntu 24.04 amd64 packages. On an Ubuntu
24.04 amd64 host, install the build tools with:

```sh
sudo apt update
sudo apt install -y \
    build-essential cmake ninja-build verilator git patch \
    python3 python3-venv pkg-config \
    libglib2.0-dev libpixman-1-dev zlib1g-dev \
    flex bison
```

`cocotb` is only required when running the optional RTL simulation; it is not
required to build or boot the QEMU guest.

## 3. Prepare the QEMU source tree

Clone QEMU beside `tiny-gpu/` and select the revision used by this integration:

```sh
git clone https://github.com/qemu/qemu.git qemu-x86
git -C qemu-x86 checkout b428fe036233cbd15d37e3c027ab6ca4d3661a80
```

Do not build QEMU yet. The TinyGPU Verilator library must be built first in
step 5, and the TinyGPU QEMU sources are installed in step 6.

## 4. Prepare `tinygpu-linux/`

Run the preparation script from the workspace root:

```sh
./tiny-gpu/virtualization/qemu/prepare-linux.sh
```

The script creates `tinygpu-linux/`, downloads these packages with
`apt download`, and extracts them with `dpkg-deb -x`:

```text
busybox-static
linux-image-6.8.0-136-generic
linux-headers-6.8.0-136-generic
linux-headers-6.8.0-136
```

It produces:

```text
tinygpu-linux/
├── busybox-package/usr/bin/busybox
├── headers/usr/src/linux-headers-6.8.0-136/
├── headers/usr/src/linux-headers-6.8.0-136-generic/
├── kernel/vmlinuz-6.8.0-136-generic
├── packages/
└── rootfs/
```

These files have different jobs:

- `kernel/vmlinuz-6.8.0-136-generic` is passed to QEMU with `-kernel` and is
  the kernel that runs inside the guest.
- `headers/usr/src/linux-headers-6.8.0-136-generic/` is used by
  `make driver-build` to compile `tinygpu.ko` for exactly that guest kernel.
- `busybox-package/usr/bin/busybox` provides the static shell and basic tools
  used by the minimal root filesystem.

The headers are build-time inputs only. They are not copied into the initramfs.

## 5. Build TinyGPU artifacts

Build the compiler, runtime, Verilator model and host-side tests:

```sh
cd tiny-gpu/virtualization
make build
make test
make driver-build
cd ../..
```

`make driver-build` automatically uses:

```text
tinygpu-linux/headers/usr/src/linux-headers-6.8.0-136-generic
```

and writes the guest module to:

```text
tiny-gpu/virtualization/build/driver/tinygpu.ko
```

Generate the sample kernel consumed by the guest test:

```sh
cd tiny-gpu/sim_rtl
make matadd_kernel
cd ../..
```

This creates `tiny-gpu/sim_rtl/matadd.s` and
`tiny-gpu/sim_rtl/matadd.bin` from `matadd.tu`.

Optional RTL simulation:

```sh
python3 -m venv tiny-gpu/sim_rtl/.venv
tiny-gpu/sim_rtl/.venv/bin/pip install cocotb
cd tiny-gpu/sim_rtl
PATH="$PWD/.venv/bin:$PATH" make matadd
cd ../..
```

## 6. Install TinyGPU into QEMU and build QEMU

The source of record for the TinyGPU QEMU device remains under
`tiny-gpu/virtualization/qemu/`. Install that source and its build-system patch
into the external QEMU tree:

```sh
./tiny-gpu/virtualization/qemu/setup-qemu.sh
```

Configure and build QEMU inside its own source tree:

```sh
cd qemu-x86
mkdir -p build
cd build
../configure --target-list=x86_64-softmmu --disable-docs
ninja
cd ../..
```

The required result is:

```text
qemu-x86/build/qemu-system-x86_64
```

The setup script is idempotent: running it again refreshes the TinyGPU source
files and skips the build-system patch when `CONFIG_TINYGPU` is already present.

## 7. Build the root filesystem and initramfs

Run:

```sh
./tiny-gpu/virtualization/qemu/build-rootfs.sh
```

The script performs all rootfs setup; no manual copying is needed. It:

1. creates the minimal directory tree under `tinygpu-linux/rootfs/`;
2. installs static BusyBox and all command links used by `/init`;
3. installs the version-controlled `qemu/rootfs-overlay/`, including `/init`
   and `/etc` configuration;
4. builds static guest compiler/runtime executables under
   `virtualization/build/guest/`;
5. copies `tinygpu.ko`, tools and the sample kernel into the rootfs;
6. verifies the guest compiler output;
7. packs `tinygpu-linux/initramfs.cpio.gz`.

The key guest files after packing are:

```text
/init
/lib/modules/6.8.0-136-generic/extra/tinygpu.ko
/opt/tinygpu/bin/tinygpu-cc
/opt/tinygpu/bin/tinygpu-as
/opt/tinygpu/bin/tinygpu-frontend
/opt/tinygpu/bin/tinygpu-runtime-matadd
/opt/tinygpu/kernels/matadd.tu
/opt/tinygpu/kernels/matadd.s
/opt/tinygpu/kernels/matadd.bin
```

## 8. Start QEMU

From the workspace root, run:

```sh
./tiny-gpu/virtualization/qemu/run-built-in-qemu.sh
```

This launches the built-in TinyGPU PCI device. It does not use a socket or a
separate device process. The launcher uses:

```text
qemu-x86/build/qemu-system-x86_64
tinygpu-linux/kernel/vmlinuz-6.8.0-136-generic
tinygpu-linux/initramfs.cpio.gz
```

When KVM is unavailable, the script automatically uses QEMU TCG emulation.
Exit QEMU with `Ctrl-a x`.

## 9. Test inside the guest

The guest `/init` mounts the pseudo-filesystems, runs `mdev`, loads
`tinygpu.ko`, and opens a root shell. Confirm that the device and driver are
present:

```sh
dmesg
ls /sys/bus/pci/devices
ls -l /dev/tinygpu*
```

Run the end-to-end matrix-add test:

```sh
tinygpu-runtime-matadd /opt/tinygpu/kernels/matadd.bin /dev/tinygpu0
```

The complete copy-and-run sequence after host dependencies are installed is:

```sh
git clone https://github.com/qemu/qemu.git qemu-x86
git -C qemu-x86 checkout b428fe036233cbd15d37e3c027ab6ca4d3661a80

./tiny-gpu/virtualization/qemu/prepare-linux.sh

cd tiny-gpu/virtualization
make build
make test
make driver-build
cd ../sim_rtl
make matadd_kernel
cd ../..

./tiny-gpu/virtualization/qemu/setup-qemu.sh
cd qemu-x86
mkdir -p build
cd build
../configure --target-list=x86_64-softmmu --disable-docs
ninja
cd ../..

./tiny-gpu/virtualization/qemu/build-rootfs.sh
./tiny-gpu/virtualization/qemu/run-built-in-qemu.sh
```
