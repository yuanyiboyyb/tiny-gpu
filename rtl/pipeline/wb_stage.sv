`default_nettype none
`timescale 1ns/1ns

module wb_stage #(
	parameter int unsigned DATA_BITS = 8
) (
	input  wire [DATA_BITS-1:0] execute_result  , // MEM/WB统一写回数据
	input  wire [          2:0] nzp_result      , // MEM/WB的NZP结果
	input  wire [          3:0] rd_addr         , // 目标寄存器编号
	input  wire nzp_write       , // NZP写使能
	input  wire reg_write       , // 通用寄存器写使能
	input  wire is_ret          , // RET指令标志
	input  wire valid           , // MEM/WB数据有效

	output logic                 write_enable    , // 寄存器堆写使能
	output logic [          3:0] write_addr      , // 寄存器堆写地址
	output logic [DATA_BITS-1:0] write_data      , // 寄存器堆写数据
	output logic                 nzp_write_enable, // NZP寄存器写使能
	output logic [          2:0] nzp_write_data  , // NZP寄存器写数据
	output logic                 done              // 线程执行完成
);

	always_comb begin
		write_enable     = valid && reg_write;
		write_addr       = rd_addr;
		write_data       = execute_result;
		nzp_write_enable = valid && nzp_write;
		nzp_write_data   = nzp_result;
		done             = valid && is_ret;
	end

endmodule

`default_nettype wire
