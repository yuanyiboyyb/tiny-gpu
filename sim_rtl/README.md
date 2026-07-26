# RTL 实现与仿真

## RTL 文件结构

RTL 位于仓库的 `rtl/` 目录，各文件功能如下：

- `gpu.sv`：GPU 顶层模块，连接 DCR、任务分发模块、多个 core 和 memory controller，并向外提供程序及数据存储器接口。
- `dcr.sv`：设备控制寄存器，保存外部写入的线程数量等任务配置。
- `dispatch.sv`：任务分发模块，根据配置向空闲 core 分配 block，并在所有 block 执行结束后产生 GPU 的 `done` 信号。
- `core.sv`：计算核心，维护共享 PC、线程运行 `mask` 和 block 信息；实例化共享 IF 以及每个线程独立的寄存器堆、ID、EX、MEM、WB 流水级。
- `memory_controller.sv`：内存控制器，将各 core 的程序和数据访问请求映射到对应 memory channel，并传递每个 channel 独立的 valid、ready、地址和数据。
- `pipeline/if_stage.sv`：取指级，通过程序存储器 valid/ready 接口读取指令；存储器未 ready 时保持请求，跳转时等待 PC 更新。
- `pipeline/id_stage.sv`：译码级，解析操作码、读取源寄存器、生成控制信号，并处理 EX/MEM 数据旁路、Load 数据冒险和分支判断。
- `pipeline/ex_stage.sv`：执行级，完成 ADD、SUB、MUL、DIV、CONST、访存地址和 NZP 的计算，同时向 ID 提供 EX 旁路数据。
- `pipeline/mem_stage.sv`：访存级，通过数据存储器 valid/ready 接口完成 Load 和 Store；等待响应时保持请求，并向 ID 提供 MEM 旁路数据。
- `pipeline/wb_stage.sv`：写回级，将执行或 Load 结果写回通用寄存器或 NZP 寄存器，并处理 RET 完成信号。
- `pipeline/register_file.sv`：每个线程独立的寄存器堆，保存通用寄存器和 NZP，并提供 block ID、block dimension 和 thread ID 数据。

当前每个 core 内的线程共享一条取指 PC，因此暂不支持同一 core 中不同线程跳转到不同地址，默认所有活动线程采用一致的分支结果。程序和数据均按字节寻址，程序存储器一次读取 16 位指令，数据存储器一次读写 8 位数据。

## 指令编码与汇编指令

每条指令宽 16 位，通用格式如下：

```text
15             12 11              8 7               4 3               0
+----------------+-----------------+-----------------+-----------------+
|     opcode     |       rd        |       rs        |       rt        |
+----------------+-----------------+-----------------+-----------------+
                                  |             imm8                  |
                                  +-----------------------------------+
```

程序存储器按字节寻址，因此一条 16 位指令占 2 个地址。`BRnzp` 的 `addr8`
是绝对字节地址，而不是相对当前 PC 的指令偏移。例如，跳转到第 12 条指令时，
目标地址为 `12 * 2 = 24`。

| opcode | 位域 `[11:0]` | 汇编格式 | 功能 |
| --- | --- | --- | --- |
| `0000` | `0000_0000_0000` | `NOP` | 不执行操作 |
| `0001` | `nzp_0_addr8` | `BR<nzp> #addr8` | 当前 NZP 与指令中的 N/Z/P 条件相交时跳转；`nzp=111` 为不依赖 NZP 的无条件跳转，例如 `BRn #24`、`BRnzp #40` |
| `0010` | `0000_rs_rt` | `CMP Rs, Rt` | 计算 `Rs - Rt`，更新线程的 NZP 状态 |
| `0011` | `rd_rs_rt` | `ADD Rd, Rs, Rt` | `Rd = Rs + Rt` |
| `0100` | `rd_rs_rt` | `SUB Rd, Rs, Rt` | `Rd = Rs - Rt` |
| `0101` | `rd_rs_rt` | `MUL Rd, Rs, Rt` | `Rd = Rs * Rt` |
| `0110` | `rd_rs_rt` | `DIV Rd, Rs, Rt` | `Rd = Rs / Rt` |
| `0111` | `rd_rs_0000` | `LDR Rd, Rs` | 从地址 `Rs` 读取数据并写入 `Rd` |
| `1000` | `0000_rs_rt` | `STR Rs, Rt` | 将 `Rt` 写入地址 `Rs` |
| `1001` | `rd_imm8` | `CONST Rd, #imm8` | 将 8 位立即数写入 `Rd` |
| `1010` | `0000_0000_0000` | `JOIN` | 分支路径结束并切换到分支栈中的下一执行上下文 |
| `1011`–`1110` | 保留 | — | 保留给后续扩展 |
| `1111` | `0000_0000_0000` | `RET` | 结束当前线程 |

`rd`、`rs` 和 `rt` 都是 4 位寄存器编号。`R0`–`R12` 是可读写通用
寄存器，`R13`、`R14`、`R15` 分别对应只读汇编寄存器
`%blockIdx`、`%blockDim`、`%threadIdx`。

### `JOIN` 分支合并点指令设计

`JOIN` 使用尚未占用的 opcode `4'b1010`，完整编码为
`16'b1010_0000_0000_0000`。其余 12 位保留且必须编码为 0。该指令不访问
通用寄存器、NZP 或数据存储器。

分支栈中的每个条目保存一组 `{pc, mask}` 执行上下文。发生线程分歧时，
当前先执行其中一条路径，其他尚未执行的路径压入分支栈：

```text
当前上下文：{current_path_pc, current_path_mask}

栈顶 -> 尚未执行的路径 {pending_path_pc, pending_path_mask}
        更外层分支上下文（若存在）
```

当前路径执行到 `JOIN` 时，必须先检查栈顶上下文能否与当前上下文合并：

```systemverilog
can_merge = !stack_empty && (stack_pc == current_pc);
```

其中 PC 相等表示两组线程都已经到达同一条 `JOIN` 指令。控制器按照下表处理：

| 条件 | 分支栈操作 | 下一 PC | 下一 mask | 含义 |
| --- | --- | --- | --- | --- |
| `stack_empty` | `STACK_IDLE` | `current_pc + 2` | `current_mask` | 没有待执行或待合并的路径，顺序继续 |
| `can_merge` | `STACK_POP` | `current_pc` | `current_mask \| stack_mask` | 栈顶路径和当前路径在同一 `JOIN` 汇合，合并线程 mask，并保持 PC 以检查新的栈顶 |
| `!stack_empty && !can_merge` | `STACK_POP_PUSH` | `stack_pc` | `stack_mask` | 栈顶仍是另一条待执行路径；用当前 `{current_pc, current_mask}` 替换栈顶并切换过去执行 |

最后一种情况下，当前路径已经到达 `JOIN`，但另一条路径还未执行完成。
`STACK_POP_PUSH` 的输入是当前 `{current_pc, current_mask}`，原栈顶
`{stack_pc, stack_mask}` 则在同一个周期被 Core 采样为下一执行上下文。另一条
路径随后也到达该 `JOIN` 时，栈顶 PC 与当前 PC 相等，进入 `can_merge` 分支，
将两个 mask 按位或后弹栈。合并周期保持 PC 不变，以便下一周期继续检查新的
栈顶：若仍可合并则继续合并；若是其他待执行路径则进行上下文切换；只有栈空
时才执行 `current_pc + 2`，离开 `JOIN`。这样同一合并点存在两个以上待合并
上下文时也不会遗漏。

对于两条互斥分支，正常情况下还应满足
`(current_mask & stack_mask) == 0`。该条件可作为仿真断言检查分支控制是否
错误，但不影响合并操作使用按位或生成完整活动 mask。

`JOIN` 的 opcode 已在 `id_stage.sv` 中预留空的译码分支，但尚未产生控制
信号，也未接入 `core.sv`。接入时还需要保证该指令只被流水线接受一次，并在
切换上下文时冲刷错误路径上已取出的指令。

## 环境准备

Python 依赖由仓库根目录的 `pyproject.toml` 和 `uv.lock` 管理：

```sh
uv sync
```

系统还需要安装 Verilator、GNU Make 和 GTKWave：

```sh
sudo apt install verilator make gtkwave
```

## 测试命令

在仓库根目录运行矩阵加法测试：

```sh
uv run make -C sim_rtl matadd
```

运行矩阵乘法测试：

```sh
uv run make -C sim_rtl matmul
```

仿真会生成完整波形：

```text
sim_rtl/dump.vcd
```

使用 GTKWave 查看：

```sh
uv run make -C sim_rtl wave
```
