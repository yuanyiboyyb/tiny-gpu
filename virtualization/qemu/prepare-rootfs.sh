#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
virtualization_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
project_dir=$(dirname -- "$virtualization_dir")
workspace_dir=$(dirname -- "$project_dir")
rootfs_workspace=${TINYGPU_ROOTFS_DIR:-"$workspace_dir/tinygpu-rootfs"}
busybox=${BUSYBOX_BIN:-"$rootfs_workspace/busybox"}

command -v apt >/dev/null 2>&1 || {
    echo "apt is required to download busybox-static" >&2
    exit 1
}
command -v dpkg-deb >/dev/null 2>&1 || {
    echo "dpkg-deb is required to unpack busybox-static" >&2
    exit 1
}

download_dir=$(mktemp -d)
unpack_dir="$download_dir/unpacked"
trap 'rm -rf "$download_dir"' EXIT HUP INT TERM

mkdir -p "$rootfs_workspace" "$(dirname -- "$busybox")" "$unpack_dir"
(
    cd "$download_dir"
    apt download busybox-static
)

set -- "$download_dir"/busybox-static_*.deb
[ "$#" -eq 1 ] || {
    echo "expected exactly one downloaded busybox-static package" >&2
    exit 1
}

dpkg-deb -x "$1" "$unpack_dir"
install -m 0755 "$unpack_dir/usr/bin/busybox" "$busybox"

echo "Prepared rootfs workspace: $rootfs_workspace"
echo "BusyBox: $busybox"
