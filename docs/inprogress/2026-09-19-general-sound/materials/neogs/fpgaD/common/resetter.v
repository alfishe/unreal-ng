// part of NeoGS project (c) 2007-2008 NedoPC
//


module resetter(

	clk,

	rst_in1_n,
	rst_in2_n,

	rst_out_n );

parameter RST_CNT_SIZE = 3;


	input clk;

	input rst_in1_n; // input of external asynchronous reset 1
	input rst_in2_n; // input of external asynchronous reset 2

	output reg rst_out_n; // output of end-synchronized reset (beginning is asynchronous to clock)



	reg [RST_CNT_SIZE:0] rst_cnt; // one bit more for counter stopping

	reg rst1_n,rst2_n;

	wire resets_n;


	assign resets_n = rst_in1_n & rst_in2_n;

	always @(posedge clk, negedge resets_n)
	if( !resets_n ) // external asynchronous reset
	begin
		rst_cnt <= 0;

		rst1_n <= 1'b0;
		rst2_n <= 1'b0; // sync in reset end

		rst_out_n <= 1'b0; // this zeroing also happens after FPGA configuration, so also power-up reset happens
	end
	else // clocking
	begin
		rst1_n <= 1'b1;
		rst2_n <= rst1_n;

		if( rst2_n && !rst_cnt[RST_CNT_SIZE] )
		begin
			rst_cnt <= rst_cnt + 1;
		end

		rst_out_n <= rst_cnt[RST_CNT_SIZE];
	end


endmodule

