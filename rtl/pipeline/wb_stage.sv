`default_nettype none
`timescale 1ns/1ns

module wb_stage #(
	parameter int unsigned DATA_BITS = 8
) (
	input  wire [DATA_BITS-1:0] execute_result  , // Supplies the MEM writeback value.
	input  wire [          2:0] nzp_result      , // Supplies the MEM comparison result.
	input  wire [          3:0] rd_addr         , // Supplies the destination register.
	input  wire nzp_write       , // Enables comparison-state writeback.
	input  wire reg_write       , // Enables register writeback.
	input  wire is_ret          , // Marks a return instruction.
	input  wire valid           , // Marks the MEM payload as valid.

	output logic                 write_enable    , // Enables the register-file write.
	output logic [          3:0] write_addr      , // Selects the register-file write address.
	output logic [DATA_BITS-1:0] write_data      , // Supplies the register-file write data.
	output logic                 nzp_write_enable, // Enables the comparison-state write.
	output logic [          2:0] nzp_write_data  , // Supplies the comparison-state value.
	output logic                 done              // Reports thread completion.
);

	always_comb begin
		write_enable     = valid && reg_write;
		write_addr       = rd_addr;
		write_data       = execute_result;
		nzp_write_enable = valid && nzp_write;
		nzp_write_data   = nzp_result;
		done             = valid && is_ret;
	end

endmodule

`default_nettype wire
