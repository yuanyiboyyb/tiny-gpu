#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
virtualization_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
project_dir=$(dirname -- "$virtualization_dir")
workspace_dir=$(dirname -- "$project_dir")
linux_dir=${TINYGPU_LINUX_DIR:-"$workspace_dir/tinygpu-linux"}

qemu_bin=${QEMU_BIN:-"$workspace_dir/qemu-x86/build/qemu-system-x86_64"}
kernel_image=${KERNEL_IMAGE:-"$linux_dir/kernel/vmlinuz-6.8.0-136-generic"}
initramfs_image=${INITRAMFS_IMAGE:-"$linux_dir/initramfs.cpio.gz"}
guest_memory=${GUEST_MEMORY:-512M}
guest_cpus=${GUEST_CPUS:-2}
exec_delay_ms=${TINYGPU_EXEC_DELAY_MS:-1}

require_executable()
{
    if [ ! -x "$1" ]; then
        echo "missing executable: $1" >&2
        exit 1
    fi
}

require_file()
{
    if [ ! -f "$1" ]; then
        echo "missing file: $1" >&2
        exit 1
    fi
}

require_executable "$qemu_bin"
require_file "$kernel_image"
require_file "$initramfs_image"

if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
    accelerator=kvm
    cpu_model=host
else
    accelerator=tcg,thread=multi
    cpu_model=max
    echo "KVM is unavailable; using TCG emulation" >&2
fi

echo "QEMU:      $qemu_bin"
echo "Kernel:    $kernel_image"
echo "Initramfs: $initramfs_image"
echo "Device:    tinygpu (exec-delay-ms=$exec_delay_ms)"
echo "Exit QEMU with Ctrl-a x"

exec "$qemu_bin" \
    -machine q35 \
    -accel "$accelerator" \
    -cpu "$cpu_model" \
    -m "$guest_memory" \
    -smp "$guest_cpus" \
    -kernel "$kernel_image" \
    -initrd "$initramfs_image" \
    -append "console=ttyS0,115200 earlyprintk=serial rdinit=/init panic=-1 nokaslr" \
    -device "tinygpu,exec-delay-ms=$exec_delay_ms" \
    -no-reboot \
    -nographic \
    "$@"
