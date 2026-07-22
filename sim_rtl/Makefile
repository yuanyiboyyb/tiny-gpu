SIM           ?= verilator
TOPLEVEL_LANG := verilog
TOPLEVEL      := gpu
MODULE        := test_gpu
SIM_BUILD     := build/sim_build

RTL_DIR := $(abspath ../rtl)
VERILOG_SOURCES := \
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

.PHONY: matadd matmul wave

matadd:
	$(MAKE) sim TESTCASE=test_matadd

matmul:
	$(MAKE) sim TESTCASE=test_matmul

wave:
	gtkwave dump.vcd

include $(shell cocotb-config --makefiles)/Makefile.sim
