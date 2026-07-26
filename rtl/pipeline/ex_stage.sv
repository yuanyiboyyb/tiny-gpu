`default_nettype none
`timescale 1ns/1ns

module ex_stage #(parameter int unsigned DATA_BITS             = 8) (
	input  wire                  clk                     , // System clock.
	input  wire                  reset                   , // Resets the execute stage.
	input  wire                  start                   , // Enables execution for this thread.
	input  wire  [          3:0] rd_addr                 , // Supplies the destination register.
	input  wire  [DATA_BITS-1:0] rs_data                 , // Supplies the first operand.
	input  wire  [DATA_BITS-1:0] rt_data                 , // Supplies the second operand.
	input  wire  [DATA_BITS-1:0] imm                     , // Supplies the immediate value.
	input  wire  [          1:0] alu_op                  , // Selects the arithmetic operation.
	input  wire                  alu_src                 , // Selects the immediate source.
	input  wire                  alu_enable              , // Enables arithmetic execution.
	input  wire                  nzp_write               , // Enables comparison-state writeback.
	input  wire                  reg_write               , // Enables register writeback.
	input  wire                  mem_read                , // Marks a memory-read operation.
	input  wire                  mem_write               , // Marks a memory-write operation.
	input  wire  [DATA_BITS-1:0] store_data              , // Supplies data for a store.
	input  wire                  is_ret                  , // Marks a return instruction.
	input  wire                  valid                   , // Marks the input payload as valid.
	input  wire                  ready_in                , // Indicates that MEM can accept data.
	// EX calculation results.
	output logic [DATA_BITS-1:0] execute_result          , // Provides the execution result.
	output logic [          2:0] nzp_result              , // Provides the comparison result.
	// Signals passed to the next pipeline stage.
	output logic [          3:0] rd_addr_out             , // Passes the destination register to MEM.
	output logic                 nzp_write_out           , // Passes the comparison write control.
	output logic                 reg_write_out           , // Passes the register write control.
	output logic                 mem_read_out            , // Passes the memory-read control.
	output logic                 mem_write_out           , // Passes the memory-write control.
	output logic [DATA_BITS-1:0] store_data_out          , // Passes the store data to MEM.
	output logic                 is_ret_out              , // Passes the return flag to MEM.
	output logic                 valid_out               , // Marks the EX output as valid.
	// Combinational forwarding toward ID.
	output logic                 forward_reg_write_enable, // Enables register-result forwarding.
	output logic [          3:0] forward_reg_write_addr  , // Identifies the forwarded register.
	output logic [DATA_BITS-1:0] forward_data            , // Supplies the forwarded result.
	output logic                 forward_nzp_write_enable, // Enables comparison-result forwarding.
	output logic [          2:0] forward_nzp_data        , // Supplies the forwarded comparison result.
	output logic                 ready_out // Indicates that EX can accept data.
);
	logic [DATA_BITS-1:0] execute_result_d;
	logic [          2:0] nzp_result_d    ;
	logic [          3:0] rd_addr_out_d   ;
	logic                 nzp_write_out_d ;
	logic                 reg_write_out_d ;
	logic                 mem_read_out_d  ;
	logic                 mem_write_out_d ;
	logic [DATA_BITS-1:0] store_data_out_d;
	logic                 is_ret_out_d    ;
	logic                 valid_out_d     ;

	always_comb begin
		// Hold the previous EX/MEM payload. valid_out marks whether it is valid.
		execute_result_d = execute_result;
		nzp_result_d     = nzp_result;
		rd_addr_out_d    = rd_addr_out;
		nzp_write_out_d  = nzp_write_out;
		reg_write_out_d  = reg_write_out;
		mem_read_out_d   = mem_read_out;
		mem_write_out_d  = mem_write_out;
		store_data_out_d = store_data_out;
		is_ret_out_d     = is_ret_out;
		valid_out_d      = valid_out;
		// EX has no additional wait condition, so backpressure from MEM is
		// propagated directly to ID in the same cycle.
		ready_out        = ready_in;

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
					2'b00   : execute_result_d = rs_data + rt_data;
					2'b01   : execute_result_d = rs_data - rt_data;
					2'b10   : execute_result_d = rs_data * rt_data;
					2'b11   : execute_result_d = (rt_data == '0) ? '0 : rs_data / rt_data;
					default : execute_result_d = '0;
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
