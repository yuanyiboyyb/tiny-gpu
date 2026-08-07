# TinyGPU Virtualization Build and Run

All commands are run from the workspace root:

```sh
cd /home/yyb/tinygpu-workspace
```

## 1. Install dependencies

```sh
sudo apt update
sudo apt install -y \
    build-essential cmake ninja-build verilator git patch \
    python3 python3-venv pkg-config \
    libglib2.0-dev libpixman-1-dev zlib1g-dev \
    flex bison bc libssl-dev libelf-dev dwarves
```

## 2. Configure the Linux kernel

```sh
make -C linux-src x86_64_defconfig

linux-src/scripts/config --file linux-src/.config \
    --set-str LOCALVERSION "-tinygpu" \
    --enable BLK_DEV_INITRD \
    --enable RD_GZIP \
    --enable MODULES \
    --enable PCI \
    --enable DEVTMPFS \
    --enable DEVTMPFS_MOUNT \
    --enable SERIAL_8250 \
    --enable SERIAL_8250_CONSOLE

make -C linux-src olddefconfig
```

## 3. Build the Linux kernel

```sh
make -C linux-src -j"$(nproc)"
test -f linux-src/arch/x86/boot/bzImage
test -f linux-src/Module.symvers
make -s -C linux-src kernelrelease
```

## 4. Prepare the root filesystem

```sh
./tiny-gpu/virtualization/qemu/prepare-rootfs.sh
```

## 5. Build TinyGPU

```sh
make -C tiny-gpu/virtualization build
make -C tiny-gpu/virtualization test
make -C tiny-gpu/virtualization driver-build
make -C tiny-gpu/sim_rtl matadd_kernel
```

## 6. Prepare and build QEMU

```sh
git clone https://github.com/qemu/qemu.git qemu-x86
git -C qemu-x86 checkout b428fe036233cbd15d37e3c027ab6ca4d3661a80
./tiny-gpu/virtualization/qemu/build-qemu.sh
```

## 7. Build the initramfs

```sh
./tiny-gpu/virtualization/qemu/build-rootfs.sh
```

## 8. Start QEMU

```sh
./tiny-gpu/virtualization/qemu/run-qemu.sh
```

Exit QEMU:

```text
Ctrl-a x
```

## 9. Test inside the guest

```sh
uname -r
dmesg
ls -l /dev/tinygpu*
tinygpu-runtime-matadd /opt/tinygpu/kernels/matadd.bin /dev/tinygpu0
```

## Rebuild after changing Linux

```sh
make -C linux-src -j"$(nproc)"
make -C tiny-gpu/virtualization driver-build
./tiny-gpu/virtualization/qemu/build-rootfs.sh
./tiny-gpu/virtualization/qemu/run-qemu.sh
```

## Rebuild after changing TinyGPU

```sh
make -C tiny-gpu/virtualization build
make -C tiny-gpu/virtualization test
make -C tiny-gpu/virtualization driver-build
make -C tiny-gpu/sim_rtl matadd_kernel
./tiny-gpu/virtualization/qemu/build-qemu.sh
./tiny-gpu/virtualization/qemu/build-rootfs.sh
./tiny-gpu/virtualization/qemu/run-qemu.sh
```
