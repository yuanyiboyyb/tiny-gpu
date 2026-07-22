`default_nettype none
`timescale 1ns/1ns

module memory_controller #(
	parameter int unsigned ADDR_BITS     = 8,
	parameter int unsigned DATA_BITS     = 8,
	parameter int unsigned NUM_CONSUMERS = 4,
	parameter int unsigned NUM_CHANNELS  = 1,
	parameter bit          WRITE_ENABLE  = 1'b1
) (
	input  logic                            clk,
	input  logic                            reset,

	input  logic [     NUM_CONSUMERS-1:0] consumer_read_valid,
	input  logic [        ADDR_BITS-1:0] consumer_read_address [NUM_CONSUMERS-1:0],
	output logic [     NUM_CONSUMERS-1:0] consumer_read_ready,
	output logic [        DATA_BITS-1:0] consumer_read_data    [NUM_CONSUMERS-1:0],

	input  logic [     NUM_CONSUMERS-1:0] consumer_write_valid,
	input  logic [        ADDR_BITS-1:0] consumer_write_address[NUM_CONSUMERS-1:0],
	input  logic [        DATA_BITS-1:0] consumer_write_data   [NUM_CONSUMERS-1:0],
	output logic [     NUM_CONSUMERS-1:0] consumer_write_ready,

	output logic [       NUM_CHANNELS-1:0] mem_read_valid,
	output logic [          ADDR_BITS-1:0] mem_read_address[NUM_CHANNELS-1:0],
	input  logic [       NUM_CHANNELS-1:0] mem_read_ready,
	input  logic [          DATA_BITS-1:0] mem_read_data   [NUM_CHANNELS-1:0],

	output logic [       NUM_CHANNELS-1:0] mem_write_valid,
	output logic [          ADDR_BITS-1:0] mem_write_address[NUM_CHANNELS-1:0],
	output logic [          DATA_BITS-1:0] mem_write_data   [NUM_CHANNELS-1:0],
	input  logic [       NUM_CHANNELS-1:0] mem_write_ready
);
	localparam int unsigned CONSUMER_INDEX_BITS =
		(NUM_CONSUMERS <= 1) ? 1 : $clog2(NUM_CONSUMERS);

	typedef enum logic {
		IDLE = 1'b0,
		BUSY = 1'b1
	} channel_state_t;

	channel_state_t channel_state_d [NUM_CHANNELS-1:0];
	channel_state_t channel_state_p [NUM_CHANNELS-1:0];

	logic [CONSUMER_INDEX_BITS-1:0] current_consumer_d [NUM_CHANNELS-1:0];
	logic [CONSUMER_INDEX_BITS-1:0] current_consumer_p [NUM_CHANNELS-1:0];
	logic                           request_is_write_d [NUM_CHANNELS-1:0];
	logic                           request_is_write_p [NUM_CHANNELS-1:0];
	logic [          ADDR_BITS-1:0] request_address_d  [NUM_CHANNELS-1:0];
	logic [          ADDR_BITS-1:0] request_address_p  [NUM_CHANNELS-1:0];
	logic [          DATA_BITS-1:0] request_write_data_d[NUM_CHANNELS-1:0];
	logic [          DATA_BITS-1:0] request_write_data_p[NUM_CHANNELS-1:0];

	logic [NUM_CONSUMERS-1:0] consumer_selected;

	always_comb begin
		consumer_read_ready  = '0;
		consumer_write_ready = '0;
		consumer_selected    = '0;

		for (int unsigned consumer = 0; consumer < NUM_CONSUMERS; consumer = consumer + 1)
			consumer_read_data[consumer] = '0;

		for (int unsigned channel = 0; channel < NUM_CHANNELS; channel = channel + 1) begin
			channel_state_d[channel]       = channel_state_p[channel];
			current_consumer_d[channel]    = current_consumer_p[channel];
			request_is_write_d[channel]    = request_is_write_p[channel];
			request_address_d[channel]     = request_address_p[channel];
			request_write_data_d[channel]  = request_write_data_p[channel];

			mem_read_valid[channel]        = 1'b0;
			mem_read_address[channel]      = '0;
			mem_write_valid[channel]       = 1'b0;
			mem_write_address[channel]     = '0;
			mem_write_data[channel]        = '0;

			if (channel_state_p[channel] == BUSY)
				consumer_selected[current_consumer_p[channel]] = 1'b1;
		end

		for (int unsigned channel = 0; channel < NUM_CHANNELS; channel = channel + 1) begin
			case (channel_state_p[channel])
				IDLE: begin
					logic request_found;
					request_found = 1'b0;

					for (int unsigned consumer = 0; consumer < NUM_CONSUMERS; consumer = consumer + 1) begin
						if (!request_found && !consumer_selected[consumer] && consumer_read_valid[consumer]) begin
							request_found                       = 1'b1;
							consumer_selected[consumer]          = 1'b1;
							channel_state_d[channel]             = BUSY;
							current_consumer_d[channel]          = CONSUMER_INDEX_BITS'(consumer);
							request_is_write_d[channel]          = 1'b0;
							request_address_d[channel]           = consumer_read_address[consumer];
							request_write_data_d[channel]        = '0;
						end else if (WRITE_ENABLE && !request_found && !consumer_selected[consumer] &&
						             consumer_write_valid[consumer]) begin
							request_found                       = 1'b1;
							consumer_selected[consumer]          = 1'b1;
							channel_state_d[channel]             = BUSY;
							current_consumer_d[channel]          = CONSUMER_INDEX_BITS'(consumer);
							request_is_write_d[channel]          = 1'b1;
							request_address_d[channel]           = consumer_write_address[consumer];
							request_write_data_d[channel]        = consumer_write_data[consumer];
						end
					end
				end

				BUSY: begin
					if (request_is_write_p[channel]) begin
						mem_write_valid[channel]   = 1'b1;
						mem_write_address[channel] = request_address_p[channel];
						mem_write_data[channel]    = request_write_data_p[channel];

						if (mem_write_ready[channel]) begin
							consumer_write_ready[current_consumer_p[channel]] = 1'b1;
							channel_state_d[channel] = IDLE;
						end
					end else begin
						mem_read_valid[channel]   = 1'b1;
						mem_read_address[channel] = request_address_p[channel];

						if (mem_read_ready[channel]) begin
							consumer_read_ready[current_consumer_p[channel]] = 1'b1;
							consumer_read_data[current_consumer_p[channel]]  = mem_read_data[channel];
							channel_state_d[channel] = IDLE;
						end
					end
				end

				default: channel_state_d[channel] = IDLE;
			endcase
		end
	end

	always_ff @(posedge clk) begin
		if (reset) begin
			for (int unsigned channel = 0; channel < NUM_CHANNELS; channel = channel + 1) begin
				channel_state_p[channel]       <= IDLE;
				current_consumer_p[channel]    <= '0;
				request_is_write_p[channel]    <= 1'b0;
				request_address_p[channel]     <= '0;
				request_write_data_p[channel]  <= '0;
			end
		end else begin
			for (int unsigned channel = 0; channel < NUM_CHANNELS; channel = channel + 1) begin
				channel_state_p[channel]       <= channel_state_d[channel];
				current_consumer_p[channel]    <= current_consumer_d[channel];
				request_is_write_p[channel]    <= request_is_write_d[channel];
				request_address_p[channel]     <= request_address_d[channel];
				request_write_data_p[channel]  <= request_write_data_d[channel];
			end
		end
	end

endmodule

`default_nettype wire
