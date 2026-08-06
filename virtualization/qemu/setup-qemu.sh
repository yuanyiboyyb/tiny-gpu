#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
virtualization_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
project_dir=$(dirname -- "$virtualization_dir")
workspace_dir=$(dirname -- "$project_dir")
qemu_dir=${QEMU_DIR:-"$workspace_dir/qemu-x86"}

test -f "$qemu_dir/meson.build" || {
    echo "QEMU source tree not found: $qemu_dir" >&2
    exit 1
}

install -m 0644 "$script_dir/hw/misc/tinygpu.c" \
    "$qemu_dir/hw/misc/tinygpu.c"
install -m 0644 "$script_dir/hw/misc/tinygpu-verilator.cpp" \
    "$qemu_dir/hw/misc/tinygpu-verilator.cpp"
install -m 0644 "$script_dir/hw/misc/tinygpu-verilator.h" \
    "$qemu_dir/hw/misc/tinygpu-verilator.h"
install -m 0644 "$script_dir/tests/qtest/tinygpu-test.c" \
    "$qemu_dir/tests/qtest/tinygpu-test.c"

if ! grep -q '^config TINYGPU$' "$qemu_dir/hw/misc/Kconfig"; then
    patch -d "$qemu_dir" -p1 < "$script_dir/qemu-tinygpu.patch"
else
    echo "TinyGPU QEMU build integration is already present; patch skipped"
fi

echo "TinyGPU sources installed into $qemu_dir"
