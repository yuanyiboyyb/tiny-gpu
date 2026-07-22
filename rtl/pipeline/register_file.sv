`default_nettype none
`timescale 1ns/1ns

// Per-thread register file.
// R0-R12 are writable GPRs. R13/R14/R15 are read-only blockIdx/blockDim/threadIdx.
module register_file #(
	parameter int unsigned DATA_BITS = 8,
	parameter int unsigned GPR_COUNT = 13
) (
	input  logic                 clk,
	input  logic                 reset,

	input  logic [DATA_BITS-1:0] block_id,
	input  logic [DATA_BITS-1:0] block_dim,
	input  logic [DATA_BITS-1:0] thread_id,

	input  logic [3:0]           rs_addr,
	output logic [DATA_BITS-1:0] rs_data,
	input  logic [3:0]           rt_addr,
	output logic [DATA_BITS-1:0] rt_data,

	input  logic                 write_enable,
	input  logic [3:0]           write_addr,
	input  logic [DATA_BITS-1:0] write_data,

	output logic [2:0]           nzp_read_data,
	input  logic                 nzp_write_enable,
	input  logic [2:0]           nzp_write_data
);
	localparam logic [3:0] REG_BLOCK_ID  = 4'd13;
	localparam logic [3:0] REG_BLOCK_DIM = 4'd14;
	localparam logic [3:0] REG_THREAD_ID = 4'd15;
	localparam logic [3:0] GPR_LIMIT     = 4'(GPR_COUNT);

	logic [DATA_BITS-1:0] gpr [GPR_COUNT-1:0];
	logic [DATA_BITS-1:0] gpr_d [GPR_COUNT-1:0];
	logic [2:0]           nzp;
	logic [2:0]           nzp_d;

	function automatic logic [DATA_BITS-1:0] read_register(input logic [3:0] address);begin
			if (address < GPR_LIMIT)
				read_register = gpr[address];
			else begin
				case (address)
					REG_BLOCK_ID:  read_register = block_id;
					REG_BLOCK_DIM: read_register = block_dim;
					REG_THREAD_ID: read_register = thread_id;
					default:       read_register = '0;
				endcase
			end

			// Write-through bypass for a read and WB write to the same GPR in one cycle.
			if (write_enable && (write_addr < GPR_LIMIT) && (write_addr == address))
				read_register = write_data;
		end
	endfunction

	always_comb begin
		rs_data       =  read_register(rs_addr);
		rt_data       =  read_register(rt_addr);
		nzp_read_data = nzp;

		if (nzp_write_enable)
			nzp_read_data = nzp_write_data;
	end

	// All write decisions are made in next-state logic.
	always_comb begin
		for (int unsigned index = 0; index < GPR_COUNT; index = index + 1)
			gpr_d[index] = gpr[index];
		nzp_d = nzp;

		if (write_enable && (write_addr < GPR_LIMIT))
			gpr_d[write_addr] = write_data;

		if (nzp_write_enable)
			nzp_d = nzp_write_data;
	end

	always_ff @(posedge clk) begin
		if (reset) begin
			for (int unsigned index = 0; index < GPR_COUNT; index = index + 1)
				gpr[index] <= '0;
			nzp <= 3'b000;
		end else begin
			for (int unsigned index = 0; index < GPR_COUNT; index = index + 1)
				gpr[index] <= gpr_d[index];
			nzp <= nzp_d;
		end
	end
endmodule

`default_nettype wire
