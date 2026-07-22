`default_nettype none
`timescale 1ns/1ns
module if_stage #(
	parameter PROGRAM_MEM_ADDR_BITS = 8 ,
	parameter PROGRAM_MEM_DATA_BITS = 16
) (
	input  logic                             clk             ,
	input  logic                             reset           ,
	input  logic                             start           ,
	// ID/IFID 是否可以接收
	input  logic                             ready           ,
	// ID确认跳转时，IF等待Core更新PC，本周期不再取指。
	input  logic                             pc_write_enable ,
	input  logic [PROGRAM_MEM_ADDR_BITS-1:0] current_pc      ,
	// Program memory
	output logic                             mem_read_valid  ,
	output logic [PROGRAM_MEM_ADDR_BITS-1:0] mem_read_address,
	input  logic                             mem_read_ready  ,
	input  logic [PROGRAM_MEM_DATA_BITS-1:0] mem_read_data   ,
	// 输出到 IF/ID
	output logic [PROGRAM_MEM_DATA_BITS-1:0] instruction     ,
	output logic                             valid           ,
	output logic [PROGRAM_MEM_ADDR_BITS-1:0] next_pc
);
	localparam PC_ADD = PROGRAM_MEM_ADDR_BITS'(PROGRAM_MEM_DATA_BITS/8);

	typedef enum logic {
		IDLE = 1'b0,
		WAIT = 1'b1
	} if_state_t;

	logic [PROGRAM_MEM_DATA_BITS-1:0] instruction_d;
	logic                             valid_d      ;
	if_state_t                        state_p      ;
	if_state_t                        state_d      ;
	logic [PROGRAM_MEM_ADDR_BITS-1:0] request_address_p;
	logic [PROGRAM_MEM_ADDR_BITS-1:0] request_address_d;

	always_comb begin
		instruction_d     = instruction;
		valid_d           = valid;
		state_d           = state_p;
		request_address_d = request_address_p;

		// 默认无请求
		mem_read_valid   = 1'b0;
		mem_read_address = '0;
		next_pc         = current_pc;

		if (pc_write_enable) begin
			// ID中的分支指令仍然有效；这里只丢弃错误路径上的后一条取指。
			// IF等待一个周期，下一次从Core更新后的PC继续取指。
			valid_d = 1'b0;
			state_d = IDLE;
		end else case (state_p)
			IDLE: begin
				if (start && ready) begin
					mem_read_valid   = 1'b1;
					mem_read_address = current_pc;
					valid_d          = 1'b0;

					if (mem_read_ready) begin
						instruction_d = mem_read_data;
						valid_d       = 1'b1;
						next_pc       = current_pc + PC_ADD;
					end else begin
						request_address_d = current_pc;
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
			valid            <= '0;
			state_p          <= IDLE;
			request_address_p <= '0;

		end else begin
			instruction      <= instruction_d;
			valid            <= valid_d;
			state_p          <= state_d;
			request_address_p <= request_address_d;
		end
	end
endmodule
