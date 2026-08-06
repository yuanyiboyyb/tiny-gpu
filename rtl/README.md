# TinyGPU RTL

`rtl/` contains the hardware source of record for TinyGPU. This directory is
for the synthesizable SystemVerilog implementation of the GPU core and its
supporting blocks. It does not contain host virtualization code, guest driver
code, or the compiler toolchain.

## Layout

```text
rtl/
├── gpu.sv                 Top-level TinyGPU module
├── core.sv                Per-core execution logic
├── dispatch.sv            Block scheduling and dispatch
├── dcr.sv                 Device control register block
├── memory_controller.sv   Program/data memory request arbitration
├── branch_stack_pkg.sv    Branch stack package definitions
└── pipeline/              Pipeline-stage modules and register file
```

## Scope

- Edit RTL here when you are changing hardware behavior.
- Keep virtualization-side wrappers in `../virtualization/`.
- Keep simple RTL simulation assets in `../sim_rtl/`.

## Related Directories

- `../sim_rtl/`: lightweight RTL simulation flow and sample kernels
- `../virtualization/`: Verilated model wrapper, compiler, runtime, and guest
  driver
