`default_nettype none
`timescale 1ns/1ns

module mem_stage #(
	parameter int unsigned DATA_BITS          = 8,
	parameter int unsigned DATA_MEM_ADDR_BITS = 8,
	parameter int unsigned DATA_MEM_DATA_BITS = DATA_BITS
) (
	input  wire clk              , // System clock.
	input  wire reset            , // Resets the memory stage.
	input  wire start            , // Enables memory handling for this thread.
	input  wire [           DATA_BITS-1:0] execute_result   , // Supplies the EX result or memory address.
	input  wire [                     2:0] nzp_result       , // Supplies the EX comparison result.
	input  wire [                     3:0] rd_addr          , // Supplies the destination register.
	input  wire nzp_write        , // Enables comparison-state writeback.
	input  wire reg_write        , // Enables register writeback.
	input  wire mem_read         , // Marks a load operation.
	input  wire mem_write        , // Marks a store operation.
	input  wire [           DATA_BITS-1:0] store_data       , // Supplies store data.
	input  wire is_ret           , // Marks a return instruction.
	input  wire valid            , // Marks the EX payload as valid.
	input  wire ready_in         , // Indicates that WB can accept data.

	output logic                            mem_read_valid   , // Requests a data-memory read.
	output logic [  DATA_MEM_ADDR_BITS-1:0] mem_read_address , // Selects the data-memory read address.
	input  wire mem_read_ready   , // Acknowledges the data-memory read.
	input  wire [  DATA_MEM_DATA_BITS-1:0] mem_read_data    , // Returns data from memory.

	output logic                            mem_write_valid  , // Requests a data-memory write.
	output logic [  DATA_MEM_ADDR_BITS-1:0] mem_write_address, // Selects the data-memory write address.
	output logic [  DATA_MEM_DATA_BITS-1:0] mem_write_data   , // Supplies data for the memory write.
	input  wire mem_write_ready  , // Acknowledges the data-memory write.

	// Combinational forwarding information sent toward ID.
	output logic                            forward_valid      , // Marks forwarding information as valid.
	output logic                            forward_from_memory, // Selects memory data instead of the EX result.
	output logic                            forward_ready      , // Indicates that forwarded data is ready.
	output logic [           DATA_BITS-1:0] forward_data       , // Supplies the forwarded value.
	output logic [                     3:0] forward_rd_addr    , // Identifies the forwarded register.
	output logic                            forward_reg_write  , // Enables register-result forwarding.
	output logic                            forward_nzp_write  , // Enables comparison-result forwarding.
	output logic [                     2:0] forward_nzp_data   , // Supplies the forwarded comparison result.

	// Registered MEM/WB payload.
	output logic [           DATA_BITS-1:0] execute_result_out, // Sends the writeback value to WB.
	output logic [                     2:0] nzp_result_out    , // Sends the comparison result to WB.
	output logic [                     3:0] rd_addr_out       , // Sends the destination register to WB.
	output logic                            nzp_write_out     , // Sends the comparison write control to WB.
	output logic                            reg_write_out     , // Sends the register write control to WB.
	output logic                            is_ret_out        , // Sends the return flag to WB.
	output logic                            valid_out         , // Marks the MEM output as valid.
	output logic                            ready_out           // Indicates that MEM can accept data.
);
	typedef enum logic {
		IDLE = 1'b0,
		WAIT = 1'b1
	} mem_state_t;

	mem_state_t state_p;
	mem_state_t state_d;

	logic [DATA_BITS-1:0] execute_result_out_d;
	logic [          2:0] nzp_result_out_d;
	logic [          3:0] rd_addr_out_d;
	logic                 nzp_write_out_d;
	logic                 reg_write_out_d;
	logic                 is_ret_out_d;
	logic                 valid_out_d;

	// Payload held while a variable-latency memory request is outstanding.
	logic [DATA_BITS-1:0] pending_execute_result_p;
	logic [DATA_BITS-1:0] pending_execute_result_d;
	logic [          2:0] pending_nzp_result_p;
	logic [          2:0] pending_nzp_result_d;
	logic [          3:0] pending_rd_addr_p;
	logic [          3:0] pending_rd_addr_d;
	logic                 pending_nzp_write_p;
	logic                 pending_nzp_write_d;
	logic                 pending_reg_write_p;
	logic                 pending_reg_write_d;
	logic                 pending_mem_read_p;
	logic                 pending_mem_read_d;
	logic                 pending_mem_write_p;
	logic                 pending_mem_write_d;
	logic [DATA_BITS-1:0] pending_store_data_p;
	logic [DATA_BITS-1:0] pending_store_data_d;
	logic                 pending_is_ret_p;
	logic                 pending_is_ret_d;

	always_comb begin
		state_d                 = state_p;
		execute_result_out_d    = execute_result_out;
		nzp_result_out_d        = nzp_result_out;
		rd_addr_out_d           = rd_addr_out;
		nzp_write_out_d         = nzp_write_out;
		reg_write_out_d         = reg_write_out;
		is_ret_out_d            = is_ret_out;
		valid_out_d             = valid_out;

		// MEM can accept an EX entry only while idle and while WB can accept
		// the resulting payload. WAIT is reserved for the pending request.
		ready_out = (state_p == IDLE) && ready_in;

		pending_execute_result_d = pending_execute_result_p;
		pending_nzp_result_d     = pending_nzp_result_p;
		pending_rd_addr_d        = pending_rd_addr_p;
		pending_nzp_write_d      = pending_nzp_write_p;
		pending_reg_write_d      = pending_reg_write_p;
		pending_mem_read_d       = pending_mem_read_p;
		pending_mem_write_d      = pending_mem_write_p;
		pending_store_data_d     = pending_store_data_p;
		pending_is_ret_d         = pending_is_ret_p;

		mem_read_valid    = 1'b0;
		mem_read_address  = '0;
		mem_write_valid   = 1'b0;
		mem_write_address = '0;
		mem_write_data    = '0;

		forward_valid       = 1'b0;
		forward_from_memory = 1'b0;
		forward_ready       = 1'b0;
		forward_data        = '0;
		forward_rd_addr     = '0;
		forward_reg_write   = 1'b0;
		forward_nzp_write   = 1'b0;
		forward_nzp_data    = '0;

		case (state_p)
			IDLE: begin
				if (start && ready_in && valid) begin
					// These outputs describe the entry currently occupying MEM.
					forward_valid       = 1'b1;
					forward_from_memory = mem_read;
					forward_ready       = mem_read ? mem_read_ready :
					                      mem_write ? mem_write_ready : 1'b1;
					forward_data        = mem_read ? DATA_BITS'(mem_read_data) : execute_result;
					forward_rd_addr     = rd_addr;
					forward_reg_write   = reg_write;
					forward_nzp_write   = nzp_write;
					forward_nzp_data    = nzp_result;

					if (mem_read) begin
						mem_read_valid   = 1'b1;
						mem_read_address = DATA_MEM_ADDR_BITS'(execute_result);
					end

					if (mem_write) begin
						mem_write_valid   = 1'b1;
						mem_write_address = DATA_MEM_ADDR_BITS'(execute_result);
						mem_write_data    = DATA_MEM_DATA_BITS'(store_data);
					end

					if ((!mem_read && !mem_write) ||
					    (mem_read && mem_read_ready) ||
					    (mem_write && mem_write_ready)) begin
						execute_result_out_d = mem_read ? DATA_BITS'(mem_read_data) : execute_result;
						nzp_result_out_d     = nzp_result;
						rd_addr_out_d        = rd_addr;
						nzp_write_out_d      = nzp_write;
						reg_write_out_d      = reg_write;
						is_ret_out_d         = is_ret;
						valid_out_d          = 1'b1;
					end else begin
						// Hold the complete request until the corresponding ready arrives.
						pending_execute_result_d = execute_result;
						pending_nzp_result_d     = nzp_result;
						pending_rd_addr_d        = rd_addr;
						pending_nzp_write_d      = nzp_write;
						pending_reg_write_d      = reg_write;
						pending_mem_read_d       = mem_read;
						pending_mem_write_d      = mem_write;
						pending_store_data_d     = store_data;
						pending_is_ret_d         = is_ret;
						valid_out_d              = 1'b0;
						state_d                  = WAIT;
					end
				end else if (ready_in && !valid) begin
					valid_out_d = 1'b0;
				end
			end

			WAIT: begin
				forward_valid       = 1'b1;
				forward_from_memory = pending_mem_read_p;
				forward_ready       = pending_mem_read_p ? mem_read_ready : mem_write_ready;
				forward_data        = pending_mem_read_p ? DATA_BITS'(mem_read_data) : pending_execute_result_p;
				forward_rd_addr     = pending_rd_addr_p;
				forward_reg_write   = pending_reg_write_p;
				forward_nzp_write   = pending_nzp_write_p;
				forward_nzp_data    = pending_nzp_result_p;

				if (pending_mem_read_p) begin
					mem_read_valid   = 1'b1;
					mem_read_address = DATA_MEM_ADDR_BITS'(pending_execute_result_p);
				end

				if (pending_mem_write_p) begin
					mem_write_valid   = 1'b1;
					mem_write_address = DATA_MEM_ADDR_BITS'(pending_execute_result_p);
					mem_write_data    = DATA_MEM_DATA_BITS'(pending_store_data_p);
				end

				if ((pending_mem_read_p && mem_read_ready) ||
				    (pending_mem_write_p && mem_write_ready)) begin
					execute_result_out_d = pending_mem_read_p ? DATA_BITS'(mem_read_data) : pending_execute_result_p;
					nzp_result_out_d     = pending_nzp_result_p;
					rd_addr_out_d        = pending_rd_addr_p;
					nzp_write_out_d      = pending_nzp_write_p;
					reg_write_out_d      = pending_reg_write_p;
					is_ret_out_d         = pending_is_ret_p;
					valid_out_d          = 1'b1;
					state_d              = IDLE;
				end
			end

			default: state_d = IDLE;
		endcase
	end

	always_ff @(posedge clk) begin
		if (reset) begin
			state_p                  <= IDLE;
			execute_result_out       <= '0;
			nzp_result_out           <= '0;
			rd_addr_out              <= '0;
			nzp_write_out            <= 1'b0;
			reg_write_out            <= 1'b0;
			is_ret_out               <= 1'b0;
			valid_out                <= 1'b0;
			pending_execute_result_p <= '0;
			pending_nzp_result_p     <= '0;
			pending_rd_addr_p        <= '0;
			pending_nzp_write_p      <= 1'b0;
			pending_reg_write_p      <= 1'b0;
			pending_mem_read_p       <= 1'b0;
			pending_mem_write_p      <= 1'b0;
			pending_store_data_p     <= '0;
			pending_is_ret_p         <= 1'b0;
		end else begin
			state_p                  <= state_d;
			execute_result_out       <= execute_result_out_d;
			nzp_result_out           <= nzp_result_out_d;
			rd_addr_out              <= rd_addr_out_d;
			nzp_write_out            <= nzp_write_out_d;
			reg_write_out            <= reg_write_out_d;
			is_ret_out               <= is_ret_out_d;
			valid_out                <= valid_out_d;
			pending_execute_result_p <= pending_execute_result_d;
			pending_nzp_result_p     <= pending_nzp_result_d;
			pending_rd_addr_p        <= pending_rd_addr_d;
			pending_nzp_write_p      <= pending_nzp_write_d;
			pending_reg_write_p      <= pending_reg_write_d;
			pending_mem_read_p       <= pending_mem_read_d;
			pending_mem_write_p      <= pending_mem_write_d;
			pending_store_data_p     <= pending_store_data_d;
			pending_is_ret_p         <= pending_is_ret_d;
		end
	end

endmodule

`default_nettype wire
