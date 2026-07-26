`ifndef BRANCH_STACK_PKG_SV
`define BRANCH_STACK_PKG_SV

`default_nettype none
`timescale 1ns/1ns

package branch_stack_pkg;
	typedef enum logic [1:0] {
		STACK_IDLE     = 2'd0,
		STACK_POP      = 2'd1,
		STACK_PUSH     = 2'd2,
		STACK_POP_PUSH = 2'd3
	} stack_operation_t;
endpackage

`default_nettype wire

`endif
