#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
virtualization_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
project_dir=$(dirname -- "$virtualization_dir")
workspace_dir=$(dirname -- "$project_dir")
linux_dir=${TINYGPU_LINUX_DIR:-"$workspace_dir/tinygpu-linux"}

rootfs_dir=${ROOTFS_DIR:-"$linux_dir/rootfs"}
output_image=${1:-"$linux_dir/initramfs.cpio.gz"}
kernel_release=${KERNEL_RELEASE:-6.8.0-136-generic}
busybox=${BUSYBOX_BIN:-"$linux_dir/busybox-package/usr/bin/busybox"}
driver=${TINYGPU_DRIVER:-"$virtualization_dir/build/driver/tinygpu.ko"}
guest_build=${GUEST_BUILD_DIR:-"$virtualization_dir/build/guest"}
toolkit="$rootfs_dir/opt/tinygpu"
overlay_dir="$script_dir/rootfs-overlay"

require_file()
{
    if [ ! -f "$1" ]; then
        echo "missing required file: $1" >&2
        exit 1
    fi
}

require_file "$busybox"
require_file "$driver"
require_file "$project_dir/sim_rtl/matadd.tu"
require_file "$project_dir/sim_rtl/matadd.s"
require_file "$project_dir/sim_rtl/matadd.bin"

echo "[1/6] Preparing the minimal BusyBox rootfs"
mkdir -p \
    "$rootfs_dir/bin" "$rootfs_dir/sbin" "$rootfs_dir/etc" \
    "$rootfs_dir/proc" "$rootfs_dir/sys" "$rootfs_dir/dev" \
    "$rootfs_dir/tmp" "$rootfs_dir/run" "$rootfs_dir/root" \
    "$rootfs_dir/lib/modules/$kernel_release/extra"
install -m 0755 "$busybox" "$rootfs_dir/bin/busybox"
for applet in sh mount mkdir chmod cat echo dmesg ls uname hostname \
              insmod setsid cttyhack; do
    ln -sfn /bin/busybox "$rootfs_dir/bin/$applet"
done
rm -f "$rootfs_dir/bin/lspci"
ln -sfn /bin/busybox "$rootfs_dir/sbin/mdev"
cp -a "$overlay_dir/." "$rootfs_dir/"
chmod 0755 "$rootfs_dir/init"
chmod 1777 "$rootfs_dir/tmp"

echo "[2/6] Building static guest compiler and runtime in virtualization/build"
cmake -S "$virtualization_dir" -B "$guest_build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DTINYGPU_BUILD_DEVICE=OFF \
    -DCMAKE_EXE_LINKER_FLAGS=-static
cmake --build "$guest_build" --target \
    tinygpu-cc tinygpu-as tinygpu-frontend \
    tinygpu_runtime tinygpu-runtime-matadd -j "${JOBS:-2}"

echo "[3/6] Installing TinyGPU driver into rootfs"
install -D -m 0644 "$driver" \
    "$rootfs_dir/lib/modules/$kernel_release/extra/tinygpu.ko"

echo "[4/6] Synchronizing guest tools, kernels, headers and sources"
mkdir -p \
    "$toolkit/bin" \
    "$toolkit/lib" \
    "$toolkit/include/tinygpu/compiler" \
    "$toolkit/kernels" \
    "$toolkit/source/compiler" \
    "$toolkit/source/runtime"

install -m 0755 "$guest_build/compiler/tinygpu-cc" "$toolkit/bin/tinygpu-cc"
install -m 0755 "$guest_build/compiler/tinygpu-as" "$toolkit/bin/tinygpu-as"
install -m 0755 "$guest_build/compiler/tinygpu-frontend" \
    "$toolkit/bin/tinygpu-frontend"
install -m 0755 "$guest_build/runtime/tinygpu-runtime-matadd" \
    "$toolkit/bin/tinygpu-runtime-matadd"
strip "$toolkit/bin/tinygpu-cc" \
      "$toolkit/bin/tinygpu-as" \
      "$toolkit/bin/tinygpu-frontend" \
      "$toolkit/bin/tinygpu-runtime-matadd"

install -m 0644 "$guest_build/compiler/libtinygpu-compiler.a" \
    "$toolkit/lib/libtinygpu-compiler.a"
install -m 0644 "$guest_build/runtime/libtinygpu_runtime.a" \
    "$toolkit/lib/libtinygpu_runtime.a"
install -m 0644 "$virtualization_dir/runtime/include/tinygpu/runtime.hpp" \
    "$toolkit/include/tinygpu/runtime.hpp"
cp -a "$virtualization_dir/compiler/include/tinygpu/compiler/." \
    "$toolkit/include/tinygpu/compiler/"
cp -a "$project_dir/sim_rtl/matadd.tu" \
      "$project_dir/sim_rtl/matadd.s" \
      "$project_dir/sim_rtl/matadd.bin" \
      "$toolkit/kernels/"
cp -a "$virtualization_dir/compiler/." "$toolkit/source/compiler/"
cp -a "$virtualization_dir/runtime/." "$toolkit/source/runtime/"

echo "[5/6] Verifying guest compiler output"
verify_dir=$(mktemp -d)
trap 'rm -rf "$verify_dir"' EXIT HUP INT TERM
"$toolkit/bin/tinygpu-cc" "$toolkit/kernels/matadd.tu" \
    -o "$verify_dir/matadd.bin"
cmp "$verify_dir/matadd.bin" "$toolkit/kernels/matadd.bin"

echo "[6/6] Packing rootfs as newc+gzip initramfs"
output_dir=$(dirname -- "$output_image")
mkdir -p "$output_dir"
temporary_image="$output_image.tmp"
(
    cd "$rootfs_dir"
    find . -print0 | sort -z | \
        "$busybox" cpio -0 -o -H newc -R 0:0
) | "$busybox" gzip -c > "$temporary_image"
mv "$temporary_image" "$output_image"

echo "Created $output_image"
echo "Uncompressed rootfs: $(du -sh "$rootfs_dir" | cut -f1)"
echo "Compressed initramfs: $(du -h "$output_image" | cut -f1)"
