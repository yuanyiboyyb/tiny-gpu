#!/bin/sh
set -eu

qemu_bin=${QEMU_BIN:-qemu-system-x86_64}
tinygpu_socket=${TINYGPU_SOCKET:-/tmp/tinygpu.sock}
tinygpu_memory=${TINYGPU_MEMORY:-2G}

if [ -z "${TINYGPU_IMAGE:-}" ]; then
    echo "TINYGPU_IMAGE must point to a guest disk image" >&2
    exit 2
fi

if [ ! -S "$tinygpu_socket" ]; then
    echo "tiny-gpu vfio-user socket does not exist: $tinygpu_socket" >&2
    echo "start tinygpu-vfio-server before QEMU" >&2
    exit 2
fi

tinygpu_device=$(printf '%s' \
    "{\"driver\":\"vfio-user-pci\",\"socket\":{\"path\":\"$tinygpu_socket\",\"type\":\"unix\"}}")

exec "$qemu_bin" \
    -machine q35 \
    -enable-kvm \
    -m "$tinygpu_memory" \
    -drive "file=$TINYGPU_IMAGE,if=virtio" \
    -device "$tinygpu_device" \
    "$@"
