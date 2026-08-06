#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
virtualization_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
project_dir=$(dirname -- "$virtualization_dir")
workspace_dir=$(dirname -- "$project_dir")
linux_dir=${TINYGPU_LINUX_DIR:-"$workspace_dir/tinygpu-linux"}
packages_dir="$linux_dir/packages"
kernel_release=${KERNEL_RELEASE:-6.8.0-136-generic}
kernel_abi=${KERNEL_ABI:-6.8.0-136}

command -v apt >/dev/null 2>&1 || {
    echo "apt is required to download the Ubuntu guest packages" >&2
    exit 1
}
command -v dpkg-deb >/dev/null 2>&1 || {
    echo "dpkg-deb is required to unpack the Ubuntu guest packages" >&2
    exit 1
}

mkdir -p "$packages_dir" "$linux_dir/busybox-package" \
    "$linux_dir/kernel-package" "$linux_dir/kernel" "$linux_dir/headers" \
    "$linux_dir/rootfs"

cd "$packages_dir"
apt download busybox-static
apt download "linux-image-$kernel_release"
apt download "linux-headers-$kernel_release"
apt download "linux-headers-$kernel_abi"

set -- "$packages_dir"/busybox-static_*.deb
[ "$#" -eq 1 ] || {
    echo "expected exactly one BusyBox package under $packages_dir" >&2
    exit 1
}
dpkg-deb -x "$1" "$linux_dir/busybox-package"

set -- "$packages_dir"/linux-image-${kernel_release}_*.deb
[ "$#" -eq 1 ] || {
    echo "expected exactly one kernel image package under $packages_dir" >&2
    exit 1
}
dpkg-deb -x "$1" "$linux_dir/kernel-package"
install -m 0644 \
    "$linux_dir/kernel-package/boot/vmlinuz-$kernel_release" \
    "$linux_dir/kernel/vmlinuz-$kernel_release"

set -- "$packages_dir"/linux-headers-${kernel_release}_*.deb
[ "$#" -eq 1 ] || {
    echo "expected exactly one architecture-specific headers package" >&2
    exit 1
}
dpkg-deb -x "$1" "$linux_dir/headers"

set -- "$packages_dir"/linux-headers-${kernel_abi}_*.deb
[ "$#" -eq 1 ] || {
    echo "expected exactly one common headers package" >&2
    exit 1
}
dpkg-deb -x "$1" "$linux_dir/headers"

test -x "$linux_dir/busybox-package/usr/bin/busybox"
test -f "$linux_dir/kernel/vmlinuz-$kernel_release"
test -f "$linux_dir/headers/usr/src/linux-headers-$kernel_release/Makefile"

echo "Prepared $linux_dir"
echo "BusyBox: $linux_dir/busybox-package/usr/bin/busybox"
echo "Kernel:  $linux_dir/kernel/vmlinuz-$kernel_release"
echo "Headers: $linux_dir/headers/usr/src/linux-headers-$kernel_release"
