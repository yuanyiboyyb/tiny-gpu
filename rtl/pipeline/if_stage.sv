`default_nettype none
`timescale 1ns/1ns
module if_stage #(
	parameter PROGRAM_MEM_ADDR_BITS = 8 ,
	parameter PROGRAM_MEM_DATA_BITS = 16,
	parameter MASK_BITS             = 4
) (
	input  wire clk             , // System clock.
	input  wire reset           , // Resets the fetch stage.
	input  wire start           , // Enables instruction fetching.
	// ID/IFID 是否可以接收
	input  wire ready           , // Indicates that ID can accept an instruction.
	input  wire [PROGRAM_MEM_ADDR_BITS-1:0] current_pc      , // Gives the next fetch address.
	input  wire [             MASK_BITS-1:0] current_mask    , // Gives the active threads for the fetch.
	// Program memory
	output logic                             mem_read_valid  , // Requests an instruction read.
	output logic [PROGRAM_MEM_ADDR_BITS-1:0] mem_read_address, // Selects the instruction address.
	input  wire mem_read_ready  , // Acknowledges the instruction read.
	input  wire [PROGRAM_MEM_DATA_BITS-1:0] mem_read_data   , // Returns the fetched instruction.
	// 输出到 IF/ID
	output logic [PROGRAM_MEM_DATA_BITS-1:0] instruction     , // Sends the fetched instruction to ID.
	output logic [PROGRAM_MEM_ADDR_BITS-1:0] instruction_pc  , // Sends the instruction address to ID.
	output logic [             MASK_BITS-1:0] instruction_mask, // Sends the fetch-time thread mask to ID.
	output logic                             instruction_flag, // Toggles for each accepted instruction.
	output logic                             valid           , // Marks the instruction output as valid.
	output logic [PROGRAM_MEM_ADDR_BITS-1:0] next_pc           // Provides the sequential next address.
);
	localparam PC_ADD = PROGRAM_MEM_ADDR_BITS'(PROGRAM_MEM_DATA_BITS/8);

	typedef enum logic {
		IDLE = 1'b0,
		WAIT = 1'b1
	} if_state_t;

	logic [PROGRAM_MEM_DATA_BITS-1:0] instruction_d;
	logic [PROGRAM_MEM_ADDR_BITS-1:0] instruction_pc_d;
	logic [             MASK_BITS-1:0] instruction_mask_d;
	logic                             instruction_flag_d;
	logic                             valid_d      ;
	if_state_t                        state_p      ;
	if_state_t                        state_d      ;
	logic [PROGRAM_MEM_ADDR_BITS-1:0] request_address_p;
	logic [PROGRAM_MEM_ADDR_BITS-1:0] request_address_d;
	logic [             MASK_BITS-1:0] request_mask_p;
	logic [             MASK_BITS-1:0] request_mask_d;

	always_comb begin
		instruction_d     = instruction;
		instruction_pc_d  = instruction_pc;
		instruction_mask_d = instruction_mask;
		instruction_flag_d = instruction_flag;
		valid_d           = valid;
		state_d           = state_p;
		request_address_d = request_address_p;
		request_mask_d    = request_mask_p;

		// 默认无请求
		mem_read_valid   = 1'b0;
		mem_read_address = '0;
		next_pc         = current_pc;

		case (state_p)
			IDLE: begin
				if (start && ready) begin
					mem_read_valid   = 1'b1;
					mem_read_address = current_pc;
					valid_d          = 1'b0;

					if (mem_read_ready) begin
						instruction_d = mem_read_data;
						instruction_pc_d = current_pc;
						instruction_mask_d = current_mask;
						instruction_flag_d = ~instruction_flag;
						valid_d       = 1'b1;
						next_pc       = current_pc + PC_ADD;
					end else begin
						request_address_d = current_pc;
						request_mask_d    = current_mask;
						state_d           = WAIT;
					end
				end
			end

			WAIT: begin
				// Hold the request until external memory acknowledges it.
				mem_read_valid   = 1'b1;
				mem_read_address = request_address_p;

				if (mem_read_ready) begin
					instruction_d = mem_read_data;
					instruction_pc_d = request_address_p;
					instruction_mask_d = request_mask_p;
					instruction_flag_d = ~instruction_flag;
					valid_d       = 1'b1;
					next_pc       = request_address_p + PC_ADD;
					state_d       = IDLE;
				end
			end
			default: state_d = IDLE;
		endcase
	end

	always_ff @(posedge clk) begin
		if(reset) begin
			instruction      <= '0;
			instruction_pc   <= '0;
			instruction_mask <= '0;
			instruction_flag <= 1'b0;
			valid            <= '0;
			state_p          <= IDLE;
			request_address_p <= '0;
			request_mask_p   <= '0;

		end else begin
			instruction      <= instruction_d;
			instruction_pc   <= instruction_pc_d;
			instruction_mask <= instruction_mask_d;
			instruction_flag <= instruction_flag_d;
			valid            <= valid_d;
			state_p          <= state_d;
			request_address_p <= request_address_d;
			request_mask_p   <= request_mask_d;
		end
	end
endmodule
