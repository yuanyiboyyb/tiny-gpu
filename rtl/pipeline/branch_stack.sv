`default_nettype none
`timescale 1ns/1ns

// Stack used to save the PC and the active-thread mask together.
//
// operation:
//   STACK_IDLE:     keep the stack unchanged
//   STACK_POP:      pop the current top entry
//   STACK_PUSH:     push {pc_in, mask_in}
//   STACK_POP_PUSH: pop and then push {pc_in, mask_in}; this replaces the
//                   current top entry, so the stack depth does not change
module branch_stack #(
	parameter int unsigned PC_BITS        = 8,
	parameter int unsigned MASK_BITS      = 4,
	parameter int unsigned STACK_CAPACITY = 4,
	parameter int unsigned INITIAL_TOP    = 1
) (
	input  wire clk,
	input  wire reset,

	input  branch_stack_pkg::stack_operation_t operation,
	input  wire [PC_BITS-1:0]                  pc_in,
	input  wire [MASK_BITS-1:0] mask_in,

	output logic [PC_BITS-1:0]   pc_out,
	output logic [MASK_BITS-1:0] mask_out,
	output logic                 empty
);
	import branch_stack_pkg::*;

	localparam int unsigned TOP_BITS =
		(STACK_CAPACITY < 2) ? 1 : $clog2(STACK_CAPACITY + 1);
	localparam int unsigned INDEX_BITS =
		(STACK_CAPACITY < 2) ? 1 : $clog2(STACK_CAPACITY);

	logic [PC_BITS-1:0]   pc_stack   [STACK_CAPACITY-1:0];
	logic [PC_BITS-1:0]   pc_stack_d [STACK_CAPACITY-1:0];
	logic [MASK_BITS-1:0] mask_stack [STACK_CAPACITY-1:0];
	logic [MASK_BITS-1:0] mask_stack_d [STACK_CAPACITY-1:0];
	logic [TOP_BITS-1:0]  stack_top;
	logic [TOP_BITS-1:0]  stack_top_d;
	logic [INDEX_BITS-1:0] top_index;

	// stack_top is the number of valid entries. Therefore the current top
	// entry is stored at stack_top - 1. An empty stack reads as zero.
	always_comb begin
		pc_out   = '0;
		mask_out = '0;
		empty     = (stack_top == '0);

		if (stack_top != '0) begin
			pc_out   = pc_stack[INDEX_BITS'(stack_top - 1'b1)];
			mask_out = mask_stack[INDEX_BITS'(stack_top - 1'b1)];
		end
	end

	always_comb begin
		top_index   = INDEX_BITS'(stack_top - 1'b1);
		stack_top_d = stack_top;

		for (int unsigned index = 0; index < STACK_CAPACITY; index = index + 1) begin
			pc_stack_d[index]   = pc_stack[index];
			mask_stack_d[index] = mask_stack[index];
		end

		case (operation)
			STACK_IDLE: begin
				// Hold the current stack state.
			end

			STACK_POP: begin
				if (stack_top != '0)
					stack_top_d = stack_top - 1'b1;
			end

			STACK_PUSH: begin
				if (stack_top < TOP_BITS'(STACK_CAPACITY)) begin
					pc_stack_d[INDEX_BITS'(stack_top)]   = pc_in;
					mask_stack_d[INDEX_BITS'(stack_top)] = mask_in;
					stack_top_d = stack_top + 1'b1;
				end
			end

			STACK_POP_PUSH: begin
				if (stack_top != '0) begin
					// Pop followed by push replaces the current top.
					pc_stack_d[top_index]   = pc_in;
					mask_stack_d[top_index] = mask_in;
				end else begin
					// With no entry to pop, retain the push portion.
					pc_stack_d[0]   = pc_in;
					mask_stack_d[0] = mask_in;
					stack_top_d     = TOP_BITS'(1);
				end
			end

			default: begin
				// Reserved operation: hold the current stack state.
			end
		endcase
	end

	always_ff @(posedge clk) begin
		if (reset) begin
			stack_top <= TOP_BITS'(INITIAL_TOP);
			for (int unsigned index = 0; index < STACK_CAPACITY; index = index + 1) begin
				pc_stack[index]   <= '0;
				mask_stack[index] <= '0;
			end
		end else begin
			stack_top <= stack_top_d;
			for (int unsigned index = 0; index < STACK_CAPACITY; index = index + 1) begin
				pc_stack[index]   <= pc_stack_d[index];
				mask_stack[index] <= mask_stack_d[index];
			end
		end
	end

	initial begin
		if (STACK_CAPACITY == 0)
			$fatal(1, "STACK_CAPACITY must be greater than zero");
		if (INITIAL_TOP > STACK_CAPACITY)
			$fatal(1, "INITIAL_TOP must not exceed STACK_CAPACITY");
	end
endmodule

`default_nettype wire
