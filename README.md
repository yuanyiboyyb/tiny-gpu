# TinyGPU

TinyGPU 是一个面向学习和实验的、可综合 SystemVerilog GPU。这个仓库不再是最初的“单个 Verilog GPU 示例”，而是一套从硬件、编译器到虚拟设备和 Linux 用户态运行时的端到端实现：TinyGPU 程序可以经过编译，装载到 RTL/Verilator 模型中执行，也可以通过 PCI 设备在 QEMU 虚拟机内执行。

项目当前仍处于实验阶段，重点是把 GPU 的关键机制做成一条可以运行、可以仿真、可以观察波形的完整链路，而不是追求真实 GPU 的性能或兼容性。

## 当前实现

- 多核心 GPU 顶层：设备控制寄存器、block 调度器、计算核心和程序/数据存储器控制器。
- 五级流水线：IF、ID、EX、MEM、WB。
- SIMD 风格线程执行：一个 core 处理一个 block，block 内线程共享取指流，每个线程拥有独立寄存器状态。
- 分支发散与收敛：通过 active-thread mask、branch stack 和 `JOIN` 指令依次执行不同分支路径。
- TinyGPU 前端和工具链：TinyGPU 源语言（`.tu`）、汇编器（`.s`）和机器码（`.bin`）。
- Verilator 设备模型、C++ runtime 和 Linux PCI 字符设备驱动。
- QEMU 自定义 `tinygpu` PCI 设备，以及 Linux guest、BusyBox initramfs 和 guest 内工具链的构建脚本。
- Cocotb/Verilator RTL 测试：矩阵加法、矩阵乘法和分支发散/`JOIN` 场景。

## 架构概览

```text
TinyGPU source (.tu)
        │ tinygpu-cc
        ├── assembly (.s)
        │       │ tinygpu-as
        └───────┴── program binary (.bin)
                         │
             ┌───────────▼───────────┐
             │ TinyGPU RTL / Verilator│
             │  dispatch + cores      │
             │  IF→ID→EX→MEM→WB       │
             │  branch stack + JOIN   │
             └───────────┬───────────┘
                         │ PCI BAR / IRQ
             ┌───────────▼───────────┐
             │ Linux driver + runtime │
             └───────────┬───────────┘
                         │
                    QEMU guest
```

默认顶层参数为 2 个 core、每个 block 最多 4 个线程、8 位数据存储器和 16 位程序指令。参数都定义在 RTL 顶层 `gpu` 模块中，可以在仿真或集成时覆盖。

### RTL 执行模型

`gpu` 接收 kernel 启动信号和线程数，将线程划分为 block 并分派给空闲 core。每个 core 维护共享 PC 和当前 active mask；线程的通用寄存器、N/Z/P 比较状态以及 `%blockIdx`、`%blockDim`、`%threadIdx` 元数据保存在独立的线程寄存器上下文中。

条件分支导致线程走向不同地址时，core 将其中一条路径和对应 mask 压入 branch stack，先执行另一条路径。两条路径在同一个 `JOIN` 汇合后恢复完整线程 mask。这种实现保留了单一取指流，同时显式展示了 GPU 中的 branch divergence/reconvergence。

程序存储器和数据存储器均为 8 位地址空间、容量 256 个地址单元。程序存储器每次传输一条 16 位指令，因此一条程序最多占用 128 条指令；数据存储器保存 8 位值。

## 指令集

所有指令宽度为 16 位，汇编器会输出可直接装载到程序存储器的二进制文件。

| 指令 | 作用 |
| --- | --- |
| `NOP` | 空操作 |
| `BR<nzp> #addr8` | 根据 N/Z/P 状态跳转；`111` 表示无条件跳转 |
| `CMP Rs, Rt` | 比较两个寄存器并更新 N/Z/P |
| `ADD` / `SUB` / `MUL` / `DIV` | 整数算术 |
| `LDR Rd, Rs` | 从数据存储器读取 |
| `STR Rs, Rt` | 向数据存储器写入 |
| `CONST Rd, #imm8` | 将 8 位立即数写入寄存器 |
| `JOIN` | 合并发散路径 |
| `RET` | 当前线程结束 |

`R0`–`R12` 是可读写通用寄存器；`R13`、`R14`、`R15` 分别映射到 `%blockIdx`、`%blockDim`、`%threadIdx`。

## 目录说明

```text
tiny-gpu/
├── rtl/                         可综合 SystemVerilog 硬件实现
│   ├── gpu.sv                   顶层 GPU
│   ├── core.sv                  core、线程 mask 和控制流
│   ├── dispatch.sv              block 调度
│   ├── dcr.sv                   设备控制寄存器
│   ├── memory_controller.sv     程序/数据存储器请求仲裁
│   └── pipeline/                IF/ID/EX/MEM/WB、寄存器文件和 branch stack
├── sim_rtl/                     Verilator + Cocotb 仿真及示例 kernel
│   ├── matadd.tu                TinyGPU 源程序示例
│   ├── matadd.s / matadd.bin    编译后的示例
│   └── test_gpu.py              仿真入口
├── virtualization/
│   ├── compiler/                前端、代码生成器和汇编器
│   ├── device/                  Verilator C++ 封装
│   ├── runtime/                 用户态 C++ runtime 和示例
│   ├── driver/                  Linux PCI 驱动模块
│   ├── qemu/                    QEMU 设备、补丁和 guest 构建脚本
│   └── test/                    host-side 设备模型测试
└── pyproject.toml / uv.lock     Python 仿真环境
```

`virtualization/build/`、`sim_rtl/build/` 等目录是构建产物，不是源码入口。硬件修改应放在 `rtl/`，不要继续使用已经移除的旧 `src/` 目录。

## 环境准备

推荐使用 Python 3.12 和 `uv` 管理仿真依赖：

```sh
cd /home/yyb/tinygpu-workspace/tiny-gpu
uv sync
```

RTL 仿真还需要 Verilator、GNU Make 和 Cocotb；需要查看波形时再安装 GTKWave：

```sh
sudo apt install verilator make gtkwave
```

构建虚拟化部分还需要 CMake、C++ 编译器和 Linux 内核构建依赖。完整 QEMU 流程另外需要 QEMU 源码、Linux 源码和 `busybox-static`，见下文。

## 最快验证路径

### 1. 构建 host-side 模型、编译器和 runtime

```sh
make -C virtualization build
```

运行 host-side 测试：

```sh
make -C virtualization test
make -C virtualization compiler-test
make -C virtualization runtime-test
```

### 2. 运行 RTL 仿真

```sh
# 矩阵加法：同时演示 TinyGPU 源码到机器码的编译
uv run make -C sim_rtl matadd

# 2×2 矩阵乘法
uv run make -C sim_rtl matmul

# 矩阵乘法后执行条件分支和 JOIN
uv run make -C sim_rtl matmul_branch_join
```

仿真会生成 `sim_rtl/dump.vcd`。使用 GTKWave 查看：

```sh
gtkwave sim_rtl/dump.vcd
```

### 3. 手动使用编译器

```sh
make -C virtualization compiler-build

virtualization/build/compiler/tinygpu-cc \
    sim_rtl/matadd.tu -S -o /tmp/matadd.s

virtualization/build/compiler/tinygpu-as \
    /tmp/matadd.s -o /tmp/matadd.bin
```

`tinygpu-cc` 不带 `-S` 时会直接输出机器码：

```sh
virtualization/build/compiler/tinygpu-cc \
    sim_rtl/matadd.tu -o /tmp/matadd.bin
```

## TinyGPU 源语言示例

`sim_rtl/matadd.tu` 展示了 kernel 参数区和内建线程索引：

```c
global KernelArgs {
    u8 *input_a;
    u8 *input_b;
    u8 *output;
};

u8 main() {
    u8 index = blockIdx * blockDim + threadIdx;
    KernelArgs.output[index] =
        KernelArgs.input_a[index] + KernelArgs.input_b[index];
    return 0;
}
```

当前语言是为 TinyGPU kernel 设计的最小前端，语法和类型系统仍在扩展中；需要查看解析结果时可以运行 `tinygpu-frontend sim_rtl/matadd.tu`。

## QEMU + Linux guest 端到端流程

虚拟化流程的外部目录约定如下：

```text
/home/yyb/tinygpu-workspace/
├── tiny-gpu/          本仓库
├── qemu-x86/          QEMU 源码和构建目录
├── linux-src/         x86_64 Linux 源码和构建目录
└── tinygpu-rootfs/    BusyBox/rootfs/initramfs 工作目录
```

先在工作区根目录准备 QEMU 和 Linux 源码，然后按照 `virtualization/README.md` 完成内核配置和编译。核心命令如下（均可从本仓库根目录执行）：

```sh
# 构建 TinyGPU host-side 库和驱动
make -C virtualization build
make -C virtualization driver-build KDIR=../linux-src

# 第一次运行时准备 busybox-static
./virtualization/qemu/prepare-rootfs.sh

# 构建 QEMU（默认查找 ../qemu-x86）
./virtualization/qemu/build-qemu.sh

# 构建 guest 工具、驱动、kernel modules 和 initramfs
./virtualization/qemu/build-rootfs.sh

# 启动 guest；有 KVM 时自动使用 KVM，否则退回 TCG
./virtualization/qemu/run-qemu.sh
```

进入 guest 后可以检查设备并运行示例：

```sh
ls -l /dev/tinygpu*
tinygpu-runtime-matadd /opt/tinygpu/kernels/matadd.bin /dev/tinygpu0
```

runtime 通过 `/dev/tinygpu0` 提交同步 kernel。驱动会分配 DMA 缓冲区，写入程序、输入和输出描述符，启动 PCI 设备，并通过完成/错误中断等待执行结束。

如果外部目录不是默认位置，可以覆盖脚本变量，例如：

```sh
KDIR=/path/to/linux-src make -C virtualization driver-build
QEMU_DIR=/path/to/qemu-x86 ./virtualization/qemu/build-qemu.sh
LINUX_SRC_DIR=/path/to/linux-src ./virtualization/qemu/build-rootfs.sh
```

## 常用清理命令

```sh
make -C virtualization clean
make -C virtualization driver-clean KDIR=../linux-src
rm -rf sim_rtl/build sim_rtl/dump.vcd
```

## 当前边界

- 程序和数据存储器均为 256 地址单元，数据宽度为 8 位；程序指令宽度为 16 位。
- 当前设备模型面向单个同步 kernel 提交，不提供真实 GPU 的并发 kernel 调度。
- branch stack 容量和线程数由 RTL 参数限制；复杂控制流需要保证所有发散路径最终到达对应的 `JOIN`。
- PCI vendor/device ID 仍是开发用占位值（`0x1234:0x1001`），不代表正式硬件设备。
- cache、性能优化和更完整的语言特性尚未实现；该项目的主要用途是学习、验证 RTL 和观察执行过程。

## 相关文档

- [RTL 结构与指令集](rtl/README.md)
- [虚拟化构建与运行说明](virtualization/README.md)
- [RTL 仿真说明](sim_rtl/README.md)
