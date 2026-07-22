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
