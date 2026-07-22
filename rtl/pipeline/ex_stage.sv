`default_nettype none
`timescale 1ns/1ns

module ex_stage #(
	parameter int unsigned DATA_BITS             = 8
) (
	input  logic                             clk              ,
	input  logic                             reset            ,
	input  logic                             start            ,
	input  logic [                      3:0] rd_addr          ,
	input  logic [            DATA_BITS-1:0] rs_data          ,
	input  logic [            DATA_BITS-1:0] rt_data          ,
	input  logic [            DATA_BITS-1:0] imm              ,
	input  logic [                      1:0] alu_op           ,
	input  logic                             alu_src          ,
	input  logic                             alu_enable       ,
	input  logic                             nzp_write        ,
	input  logic                             reg_write        ,
	input  logic                             mem_read         ,
	input  logic                             mem_write        ,
	input  logic [            DATA_BITS-1:0] store_data       ,
	input  logic                             is_ret           ,
	input  logic                             valid            ,
	input  logic                             ready_in         ,
	// EX calculation results.
	output logic [            DATA_BITS-1:0] execute_result   ,
	output logic [                      2:0] nzp_result       ,
	// Signals passed to the next pipeline stage.
	output logic [                      3:0] rd_addr_out      ,
	output logic                             nzp_write_out    ,
	output logic                             reg_write_out    ,
	output logic                             mem_read_out     ,
	output logic                             mem_write_out    ,
	output logic [            DATA_BITS-1:0] store_data_out   ,
	output logic                             is_ret_out       ,
	output logic                             valid_out        ,
	// Combinational forwarding toward ID.
	output logic                             forward_reg_write_enable,
	output logic [                      3:0] forward_reg_write_addr  ,
	output logic [            DATA_BITS-1:0] forward_data            ,
	output logic                             forward_nzp_write_enable,
	output logic [                      2:0] forward_nzp_data        ,

	output logic                             ready_out
);
	logic [            DATA_BITS-1:0] execute_result_d   ;
	logic [                      2:0] nzp_result_d       ;
	logic [                      3:0] rd_addr_out_d      ;
	logic                             nzp_write_out_d    ;
	logic                             reg_write_out_d    ;
	logic                             mem_read_out_d     ;
	logic                             mem_write_out_d    ;
	logic [            DATA_BITS-1:0] store_data_out_d   ;
	logic                             is_ret_out_d       ;
	logic                             valid_out_d        ;

	always_comb begin
		// Hold the previous EX/MEM payload. valid_out marks whether it is valid.
		execute_result_d    = execute_result;
		nzp_result_d        = nzp_result;
		rd_addr_out_d       = rd_addr_out;
		nzp_write_out_d     = nzp_write_out;
		reg_write_out_d     = reg_write_out;
		mem_read_out_d      = mem_read_out;
		mem_write_out_d     = mem_write_out;
		store_data_out_d    = store_data_out;
		is_ret_out_d        = is_ret_out;
		valid_out_d         = valid_out;
		// EX has no additional wait condition, so backpressure from MEM is
		// propagated directly to ID in the same cycle.
		ready_out = ready_in;

		if (start && ready_in && valid) begin
			// Start a fresh EX/MEM entry.
			execute_result_d = '0;
			nzp_result_d     = '0;
			rd_addr_out_d    = rd_addr;
			nzp_write_out_d  = nzp_write;
			reg_write_out_d  = reg_write;
			mem_read_out_d   = mem_read;
			mem_write_out_d  = mem_write;
			store_data_out_d = store_data;
			is_ret_out_d     = is_ret;
			valid_out_d      = 1'b1;

			// CONST writes the immediate directly to the result bus.
			if (alu_src) begin
				execute_result_d = imm;
			end else if (alu_enable) begin
				case (alu_op)
					2'b00: execute_result_d = rs_data + rt_data;
					2'b01: execute_result_d = rs_data - rt_data;
					2'b10: execute_result_d = rs_data * rt_data;
					2'b11: execute_result_d = (rt_data == '0) ? '0 : rs_data / rt_data;
					default: execute_result_d = '0;
				endcase
			end else if (mem_read || mem_write) begin
				// LDR/STR use Rs as the data-memory address.
				execute_result_d = rs_data;
			end

			// NZP encoding is {negative, zero, positive}.
			if (nzp_write) begin
				nzp_result_d = {
					$signed(rs_data) < $signed(rt_data),
					rs_data == rt_data,
					$signed(rs_data) > $signed(rt_data)
				};
			end
		end else if (ready_in && !valid) begin
			// The old payload may remain in the data registers; valid marks it stale.
			valid_out_d = 1'b0;
		end

		// ALU and immediate results are available before the EX/MEM clock edge.
		// A Load is excluded because execute_result_d is its address, not its data.
		forward_reg_write_enable = start && ready_in && valid && reg_write && !mem_read;
		forward_reg_write_addr   = rd_addr;
		forward_data             = execute_result_d;
		forward_nzp_write_enable = start && ready_in && valid && nzp_write;
		forward_nzp_data         = nzp_result_d;
	end

	always_ff @(posedge clk) begin
		if (reset) begin
			execute_result <= '0;
			nzp_result     <= '0;
			rd_addr_out    <= '0;
			nzp_write_out  <= '0;
			reg_write_out  <= '0;
			mem_read_out   <= '0;
			mem_write_out  <= '0;
			store_data_out <= '0;
			is_ret_out     <= '0;
			valid_out      <= '0;
		end else begin
			execute_result <= execute_result_d;
			nzp_result     <= nzp_result_d;
			rd_addr_out    <= rd_addr_out_d;
			nzp_write_out  <= nzp_write_out_d;
			reg_write_out  <= reg_write_out_d;
			mem_read_out   <= mem_read_out_d;
			mem_write_out  <= mem_write_out_d;
			store_data_out <= store_data_out_d;
			is_ret_out     <= is_ret_out_d;
			valid_out      <= valid_out_d;
		end
	end

endmodule

`default_nettype wire
