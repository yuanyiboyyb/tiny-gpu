# RTL Implementation and Simulation

## RTL Structure

The RTL source is located in the repository's `rtl/` directory:

- `gpu.sv`: Top-level GPU module connecting the device control register,
  dispatcher, cores, and memory controllers.
- `dcr.sv`: Stores kernel configuration such as the number of threads.
- `dispatch.sv`: Assigns thread blocks to available cores and reports when all
  blocks have completed.
- `core.sv`: Maintains the shared program counter, active-thread mask, branch
  state, and per-block execution state.
- `branch_stack_pkg.sv`: Defines the branch-stack operations.
- `pipeline/branch_stack.sv`: Stores execution contexts for pending branch
  paths.
- `memory_controller.sv`: Maps memory requests from cores to the available
  external memory channels.
- `pipeline/if_stage.sv`: Fetches instructions through the program-memory
  ready/valid interface.
- `pipeline/id_stage.sv`: Decodes instructions, handles forwarding and
  hazards, and resolves branches and `JOIN`.
- `pipeline/ex_stage.sv`: Executes arithmetic operations and comparisons.
- `pipeline/mem_stage.sv`: Performs data-memory reads and writes.
- `pipeline/wb_stage.sv`: Writes results and comparison flags back to the
  register file.
- `pipeline/register_file.sv`: Stores each thread's general-purpose registers,
  comparison flags, and thread metadata.

Each core has one shared instruction-fetch PC. When threads within a core take
different branch paths, the paths are executed one at a time with different
active-thread masks. Paths that are not currently running are saved in the
branch stack and resumed later.

Program and data memory are byte-addressed. Program memory transfers one
16-bit instruction at a time, while data memory transfers 8-bit values.

## Instruction Set

Each instruction is 16 bits wide:

```text
15             12 11              8 7               4 3               0
+----------------+-----------------+-----------------+-----------------+
|     opcode     |       rd        |       rs        |       rt        |
+----------------+-----------------+-----------------+-----------------+
                                  |             imm8                  |
                                  +-----------------------------------+
```

Because program memory is byte-addressed, each 16-bit instruction occupies two
addresses. The address in a branch instruction is an absolute byte address.
For example, the twelfth instruction begins at address `12 * 2 = 24`.

| Opcode | Bits `[11:0]` | Assembly | Description |
| --- | --- | --- | --- |
| `0000` | `0000_0000_0000` | `NOP` | No operation |
| `0001` | `nzp_0_addr8` | `BR<nzp> #addr8` | Branch when the thread's comparison state matches the selected N/Z/P flags; `nzp=111` is unconditional |
| `0010` | `0000_rs_rt` | `CMP Rs, Rt` | Compare `Rs` with `Rt` and update the thread's N/Z/P state |
| `0011` | `rd_rs_rt` | `ADD Rd, Rs, Rt` | Add |
| `0100` | `rd_rs_rt` | `SUB Rd, Rs, Rt` | Subtract |
| `0101` | `rd_rs_rt` | `MUL Rd, Rs, Rt` | Multiply |
| `0110` | `rd_rs_rt` | `DIV Rd, Rs, Rt` | Divide |
| `0111` | `rd_rs_0000` | `LDR Rd, Rs` | Load from data memory |
| `1000` | `0000_rs_rt` | `STR Rs, Rt` | Store to data memory |
| `1001` | `rd_imm8` | `CONST Rd, #imm8` | Load an 8-bit constant |
| `1010` | `0000_0000_0000` | `JOIN` | Rejoin diverged thread paths |
| `1011`–`1110` | Reserved | — | Reserved for future instructions |
| `1111` | `0000_0000_0000` | `RET` | Finish the current thread |

`R0` through `R12` are writable general-purpose registers. `R13`, `R14`, and
`R15` expose `%blockIdx`, `%blockDim`, and `%threadIdx`.

## Branch Divergence and Join

An execution context consists of a program location and an active-thread mask.
When all active threads make the same branch decision, execution continues
normally. When their decisions differ, the core divides them into two groups:
threads that take the branch and threads that continue along the fall-through
path.

The core executes one group first and saves the other group in the branch
stack. Only the threads selected by the current mask execute instructions, so
register and memory operations remain isolated between the two paths.

Both paths must reach the same `JOIN` instruction:

```text
                         +-> branch path -----+
active threads -> branch |                    +-> JOIN -> all threads
                         +-> fall-through ----+
```

When the first group reaches `JOIN`, its continuation is saved and the pending
group is resumed. When the second group reaches the same join point, the two
thread masks are combined. Execution then continues after `JOIN` with all
threads active again.

This scheme keeps the fetch logic simple: a core still fetches one instruction
stream at a time, while the branch stack preserves the paths that must be
executed later.

## Environment Setup

Python dependencies are managed by `pyproject.toml` and `uv.lock`:

```sh
uv sync
```

The simulation also requires Verilator, GNU Make, and optionally GTKWave:

```sh
sudo apt install verilator make gtkwave
```

## Running Tests

Run the matrix-addition test from the repository root:

```sh
uv run make -C sim_rtl matadd
```

Run the original matrix-multiplication test:

```sh
uv run make -C sim_rtl matmul
```

Run the branch-divergence and join test:

```sh
uv run make -C sim_rtl matmul_branch_join
```

The branch test first computes the matrix-multiplication result
`[7, 10, 15, 22]`. It then sets results greater than 10 to zero. Two threads
take the zeroing path, while the other two preserve their results. After
`JOIN`, all four threads write back the expected result:

```text
[7, 10, 0, 0]
```

Each simulation generates a waveform at:

```text
sim_rtl/dump.vcd
```

Open the waveform with:

```sh
uv run make -C sim_rtl wave
```
