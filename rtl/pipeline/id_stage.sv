`default_nettype none
`timescale 1ns/1ns

module id_stage #(
	parameter int unsigned PROGRAM_MEM_ADDR_BITS = 8 ,
	parameter int unsigned PROGRAM_MEM_DATA_BITS = 16,
	parameter int unsigned DATA_BITS             = 8
) (
	input  logic                             start               ,
	input  logic                             clk                 ,
	input  logic                             reset               ,
	input  logic                             valid_in            ,
	input  logic [PROGRAM_MEM_DATA_BITS-1:0] instruction         ,
	// Per-thread metadata exposed as read-only R13/R14/R15.
	input  logic [            DATA_BITS-1:0] rf_rs_data          ,
	input  logic [            DATA_BITS-1:0] rf_rt_data          ,
	input  logic [                      2:0] rf_nzp_data         ,
	// Forwarding data from EX stage.
	input  logic                             ex_reg_write_enable ,
	input  logic [                      3:0] ex_reg_write_addr   ,
	input  logic [            DATA_BITS-1:0] ex_reg_write_data   ,
	input  logic                             ex_nzp_write_enable ,
	input  logic [                      2:0] ex_nzp_write_data   ,
	// Forwarding data from MEM stage.
	input  logic                             mem_reg_write_enable,
	input  logic [                      3:0] mem_reg_write_addr  ,
	input  logic [            DATA_BITS-1:0] mem_reg_write_data  ,
	input  logic                             mem_forward_valid   ,
	input  logic                             mem_forward_from_memory,
	input  logic                             mem_forward_ready   ,
	input  logic                             mem_nzp_write_enable,
	input  logic [                      2:0] mem_nzp_write_data  ,
	// Decoded source addresses are also available to the hazard unit.
	output logic [                      3:0] rd_addr             ,
	output logic [            DATA_BITS-1:0] rs_data             ,
	output logic [            DATA_BITS-1:0] rt_data             ,
	output logic [            DATA_BITS-1:0] imm                 ,
	output logic [                      1:0] alu_op              ,
	output logic                             alu_src             ,
	output logic                             alu_enable          ,
	output logic                             nzp_write           ,
	output logic                             reg_write           ,
	output logic                             mem_read            ,
	output logic                             mem_write           ,
	output logic [            DATA_BITS-1:0] store_data          ,
	// A taken branch is resolved in ID and redirects the shared PC immediately.
	output logic                             pc_write_enable     ,
	output logic [PROGRAM_MEM_ADDR_BITS-1:0] branch_offset       ,
	output logic                             is_ret              ,
	output logic                             valid               ,
	output logic                             ready_out            ,
	input  logic                             ready_in
);
	localparam logic [3:0] OP_NOP   = 4'b0000;
	localparam logic [3:0] OP_BRNZP = 4'b0001;
	localparam logic [3:0] OP_CMP   = 4'b0010;
	localparam logic [3:0] OP_ADD   = 4'b0011;
	localparam logic [3:0] OP_SUB   = 4'b0100;
	localparam logic [3:0] OP_MUL   = 4'b0101;
	localparam logic [3:0] OP_DIV   = 4'b0110;
	localparam logic [3:0] OP_LDR   = 4'b0111;
	localparam logic [3:0] OP_STR   = 4'b1000;
	localparam logic [3:0] OP_CONST = 4'b1001;
	localparam logic [3:0] OP_RET   = 4'b1111;

	localparam logic [1:0] ALU_ADD = 2'b00;
	localparam logic [1:0] ALU_SUB = 2'b01;
	localparam logic [1:0] ALU_MUL = 2'b10;
	localparam logic [1:0] ALU_DIV = 2'b11;

	logic [                      3:0] rd_addr_d      ;
	logic [            DATA_BITS-1:0] rs_data_d      ;
	logic [            DATA_BITS-1:0] rt_data_d      ;
	logic [            DATA_BITS-1:0] imm_d          ;
	logic [                      1:0] alu_op_d       ;
	logic                             alu_src_d      ;
	logic                             alu_enable_d   ;
	logic                             nzp_write_d    ;
	logic                             reg_write_d    ;
	logic                             mem_read_d     ;
	logic                             mem_write_d    ;
	logic [            DATA_BITS-1:0] store_data_d   ;
	logic                             is_ret_d       ;
	logic                             valid_d        ;
	logic [            DATA_BITS-1:0] imm_value      ;
	logic                             mem_load_wait  ;
	logic [                      3:0] opcode         ;
	logic [                      3:0] rs_addr        ;
	logic [                      3:0] rt_addr        ;

	function automatic logic [DATA_BITS-1:0] read_register_data(
			input logic [3:0]           address,
			input logic [DATA_BITS-1:0] register_data
		);
		if (ex_reg_write_enable && (ex_reg_write_addr == address))
			read_register_data = ex_reg_write_data;
		else if (mem_reg_write_enable && (mem_reg_write_addr == address))
			read_register_data = mem_reg_write_data;
		else
			read_register_data = register_data;
	endfunction

	function automatic logic [2:0] read_nzp_data(
			input logic [2:0] register_nzp_data
		);
		if (ex_nzp_write_enable)
			read_nzp_data = ex_nzp_write_data;
		else if (mem_nzp_write_enable)
			read_nzp_data = mem_nzp_write_data;
		else
			read_nzp_data = register_nzp_data;
	endfunction

	function automatic logic register_load_wait(
			input logic [3:0] address
		);
		if (mem_reg_write_enable &&
		         mem_forward_valid &&
		         mem_forward_from_memory &&
		         !mem_forward_ready &&
		         (mem_reg_write_addr == address))
			register_load_wait = 1'b1;
		else
			register_load_wait = 1'b0;
	endfunction


	// Instruction decode and register-file data selection are purely combinational.
	always_comb begin
		// Hold the previous ID/EX payload. Its validity is tracked separately.
		opcode  = instruction[15:12];
		rd_addr_d       = rd_addr;
		rs_data_d       = rs_data;
		rt_data_d       = rt_data;
		imm_d           = imm;
		store_data_d    = store_data;
		alu_op_d        = alu_op;
		alu_src_d       = alu_src;
		alu_enable_d    = alu_enable;
		nzp_write_d     = nzp_write;
		reg_write_d     = reg_write;
		mem_read_d      = mem_read;
		mem_write_d     = mem_write;
		is_ret_d        = is_ret;
		valid_d         = valid;
		pc_write_enable = 1'b0;
		branch_offset   = PROGRAM_MEM_ADDR_BITS'(instruction[7:0]);

		// Register-file addresses come directly from the instruction fields.
		rs_addr   = instruction[7:4];
		rt_addr   = instruction[3:0];
		imm_value = DATA_BITS'(instruction[7:0]);
		mem_load_wait = 1'b0;
		if ((opcode == OP_CMP) ||
		    (opcode == OP_ADD) ||
		    (opcode == OP_SUB) ||
		    (opcode == OP_MUL) ||
		    (opcode == OP_DIV) ||
		    (opcode == OP_STR)) begin
			mem_load_wait = register_load_wait(rs_addr) ||
			                register_load_wait(rt_addr);
		end else if (opcode == OP_LDR) begin
			mem_load_wait = register_load_wait(rs_addr);
		end
		// Ready is combinational backpressure toward IF. Registering this
		// signal would delay a stall by one cycle and can deadlock IF/ID.
		ready_out = ready_in && !mem_load_wait;

		if (start && valid_in && ready_in && !mem_load_wait) begin
			// Initialize a fresh entry before applying opcode-specific controls.
			rd_addr_d       = instruction[11:8];
			rs_data_d       = '0;
			rt_data_d       = '0;
			imm_d           = DATA_BITS'(instruction[7:0]);
			store_data_d    = '0;
			alu_op_d        = ALU_ADD;
			alu_src_d       = 1'b0;
			alu_enable_d    = 1'b0;
			nzp_write_d     = 1'b0;
			reg_write_d     = 1'b0;
			mem_read_d      = 1'b0;
			mem_write_d     = 1'b0;
			is_ret_d        = 1'b0;
			valid_d         = 1'b1;
			case (instruction[15:12])
				OP_NOP   : begin
				end
				OP_BRNZP : begin
					pc_write_enable = |(read_nzp_data(rf_nzp_data) &
					                         instruction[11:9]);
				end
				OP_CMP : begin
					alu_op_d     = ALU_SUB;
					alu_enable_d = 1'b1;
					nzp_write_d  = 1'b1;
					rs_data_d    = read_register_data(instruction[7:4], rf_rs_data);
					rt_data_d    = read_register_data(instruction[3:0], rf_rt_data);
				end
				OP_ADD : begin
					rs_data_d    = read_register_data(instruction[7:4], rf_rs_data);
					rt_data_d    = read_register_data(instruction[3:0], rf_rt_data);
					alu_op_d     = ALU_ADD;
					alu_enable_d = 1'b1;
					reg_write_d  = 1'b1;
					rd_addr_d    = instruction[11:8];
				end
				OP_SUB : begin
					rs_data_d    = read_register_data(instruction[7:4], rf_rs_data);
					rt_data_d    = read_register_data(instruction[3:0], rf_rt_data);
					alu_op_d     = ALU_SUB;
					alu_enable_d = 1'b1;
					reg_write_d  = 1'b1;
					rd_addr_d    = instruction[11:8];
				end
				OP_MUL : begin
					rs_data_d    = read_register_data(instruction[7:4], rf_rs_data);
					rt_data_d    = read_register_data(instruction[3:0], rf_rt_data);
					alu_op_d     = ALU_MUL;
					alu_enable_d = 1'b1;
					reg_write_d  = 1'b1;
					rd_addr_d    = instruction[11:8];
				end
				OP_DIV : begin
					rs_data_d    = read_register_data(instruction[7:4], rf_rs_data);
					rt_data_d    = read_register_data(instruction[3:0], rf_rt_data);
					alu_op_d     = ALU_DIV;
					alu_enable_d = 1'b1;
					reg_write_d  = 1'b1;
					rd_addr_d    = instruction[11:8];
				end
				OP_LDR : begin
					rs_data_d    = read_register_data(instruction[7:4], rf_rs_data);
					rd_addr_d    = instruction[11:8];
					mem_read_d   = 1'b1;
					reg_write_d  = 1'b1;
				end
				OP_STR : begin
					rs_data_d    = read_register_data(instruction[7:4], rf_rs_data);
					rt_data_d    = read_register_data(instruction[3:0], rf_rt_data);
					store_data_d = rt_data_d;
					mem_write_d  = 1'b1;
				end
				OP_CONST : begin
					alu_src_d   = 1'b1;
					reg_write_d = 1'b1;
					rd_addr_d   = instruction[11:8];
					imm_d       = imm_value;
				end
				OP_RET : begin
					is_ret_d = 1'b1;
				end
				default : begin
				end
			endcase
		end else if (ready_in && (!valid_in||mem_load_wait)) begin
			valid_d = 1'b0;
		end
	end

	always_ff @(posedge clk) begin
		if (reset) begin
			rd_addr       <= '0;
			rs_data       <= '0;
			rt_data       <= '0;
			imm           <= '0;
			alu_op        <= '0;
			alu_src       <= '0;
			alu_enable    <= '0;
			nzp_write     <= '0;
			reg_write     <= '0;
			mem_read      <= '0;
			mem_write     <= '0;
			store_data    <= '0;
			is_ret        <= '0;
			valid         <= '0;
		end else begin
			rd_addr       <= rd_addr_d;
			rs_data       <= rs_data_d;
			rt_data       <= rt_data_d;
			imm           <= imm_d;
			alu_op        <= alu_op_d;
			alu_src       <= alu_src_d;
			alu_enable    <= alu_enable_d;
			nzp_write     <= nzp_write_d;
			reg_write     <= reg_write_d;
			mem_read      <= mem_read_d;
			mem_write     <= mem_write_d;
			store_data    <= store_data_d;
			is_ret        <= is_ret_d;
			valid         <= valid_d;
		end
	end


endmodule
