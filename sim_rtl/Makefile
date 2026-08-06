SIM           ?= verilator
TOPLEVEL_LANG := verilog
TOPLEVEL      := gpu
MODULE        := test_gpu
SIM_BUILD     := build/sim_build

RTL_DIR := $(abspath ../rtl)
VERILOG_SOURCES := \
	$(RTL_DIR)/branch_stack_pkg.sv \
	$(RTL_DIR)/pipeline/branch_stack.sv \
	$(RTL_DIR)/gpu.sv \
	$(RTL_DIR)/core.sv \
	$(RTL_DIR)/dcr.sv \
	$(RTL_DIR)/dispatch.sv \
	$(RTL_DIR)/memory_controller.sv \
	$(RTL_DIR)/pipeline/register_file.sv \
	$(RTL_DIR)/pipeline/if_stage.sv \
	$(RTL_DIR)/pipeline/id_stage.sv \
	$(RTL_DIR)/pipeline/ex_stage.sv \
	$(RTL_DIR)/pipeline/mem_stage.sv \
	$(RTL_DIR)/pipeline/wb_stage.sv

COMPILE_ARGS += -Wall -Wno-fatal --trace --trace-structs --trace-depth 0
SIM_ARGS     += --trace

TINYGPU_CC ?= ../virtualization/build/compiler/tinygpu-cc
TINYGPU_AS ?= ../virtualization/build/compiler/tinygpu-as

.PHONY: matadd matadd_kernel matmul matmul_branch_join wave

matadd_kernel: matadd.s matadd.bin

matadd.s: matadd.tu
	$(TINYGPU_CC) $< -S -o $@

matadd.bin: matadd.s
	$(TINYGPU_AS) $< -o $@

matadd: matadd_kernel
	$(MAKE) sim TESTCASE=test_matadd

matmul:
	$(MAKE) sim TESTCASE=test_matmul

matmul_branch_join:
	$(MAKE) sim TESTCASE=test_matmul_branch_join

COCOTB_MAKEFILES := $(shell cocotb-config --makefiles 2>/dev/null)

ifneq ($(COCOTB_MAKEFILES),)
include $(COCOTB_MAKEFILES)/Makefile.sim
else ifneq ($(filter matadd matmul matmul_branch_join sim,$(MAKECMDGOALS)),)
$(error cocotb-config was not found; install cocotb before running RTL simulation)
endif
