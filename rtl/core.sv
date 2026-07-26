`default_nettype none
`timescale 1ns/1ns

// COMPUTE CORE
// > Handles processing 1 block at a time
// > The core also has it's own scheduler to manage control flow
// > Each core contains 1 fetcher & decoder, and logicister files, ALUs, LSUs, PC for each thread
module core #(
	parameter DATA_MEM_ADDR_BITS    = 8 ,
	parameter DATA_MEM_DATA_BITS    = 8 ,
	parameter PROGRAM_MEM_ADDR_BITS = 8 ,
	parameter PROGRAM_MEM_DATA_BITS = 16,
	parameter THREADS_PER_BLOCK     = 4
) (
	input  wire clk                                           ,
	input  wire reset                                         ,
	// Kernel Execution
	input  wire start                                         ,
	output logic                               done                                          ,
	// Block Metadata
	input  wire [                        7:0] block_id                                      ,
	input  wire [$clog2(THREADS_PER_BLOCK):0] thread_count                                  ,
	// Program Memory
	output logic                               program_mem_read_valid                        ,
	output logic [  PROGRAM_MEM_ADDR_BITS-1:0] program_mem_read_address                      ,
	input  wire program_mem_read_ready                        ,
	input  wire [  PROGRAM_MEM_DATA_BITS-1:0] program_mem_read_data                         ,
	// Data Memory
	output logic [      THREADS_PER_BLOCK-1:0] data_mem_read_valid                           ,
	output logic [     DATA_MEM_ADDR_BITS-1:0] data_mem_read_address [THREADS_PER_BLOCK-1:0] ,
	input  wire [      THREADS_PER_BLOCK-1:0] data_mem_read_ready                           ,
	input  wire [     DATA_MEM_DATA_BITS-1:0] data_mem_read_data [THREADS_PER_BLOCK-1:0]    ,
	output logic [      THREADS_PER_BLOCK-1:0] data_mem_write_valid                          ,
	output logic [     DATA_MEM_ADDR_BITS-1:0] data_mem_write_address [THREADS_PER_BLOCK-1:0],
	output logic [     DATA_MEM_DATA_BITS-1:0] data_mem_write_data [THREADS_PER_BLOCK-1:0]   ,
	input  wire [      THREADS_PER_BLOCK-1:0] data_mem_write_ready
);
	logic [     THREADS_PER_BLOCK-1:0] mask; // 1表示对应线程正在运行
	logic [     THREADS_PER_BLOCK-1:0] mask_d;
	logic [PROGRAM_MEM_ADDR_BITS-1:0] pc  ; // 当前共享程序计数器
	logic [PROGRAM_MEM_ADDR_BITS-1:0] pc_d; // 下一周期程序计数器
	logic [     THREADS_PER_BLOCK-1:0] id_ready_vector; // 每一位为对应ID段的ready_out
	logic [     THREADS_PER_BLOCK-1:0] pc_write_enable_vector; // 每一位为对应ID段的实际跳转请求
	logic [     THREADS_PER_BLOCK-1:0] branch_unconditional_vector; // 每一位为对应ID段的无条件跳转
	logic [PROGRAM_MEM_ADDR_BITS-1:0] branch_offset_vector [THREADS_PER_BLOCK-1:0]; // 各线程分支地址
	branch_stack_pkg::stack_operation_t change_branch_enable_vector [THREADS_PER_BLOCK-1:0]; // 每一位为对应ID段的栈操作
	logic [PROGRAM_MEM_ADDR_BITS-1:0] change_branch_pc_vector [THREADS_PER_BLOCK-1:0]; // 各ID段请求切换的PC
	logic [     THREADS_PER_BLOCK-1:0] change_branch_mask_vector [THREADS_PER_BLOCK-1:0]; // 各ID段请求切换的mask
	logic [     THREADS_PER_BLOCK-1:0] ret_vector; // 每一位为对应线程的RET完成信号
	logic                             if_ready      ; // 所有ID段ready_out的归约或，通知IF是否继续
	logic                             pc_write_enable; // 任一ID段确定分支成立
	logic                             conditional_pc_write_enable; // 任一ID段确定条件分支成立
	logic                             change_branch_request; // 任一ID段请求操作分支栈
	branch_stack_pkg::stack_operation_t change_branch_operation; // 聚合后的ID分支栈操作
	logic [PROGRAM_MEM_ADDR_BITS-1:0] change_branch_pc; // 聚合后的ID分支PC
	logic [     THREADS_PER_BLOCK-1:0] change_branch_mask; // 聚合后的ID分支mask
	logic                             running      ; // Core正在执行当前任务
	logic                             running_d    ;
	logic                             start_q      ; // 用于检测start上升沿
	logic [PROGRAM_MEM_ADDR_BITS-1:0] branch_address_one; // 第一组跳转地址
	logic [PROGRAM_MEM_ADDR_BITS-1:0] branch_address_two; // 第二组跳转地址
	logic [     THREADS_PER_BLOCK-1:0] branch_mask_one; // 第一组跳转线程mask
	logic [     THREADS_PER_BLOCK-1:0] branch_mask_two; // 第二组跳转线程mask
	logic [PROGRAM_MEM_ADDR_BITS-1:0] if_next_pc   ; // IF计算的保持或顺序下一地址
	logic [PROGRAM_MEM_ADDR_BITS-1:0] if_current_pc; // 旁路后的IF取指PC
	logic [     THREADS_PER_BLOCK-1:0] if_current_mask; // 旁路后的IF取指mask
	logic [PROGRAM_MEM_DATA_BITS-1:0] if_instruction; // IF输出指令
	logic [PROGRAM_MEM_ADDR_BITS-1:0] if_instruction_pc; // IF输出指令对应PC
	logic [     THREADS_PER_BLOCK-1:0] if_instruction_mask; // IF输出指令对应mask
	logic                             if_instruction_flag; // IF指令epoch，每条新指令翻转
	logic                             if_valid      ; // IF输出指令有效
	branch_stack_pkg::stack_operation_t branch_stack_operation; // 给分支栈的操作
	logic [PROGRAM_MEM_ADDR_BITS-1:0] branch_stack_pc; // 写入分支栈的PC
	logic [     THREADS_PER_BLOCK-1:0] branch_stack_mask; // 写入分支栈的mask
	logic [PROGRAM_MEM_ADDR_BITS-1:0] branch_top_pc; // 分支栈顶PC
	logic [     THREADS_PER_BLOCK-1:0] branch_top_mask; // 分支栈顶mask
	logic                             branch_empty; // 分支栈是否为空

	branch_stack #(
		.PC_BITS        (PROGRAM_MEM_ADDR_BITS),
		.MASK_BITS      (THREADS_PER_BLOCK),
		.STACK_CAPACITY (4),
		.INITIAL_TOP    (1)
	) branch_stack_example_instance (
		.clk      (clk),
		.reset    (reset),
		.operation(branch_stack_operation),
		.pc_in    (branch_stack_pc),
		.mask_in  (branch_stack_mask),
		.pc_out   (branch_top_pc),
		.mask_out (branch_top_mask),
		.empty    (branch_empty)
	);

	always_comb begin
		pc_write_enable = |(pc_write_enable_vector & mask);
		conditional_pc_write_enable =
			|(pc_write_enable_vector & mask & ~branch_unconditional_vector);
		// 跳转成立时通过总ready阻止IF在旧PC上发起顺序取指。
		if_ready        = &(id_ready_vector | ~mask) && !pc_write_enable;
		mask_d          = mask;
		running_d       = running;
		done            = 1'b0;
		branch_address_one = '0;
		branch_address_two = '0;
		branch_mask_one    = '0;
		branch_mask_two    = '0;
		branch_stack_operation = branch_stack_pkg::STACK_IDLE;
		branch_stack_pc        = '0;
		branch_stack_mask      = '0;
		change_branch_request  = 1'b0;
		change_branch_operation = branch_stack_pkg::STACK_IDLE;
		change_branch_pc       = '0;
		change_branch_mask     = '0;
		begin
			logic branch_address_one_found;
			logic branch_address_two_found;
			branch_address_one_found = 1'b0;
			branch_address_two_found = 1'b0;

			for (int unsigned index = 0; index < THREADS_PER_BLOCK; index = index + 1) begin
				if (mask[index] && pc_write_enable_vector[index]) begin
					if (!branch_address_one_found) begin
						branch_address_one        = branch_offset_vector[index];
						branch_mask_one[index]    = 1'b1;
						branch_address_one_found  = 1'b1;
					end else if (branch_offset_vector[index] == branch_address_one) begin
						branch_mask_one[index] = 1'b1;
					end else if (!branch_address_two_found) begin
						branch_address_two        = branch_offset_vector[index];
						branch_mask_two[index]    = 1'b1;
						branch_address_two_found  = 1'b1;
					end else if (branch_offset_vector[index] == branch_address_two) begin
						branch_mask_two[index] = 1'b1;
					end
				end else if (mask[index] && conditional_pc_write_enable) begin
					// 活动但未跳转的线程沿顺序路径执行，作为第二组上下文压栈。
					branch_address_two       = if_instruction_pc +
						PROGRAM_MEM_ADDR_BITS'(PROGRAM_MEM_DATA_BITS / 8);
					branch_mask_two[index]   = 1'b1;
					branch_address_two_found = 1'b1;
				end
			end

			if (branch_address_two_found) begin
				branch_stack_operation = branch_stack_pkg::STACK_PUSH;
				branch_stack_pc   = branch_address_two;
				branch_stack_mask = branch_mask_two;
			end

			for (int unsigned index = 0; index < THREADS_PER_BLOCK; index = index + 1) begin
				if (!change_branch_request && mask[index] &&
				    (change_branch_enable_vector[index] != branch_stack_pkg::STACK_IDLE)) begin
					change_branch_request   = 1'b1;
					change_branch_operation = change_branch_enable_vector[index];
					change_branch_pc        = change_branch_pc_vector[index];
					change_branch_mask      = change_branch_mask_vector[index];
				end
			end

			if (change_branch_request) begin
				branch_stack_operation = change_branch_operation;
				branch_stack_pc        = change_branch_pc;
				branch_stack_mask      = change_branch_mask;
			end
		end

		// start上升沿按照thread_count激活低编号线程。
		if (start && !start_q) begin
			for (int unsigned index = 0; index < THREADS_PER_BLOCK; index = index + 1)
				mask_d[index] = (index < thread_count);
			running_d = (thread_count != '0);
			done      = (thread_count == '0);
		end else if (running) begin
			if (change_branch_request &&
			    (change_branch_operation == branch_stack_pkg::STACK_POP)) begin
				mask_d = mask | branch_top_mask;
			end else if (change_branch_request &&
			             (change_branch_operation == branch_stack_pkg::STACK_POP_PUSH)) begin
				// 将当前路径写回栈，同时切换到操作前的栈顶路径。
				mask_d = branch_top_mask;
			end else if (conditional_pc_write_enable) begin
				mask_d = branch_mask_one;
			end else begin
				// 对应线程执行RET后清除其mask位。
				mask_d = mask & ~ret_vector;
				if (!(|mask_d)) begin
					running_d = 1'b0;
					done      = 1'b1;
				end
			end
		end
	end

	always_comb begin
		if_current_pc   = pc;
		if_current_mask = mask;

		if (running && change_branch_request &&
		    (change_branch_operation == branch_stack_pkg::STACK_POP)) begin
			if_current_mask = mask | branch_top_mask;
		end else if (running && change_branch_request &&
		             (change_branch_operation == branch_stack_pkg::STACK_POP_PUSH)) begin
			if_current_pc   = branch_top_pc;
			if_current_mask = branch_top_mask;
		end else if (running && conditional_pc_write_enable) begin
			if_current_pc   = branch_address_one;
			if_current_mask = branch_mask_one;
		end
	end

	// IF负责顺序PC，Core负责在分支成立时选择分支地址。
	always_comb begin
		pc_d = if_next_pc;
		if (running && pc_write_enable)
			pc_d = branch_address_one;
	end

	always_ff @(posedge clk) begin
		if (reset) begin
			pc      <= '0;
			mask    <= '0;
			running <= 1'b0;
			start_q <= 1'b0;
		end else begin
			if (start && !start_q)
				pc <= '0;
			else
				pc <= pc_d;
			mask    <= mask_d;
			running <= running_d;
			start_q <= start;
		end
	end

	if_stage #(
		.PROGRAM_MEM_ADDR_BITS(PROGRAM_MEM_ADDR_BITS),
		.PROGRAM_MEM_DATA_BITS(PROGRAM_MEM_DATA_BITS),
		.MASK_BITS            (THREADS_PER_BLOCK)
	) if_stage_instance (
		.clk             (clk),
		.reset           (reset),
		.start           (running),
		.ready           (if_ready),
		.current_pc      (if_current_pc),
		.current_mask    (if_current_mask),
		.mem_read_valid  (program_mem_read_valid),
		.mem_read_address(program_mem_read_address),
		.mem_read_ready  (program_mem_read_ready),
		.mem_read_data   (program_mem_read_data),
		.instruction     (if_instruction),
		.instruction_pc  (if_instruction_pc),
		.instruction_mask(if_instruction_mask),
		.instruction_flag(if_instruction_flag),
		.valid           (if_valid),
		.next_pc         (if_next_pc)
	);

	genvar thread_index;
	generate
		for (thread_index = 0; thread_index < THREADS_PER_BLOCK; thread_index++) begin : gen_threads
			wire thread_running = running && mask[thread_index];

			// Register-file to ID wires.
			wire [DATA_MEM_DATA_BITS-1:0]   rf_rs_data;
			wire [DATA_MEM_DATA_BITS-1:0]   rf_rt_data;
			wire [                     2:0] rf_nzp_data;

			// ID to EX wires.
			wire [                     3:0] id_rd_addr;
			wire [DATA_MEM_DATA_BITS-1:0]   id_rs_data;
			wire [DATA_MEM_DATA_BITS-1:0]   id_rt_data;
			wire [DATA_MEM_DATA_BITS-1:0]   id_imm;
			wire [                     1:0] id_alu_op;
			wire                            id_alu_src;
			wire                            id_alu_enable;
			wire                            id_nzp_write;
			wire                            id_reg_write;
			wire                            id_mem_read;
			wire                            id_mem_write;
			wire [DATA_MEM_DATA_BITS-1:0]   id_store_data;
			wire                            id_is_ret;
			wire                            id_valid;

			// EX to MEM wires.
			wire [DATA_MEM_DATA_BITS-1:0]   ex_execute_result;
			wire [                     2:0] ex_nzp_result;
			wire [                     3:0] ex_rd_addr;
			wire                            ex_nzp_write;
			wire                            ex_reg_write;
			wire                            ex_mem_read;
			wire                            ex_mem_write;
			wire [DATA_MEM_DATA_BITS-1:0]   ex_store_data;
			wire                            ex_is_ret;
			wire                            ex_valid;
			wire                            ex_ready_out;
			wire                            ex_forward_reg_write_enable;
			wire [                     3:0] ex_forward_reg_write_addr;
			wire [DATA_MEM_DATA_BITS-1:0]   ex_forward_data;
			wire                            ex_forward_nzp_write_enable;
			wire [                     2:0] ex_forward_nzp_data;

			// MEM to WB wires.
			wire [DATA_MEM_DATA_BITS-1:0]   mem_execute_result;
			wire [                     2:0] mem_nzp_result;
			wire [                     3:0] mem_rd_addr;
			wire                            mem_nzp_write;
			wire                            mem_reg_write;
			wire                            mem_is_ret;
			wire                            mem_valid;
			wire                            mem_ready_out;
			wire                            mem_forward_valid;
			wire                            mem_forward_from_memory;
			wire                            mem_forward_ready;
			wire [DATA_MEM_DATA_BITS-1:0]   mem_forward_data;
			wire [                     3:0] mem_forward_rd_addr;
			wire                            mem_forward_reg_write;
			wire                            mem_forward_nzp_write;
			wire [                     2:0] mem_forward_nzp_data;

			// WB to register-file wires.
			wire                            wb_write_enable;
			wire [                     3:0] wb_write_addr;
			wire [DATA_MEM_DATA_BITS-1:0]   wb_write_data;
			wire                            wb_nzp_write_enable;
			wire [                     2:0] wb_nzp_write_data;

			register_file #(
				.DATA_BITS(DATA_MEM_DATA_BITS)
			) register_file_instance (
				.clk             (clk),
				.reset           (reset),
				.block_id        (DATA_MEM_DATA_BITS'(block_id)),
				.block_dim       (DATA_MEM_DATA_BITS'(THREADS_PER_BLOCK)),
				.thread_id       (DATA_MEM_DATA_BITS'(thread_index)),
				.rs_addr         (if_instruction[7:4]),
				.rs_data         (rf_rs_data),
				.rt_addr         (if_instruction[3:0]),
				.rt_data         (rf_rt_data),
				.write_enable    (wb_write_enable),
				.write_addr      (wb_write_addr),
				.write_data      (wb_write_data),
				.nzp_read_data   (rf_nzp_data),
				.nzp_write_enable(wb_nzp_write_enable),
				.nzp_write_data  (wb_nzp_write_data)
			);

			id_stage #(
				.PROGRAM_MEM_ADDR_BITS(PROGRAM_MEM_ADDR_BITS),
				.PROGRAM_MEM_DATA_BITS(PROGRAM_MEM_DATA_BITS),
				.DATA_BITS            (DATA_MEM_DATA_BITS),
				.PC_BITS              (PROGRAM_MEM_ADDR_BITS),
				.MASK_BITS            (THREADS_PER_BLOCK)
			) id_stage_instance (
				.start               (thread_running),
				.clk                 (clk),
				.reset               (reset),
				.valid_in            (if_valid),
				.instruction         (if_instruction),
				.instruction_pc      (if_instruction_pc),
				.instruction_mask    (if_instruction_mask),
				.instruction_flag    (if_instruction_flag),
				.rf_rs_data          (rf_rs_data),
				.rf_rt_data          (rf_rt_data),
				.rf_nzp_data         (rf_nzp_data),
				.ex_reg_write_enable (ex_forward_reg_write_enable),
				.ex_reg_write_addr   (ex_forward_reg_write_addr),
				.ex_reg_write_data   (ex_forward_data),
				.ex_nzp_write_enable (ex_forward_nzp_write_enable),
				.ex_nzp_write_data   (ex_forward_nzp_data),
				.mem_reg_write_enable(mem_forward_valid && mem_forward_reg_write),
				.mem_reg_write_addr  (mem_forward_rd_addr),
				.mem_reg_write_data  (mem_forward_data),
				.mem_forward_valid   (mem_forward_valid),
				.mem_forward_from_memory(mem_forward_from_memory),
				.mem_forward_ready   (mem_forward_ready),
				.mem_nzp_write_enable(mem_forward_valid && mem_forward_nzp_write),
				.mem_nzp_write_data  (mem_forward_nzp_data),
				.branch_top_pc       (branch_top_pc),
				.branch_top_mask     (branch_top_mask),
				.branch_empty        (branch_empty),
				.rd_addr             (id_rd_addr),
				.rs_data             (id_rs_data),
				.rt_data             (id_rt_data),
				.imm                 (id_imm),
				.alu_op              (id_alu_op),
				.alu_src             (id_alu_src),
				.alu_enable          (id_alu_enable),
				.nzp_write           (id_nzp_write),
				.reg_write           (id_reg_write),
				.mem_read            (id_mem_read),
				.mem_write           (id_mem_write),
				.store_data          (id_store_data),
				.pc_write_enable     (pc_write_enable_vector[thread_index]),
				.branch_unconditional(branch_unconditional_vector[thread_index]),
				.branch_offset       (branch_offset_vector[thread_index]),
				.is_ret              (id_is_ret),
				.valid               (id_valid),
				.ready_out           (id_ready_vector[thread_index]),
				.change_branch_enable(change_branch_enable_vector[thread_index]),
				.change_branch_pc    (change_branch_pc_vector[thread_index]),
				.change_branch_mask  (change_branch_mask_vector[thread_index]),
				.ready_in            (ex_ready_out)
			);

			ex_stage #(
				.DATA_BITS            (DATA_MEM_DATA_BITS)
			) ex_stage_instance (
				.clk              (clk),
				.reset            (reset),
				.start            (thread_running),
				.rd_addr          (id_rd_addr),
				.rs_data          (id_rs_data),
				.rt_data          (id_rt_data),
				.imm              (id_imm),
				.alu_op           (id_alu_op),
				.alu_src          (id_alu_src),
				.alu_enable       (id_alu_enable),
				.nzp_write        (id_nzp_write),
				.reg_write        (id_reg_write),
				.mem_read         (id_mem_read),
				.mem_write        (id_mem_write),
				.store_data       (id_store_data),
				.is_ret           (id_is_ret),
				.valid            (id_valid),
				.ready_in         (mem_ready_out),
				.execute_result   (ex_execute_result),
				.nzp_result       (ex_nzp_result),
				.rd_addr_out      (ex_rd_addr),
				.nzp_write_out    (ex_nzp_write),
				.reg_write_out    (ex_reg_write),
				.mem_read_out     (ex_mem_read),
				.mem_write_out    (ex_mem_write),
				.store_data_out   (ex_store_data),
				.is_ret_out       (ex_is_ret),
				.valid_out        (ex_valid),
				.forward_reg_write_enable(ex_forward_reg_write_enable),
				.forward_reg_write_addr(ex_forward_reg_write_addr),
				.forward_data     (ex_forward_data),
				.forward_nzp_write_enable(ex_forward_nzp_write_enable),
				.forward_nzp_data (ex_forward_nzp_data),
				.ready_out        (ex_ready_out)
			);

			mem_stage #(				
				.DATA_BITS            (DATA_MEM_DATA_BITS),
				.DATA_MEM_ADDR_BITS   (DATA_MEM_ADDR_BITS),
				.DATA_MEM_DATA_BITS   (DATA_MEM_DATA_BITS)
			) mem_stage_instance (
				.clk               (clk),
				.reset             (reset),
				.start             (thread_running),
				.execute_result    (ex_execute_result),
				.nzp_result        (ex_nzp_result),
				.rd_addr           (ex_rd_addr),
				.nzp_write         (ex_nzp_write),
				.reg_write         (ex_reg_write),
				.mem_read          (ex_mem_read),
				.mem_write         (ex_mem_write),
				.store_data        (ex_store_data),
				.is_ret            (ex_is_ret),
				.valid             (ex_valid),
				.ready_in          (1'b1),
				.mem_read_valid    (data_mem_read_valid[thread_index]),
				.mem_read_address  (data_mem_read_address[thread_index]),
				.mem_read_ready    (data_mem_read_ready[thread_index]),
				.mem_read_data     (data_mem_read_data[thread_index]),
				.mem_write_valid   (data_mem_write_valid[thread_index]),
				.mem_write_address (data_mem_write_address[thread_index]),
				.mem_write_data    (data_mem_write_data[thread_index]),
				.mem_write_ready   (data_mem_write_ready[thread_index]),
				.forward_valid     (mem_forward_valid),
				.forward_from_memory(mem_forward_from_memory),
				.forward_ready     (mem_forward_ready),
				.forward_data      (mem_forward_data),
				.forward_rd_addr   (mem_forward_rd_addr),
				.forward_reg_write (mem_forward_reg_write),
				.forward_nzp_write (mem_forward_nzp_write),
				.forward_nzp_data  (mem_forward_nzp_data),
				.execute_result_out(mem_execute_result),
				.nzp_result_out    (mem_nzp_result),
				.rd_addr_out       (mem_rd_addr),
				.nzp_write_out     (mem_nzp_write),
				.reg_write_out     (mem_reg_write),
				.is_ret_out        (mem_is_ret),
				.valid_out         (mem_valid),
				.ready_out         (mem_ready_out)
			);

			wb_stage #(
				.DATA_BITS(DATA_MEM_DATA_BITS)
			) wb_stage_instance (
				.execute_result  (mem_execute_result),
				.nzp_result      (mem_nzp_result),
				.rd_addr         (mem_rd_addr),
				.nzp_write       (mem_nzp_write),
				.reg_write       (mem_reg_write),
				.is_ret          (mem_is_ret),
				.valid           (mem_valid),
				.write_enable    (wb_write_enable),
				.write_addr      (wb_write_addr),
				.write_data      (wb_write_data),
				.nzp_write_enable(wb_nzp_write_enable),
				.nzp_write_data  (wb_nzp_write_data),
				.done            (ret_vector[thread_index])
			);
		end

	endgenerate

endmodule
