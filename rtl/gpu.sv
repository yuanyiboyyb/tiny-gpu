`default_nettype none
`timescale 1ns/1ns

module gpu #(
	parameter int DATA_MEM_ADDR_BITS       = 8 ,
	parameter int DATA_MEM_DATA_BITS       = 8 ,
	parameter int DATA_MEM_NUM_CHANNELS    = 4 ,
	parameter int PROGRAM_MEM_ADDR_BITS    = 8 ,
	parameter int PROGRAM_MEM_DATA_BITS    = 16,
	parameter int PROGRAM_MEM_NUM_CHANNELS = 1 ,
	parameter int NUM_CORES                = 2 ,
	parameter int THREADS_PER_BLOCK        = 4
) (
	input  logic                                clk                                                ,
	input  logic                                reset                                              ,
	// Kernel Execution
	input  logic                                start                                              ,
	output logic                                done                                               ,
	// Device Control Register
	input  logic                                device_control_write_enable                        ,
	input  logic [                         7:0] device_control_data                                ,
	// Program Memory
	output logic [PROGRAM_MEM_NUM_CHANNELS-1:0] program_mem_read_valid                             ,
	output logic [   PROGRAM_MEM_ADDR_BITS-1:0] program_mem_read_address [PROGRAM_MEM_NUM_CHANNELS-1:0],
	input  logic [PROGRAM_MEM_NUM_CHANNELS-1:0] program_mem_read_ready                             ,
	input  logic [   PROGRAM_MEM_DATA_BITS-1:0] program_mem_read_data [PROGRAM_MEM_NUM_CHANNELS-1:0],
	// Data Memory
	output logic [   DATA_MEM_NUM_CHANNELS-1:0] data_mem_read_valid                                ,
	output logic [      DATA_MEM_ADDR_BITS-1:0] data_mem_read_address [DATA_MEM_NUM_CHANNELS-1:0]  ,
	input  logic [   DATA_MEM_NUM_CHANNELS-1:0] data_mem_read_ready                                ,
	input  logic [      DATA_MEM_DATA_BITS-1:0] data_mem_read_data [DATA_MEM_NUM_CHANNELS-1:0]     ,
	output logic [   DATA_MEM_NUM_CHANNELS-1:0] data_mem_write_valid                               ,
	output logic [      DATA_MEM_ADDR_BITS-1:0] data_mem_write_address [DATA_MEM_NUM_CHANNELS-1:0] ,
	output logic [      DATA_MEM_DATA_BITS-1:0] data_mem_write_data [DATA_MEM_NUM_CHANNELS-1:0]    ,
	input  logic [   DATA_MEM_NUM_CHANNELS-1:0] data_mem_write_ready
);
	//dcr
	logic [7:0] thread_count;
	// Compute Core State
	logic [              NUM_CORES-1:0] core_start                      ;
	logic [              NUM_CORES-1:0] core_reset                      ;
	logic [              NUM_CORES-1:0] core_done                       ;
	logic [                        7:0] core_block_id    [NUM_CORES-1:0];
	logic [$clog2(THREADS_PER_BLOCK):0] core_thread_count[NUM_CORES-1:0];
	// LSU <> Data Memory Controller Channels
	localparam                     NUM_LSUS                        = NUM_CORES * THREADS_PER_BLOCK;
	logic [          NUM_LSUS-1:0] lsu_read_valid                                                 ;
	logic [DATA_MEM_ADDR_BITS-1:0] lsu_read_address [NUM_LSUS-1:0]                                ;
	logic [          NUM_LSUS-1:0] lsu_read_ready                                                 ;
	logic [DATA_MEM_DATA_BITS-1:0] lsu_read_data    [NUM_LSUS-1:0]                                ;
	logic [          NUM_LSUS-1:0] lsu_write_valid                                                ;
	logic [DATA_MEM_ADDR_BITS-1:0] lsu_write_address[NUM_LSUS-1:0]                                ;
	logic [DATA_MEM_DATA_BITS-1:0] lsu_write_data   [NUM_LSUS-1:0]                                ;
	logic [          NUM_LSUS-1:0] lsu_write_ready                                                ;
	// Fetcher <> Program Memory Controller Channels
	localparam                      NUM_FETCHERS                           = NUM_CORES;
	reg [         NUM_FETCHERS-1:0] fetcher_read_valid                                ;
	reg [PROGRAM_MEM_ADDR_BITS-1:0] fetcher_read_address[NUM_FETCHERS-1:0]            ;
	reg [         NUM_FETCHERS-1:0] fetcher_read_ready                                ;
	reg [PROGRAM_MEM_DATA_BITS-1:0] fetcher_read_data   [NUM_FETCHERS-1:0]            ;
	// Program memory is read-only; these wires explicitly consume disabled write outputs.
	logic [         NUM_FETCHERS-1:0] unused_fetcher_write_ready;
	logic [PROGRAM_MEM_NUM_CHANNELS-1:0] unused_program_mem_write_valid;
	logic [   PROGRAM_MEM_ADDR_BITS-1:0] unused_program_mem_write_address[PROGRAM_MEM_NUM_CHANNELS-1:0];
	logic [   PROGRAM_MEM_DATA_BITS-1:0] unused_program_mem_write_data[PROGRAM_MEM_NUM_CHANNELS-1:0];

	dcr dcr_instance (
		.clk                        (clk                        ),
		.reset                      (reset                      ),
		.device_control_write_enable(device_control_write_enable),
		.device_control_data        (device_control_data        ),
		.thread_count               (thread_count               )
	);

	memory_controller #(
		.ADDR_BITS    (DATA_MEM_ADDR_BITS   ),
		.DATA_BITS    (DATA_MEM_DATA_BITS   ),
		.NUM_CONSUMERS(NUM_LSUS             ),
		.NUM_CHANNELS (DATA_MEM_NUM_CHANNELS)
	) data_memory_controller (
		.clk                   (clk                   ),
		.reset                 (reset                 ),
		.consumer_read_valid   (lsu_read_valid        ),
		.consumer_read_address (lsu_read_address      ),
		.consumer_read_ready   (lsu_read_ready        ),
		.consumer_read_data    (lsu_read_data         ),
		.consumer_write_valid  (lsu_write_valid       ),
		.consumer_write_address(lsu_write_address     ),
		.consumer_write_data   (lsu_write_data        ),
		.consumer_write_ready  (lsu_write_ready       ),
		.mem_read_valid        (data_mem_read_valid   ),
		.mem_read_address      (data_mem_read_address ),
		.mem_read_ready        (data_mem_read_ready   ),
		.mem_read_data         (data_mem_read_data    ),
		.mem_write_valid       (data_mem_write_valid  ),
		.mem_write_address     (data_mem_write_address),
		.mem_write_data        (data_mem_write_data   ),
		.mem_write_ready       (data_mem_write_ready  )
	);

	memory_controller #(
		.ADDR_BITS    (PROGRAM_MEM_ADDR_BITS   ),
		.DATA_BITS    (PROGRAM_MEM_DATA_BITS   ),
		.NUM_CONSUMERS(NUM_FETCHERS            ),
		.NUM_CHANNELS (PROGRAM_MEM_NUM_CHANNELS),
		.WRITE_ENABLE (1'b0                    )
	) program_memory_controller (
		.clk                  (clk                     ),
		.reset                (reset                   ),
		.consumer_read_valid  (fetcher_read_valid      ),
		.consumer_read_address(fetcher_read_address    ),
		.consumer_read_ready  (fetcher_read_ready      ),
		.consumer_read_data   (fetcher_read_data       ),
		.consumer_write_valid ('0                     ),
		.consumer_write_address(fetcher_read_address   ),
		.consumer_write_data  (fetcher_read_data       ),
		.consumer_write_ready (unused_fetcher_write_ready),
		.mem_read_valid       (program_mem_read_valid  ),
		.mem_read_address     (program_mem_read_address),
		.mem_read_ready       (program_mem_read_ready  ),
		.mem_read_data        (program_mem_read_data   ),
		.mem_write_valid      (unused_program_mem_write_valid),
		.mem_write_address    (unused_program_mem_write_address),
		.mem_write_data       (unused_program_mem_write_data),
		.mem_write_ready      ('0                     )
	);

	dispatch #(
        .NUM_CORES(NUM_CORES),
        .THREADS_PER_BLOCK(THREADS_PER_BLOCK)
    ) dispatch_instance (
        .clk(clk),
        .reset(reset),
        .start(start),
        .thread_count(thread_count),
        .core_done(core_done),
        .core_start(core_start),
        .core_reset(core_reset),
        .core_block_id(core_block_id),
		.core_thread_count(core_thread_count),
		.done(done)
    );

	generate
		for(genvar  i = 0;i < NUM_CORES;i++)
			begin:core
				logic [ THREADS_PER_BLOCK-1:0] core_lsu_read_valid                          ;
				logic [DATA_MEM_ADDR_BITS-1:0] core_lsu_read_address [THREADS_PER_BLOCK-1:0];
				logic [ THREADS_PER_BLOCK-1:0] core_lsu_read_ready                          ;
				logic [DATA_MEM_DATA_BITS-1:0] core_lsu_read_data    [THREADS_PER_BLOCK-1:0];
				logic [ THREADS_PER_BLOCK-1:0] core_lsu_write_valid                         ;
				logic [DATA_MEM_ADDR_BITS-1:0] core_lsu_write_address[THREADS_PER_BLOCK-1:0];
				logic [DATA_MEM_DATA_BITS-1:0] core_lsu_write_data   [THREADS_PER_BLOCK-1:0];
				logic [ THREADS_PER_BLOCK-1:0] core_lsu_write_ready                         ;
				for (genvar j = 0; j < THREADS_PER_BLOCK; j++)
					begin : lsu_connections
						localparam int LSU_INDEX = i * THREADS_PER_BLOCK + j;

						always_comb
							begin
								lsu_read_valid[LSU_INDEX] = core_lsu_read_valid[j];
								lsu_read_address[LSU_INDEX] = core_lsu_read_address[j];
								lsu_write_valid[LSU_INDEX] = core_lsu_write_valid[j];
								lsu_write_address[LSU_INDEX] = core_lsu_write_address[j];
								lsu_write_data[LSU_INDEX] = core_lsu_write_data[j];
								core_lsu_read_ready[j] = lsu_read_ready[LSU_INDEX];
								core_lsu_read_data[j] = lsu_read_data[LSU_INDEX];
								core_lsu_write_ready[j] = lsu_write_ready[LSU_INDEX];
							end
						end

					core #(
						.DATA_MEM_ADDR_BITS    (DATA_MEM_ADDR_BITS),
						.DATA_MEM_DATA_BITS    (DATA_MEM_DATA_BITS),
						.PROGRAM_MEM_ADDR_BITS (PROGRAM_MEM_ADDR_BITS),
						.PROGRAM_MEM_DATA_BITS (PROGRAM_MEM_DATA_BITS),
						.THREADS_PER_BLOCK     (THREADS_PER_BLOCK)
					) core_instance (
						.clk                    (clk),
						.reset                  (core_reset[i]),
						.start                  (core_start[i]),
						.done                   (core_done[i]),
						.block_id               (core_block_id[i]),
						.thread_count           (core_thread_count[i]),

						.program_mem_read_valid   (fetcher_read_valid[i]),
						.program_mem_read_address (fetcher_read_address[i]),
						.program_mem_read_ready   (fetcher_read_ready[i]),
						.program_mem_read_data    (fetcher_read_data[i]),

						.data_mem_read_valid      (core_lsu_read_valid),
						.data_mem_read_address    (core_lsu_read_address),
						.data_mem_read_ready      (core_lsu_read_ready),
						.data_mem_read_data       (core_lsu_read_data),

						.data_mem_write_valid     (core_lsu_write_valid),
						.data_mem_write_address   (core_lsu_write_address),
						.data_mem_write_data      (core_lsu_write_data),
						.data_mem_write_ready     (core_lsu_write_ready)
					);

				end
		endgenerate
	endmodule
