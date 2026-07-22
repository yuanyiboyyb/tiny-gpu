`default_nettype none
`timescale 1ns/1ns

// BLOCK DISPATCH
// > The GPU has one dispatch unit at the top level
// > Manages processing of threads and marks kernel execution as done
// > Sends off batches of threads in blocks to be executed by available compute cores
module dispatch #(
	parameter NUM_CORES         = 2,
	parameter THREADS_PER_BLOCK = 4
) (
	input  logic                               clk                              ,
	input  logic                               reset                            ,
	input  logic                               start                            ,
	// Kernel Metadata
	input  logic [                        7:0] thread_count                     ,
	// Core States
	input  logic [              NUM_CORES-1:0] core_done                        ,
	output logic [              NUM_CORES-1:0] core_start                       ,
	output logic [              NUM_CORES-1:0] core_reset                       ,
	output logic [                        7:0] core_block_id [NUM_CORES-1:0]    ,
	output logic [$clog2(THREADS_PER_BLOCK):0] core_thread_count [NUM_CORES-1:0],
	// Kernel Execution
	output logic                               done
);
	typedef enum logic [1:0] {
		IDLE = 2'b00,
		BUSY = 2'b10,
		WAIT = 2'b01
	} dispatch_state_t;
	typedef enum logic {
		IDLE_C = 1'b0,
		BUSY_C = 1'b1
	} core_state_t;

	dispatch_state_t dispatch_state_p;
	dispatch_state_t dispatch_state_d;

	core_state_t core_state_p[NUM_CORES];
	core_state_t core_state_d[NUM_CORES];

	logic [7:0] block_id_p;
	logic [7:0] block_id_d;

	logic [7:0] block_id_n_p;
	logic [7:0] block_id_n_d;

	logic[7:0]temp;

	logic            [              NUM_CORES-1:0] core_start_d                                                    ; ;
	logic            [                        7:0] core_block_id_d    [NUM_CORES-1:0]                              ;
	logic            [$clog2(THREADS_PER_BLOCK):0] core_thread_count_d[NUM_CORES-1:0]                              ;
	localparam int                                 THREAD_COUNT_BITS                  = $clog2(THREADS_PER_BLOCK)+1;

	logic all_cores_idle;
	logic done_d        ;
	always_comb begin
		dispatch_state_d    = dispatch_state_p;
		core_state_d        = core_state_p;
		core_start_d        = core_start;
		core_block_id_d     = core_block_id;
		core_thread_count_d = core_thread_count;
		block_id_d          = block_id_p;
		block_id_n_d        = block_id_n_p;
		all_cores_idle      = 1'b1;
		temp                = block_id_n_p;
		done_d              = done;

		for (int i = 0; i < NUM_CORES; i++) begin
			all_cores_idle &= (core_state_p[i] == IDLE_C);
			core_reset[i] = reset;
		end
		case (dispatch_state_p)
			IDLE : begin
				if(start)begin
					dispatch_state_d = BUSY;
					block_id_d       = (thread_count + THREADS_PER_BLOCK - 1) / THREADS_PER_BLOCK;
					block_id_n_d     = '0;
					core_start_d     = '0;
					for (int i = 0; i < NUM_CORES; i++) begin
						core_state_d[i]        = IDLE_C;
						core_block_id_d[i]     = '0;
						core_thread_count_d[i] = '0;
					end
				end
			end
			BUSY : begin
				if((block_id_n_p == block_id_p) && all_cores_idle)begin
					dispatch_state_d = WAIT;
					done_d           = 1'b1;
				end
				else begin
					temp = block_id_n_p;
					for(int i = 0; i < NUM_CORES;i++)begin
						case (core_state_p[i])
							IDLE_C : begin
								if(temp < block_id_p)begin
									core_block_id_d[i]     = temp;
									core_thread_count_d[i] = THREAD_COUNT_BITS'(temp+1==block_id_p?thread_count-(temp*8'(THREADS_PER_BLOCK)):8'(THREADS_PER_BLOCK));
									core_start_d[i]        = 1'b1;
									temp                   = temp+1'b1;
									core_state_d[i]        = BUSY_C;
								end
							end
							BUSY_C : begin
								if(core_done[i])begin
									core_state_d[i] = IDLE_C;
									core_start_d[i] = 1'b0;
								end
							end
							default : core_state_d[i] = IDLE_C;
						endcase
					end

					block_id_n_d = temp;
				end
			end
			WAIT : begin
				if (!start) begin
					done_d           = 1'b0;
					dispatch_state_d = IDLE;
				end
			end
			default : dispatch_state_d = IDLE;
		endcase
	end
	always_ff @(posedge clk) begin
		if (reset) begin
			dispatch_state_p <= IDLE;
			block_id_p       <= '0;
			block_id_n_p     <= '0;
			core_start       <= '0;
			done             <= '0;
			for (int i = 0; i < NUM_CORES; i++) begin
				core_state_p[i]      <= IDLE_C;
				core_block_id[i]     <= '0;
				core_thread_count[i] <= '0;
			end
		end else begin
			dispatch_state_p <= dispatch_state_d;
			block_id_p       <= block_id_d;
			block_id_n_p     <= block_id_n_d;
			core_start       <= core_start_d;
			done           <= done_d;
			for (int i = 0; i < NUM_CORES; i++) begin
				core_state_p[i]      <= core_state_d[i];
				core_block_id[i]     <= core_block_id_d[i];
				core_thread_count[i] <= core_thread_count_d[i];
			end
		end
	end
endmodule
