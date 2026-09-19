// part of NeoGS project (c) 2007-2008 NedoPC
//
// sound_mulacc is a serial multiplier-accumulator for volume and sound data value.
// Input data is: volume (6bit unsigned) and sound data (8bit signed with sign bit inverted,
//  thus compatible with unsigned data centered at $7f-$80), and clr_sum bit.
// Input data is read and multiply-add starts after 1-clock positive pulse on load pin,
//  when ready becomes 1, operation is finished, output is defined and another load pulse could be accepted.
// If clr_sum is read to be 1, sum is also cleared (and the output will be just result of multiply), otherwise
//  output will be the sum of previous result with current multiplication result.
//
// clock number          XX |  00 |  01 |  02 |  || |  14 |  15 |  16 |
// clock            __/``\__/``\__/``\__/``\__/``||_/``\__/``\__/``\__/``\__/``\__/``\__/``\__/``
// load       _________/`````\___________________||______________________________________________
// inputs are read here --> *                    ||
// ready      ```````````````\___________________||______________/```````````````````````````````
// data out               old data |XXXXXXXXXXXXX||XXXXXXXXXXXXXX| new data ready

module sound_mulacc(

	clock,   // input clock (24 MHz)

	vol_in,  // input volume (6 bit unsigned)
	dat_in,  // input sound data (8 bit signed with sign bit inverted)

	load,    // load pulse input
	clr_sum, // clear sum input

	ready,   // ready output
	sum_out  // 16-bit sum output
);

	input clock;

	input [5:0] vol_in;
	input [7:0] dat_in;

	input load;
	input clr_sum;

	output reg ready;

	output reg [15:0] sum_out;



	wire [5:0] add_data;
	wire [6:0] sum_unreg;
	reg [6:0] sum_reg;
	reg [7:0] shifter;
	reg [5:0] adder;
	wire   mul_out;



	reg [3:0] counter;


	reg clr_sum_reg;
	wire [1:0] temp_sum;
	wire carry_in;
	wire old_data_in;
	reg old_carry;




	// control section
	//
	always @(posedge clock)
	begin

		if( load )
			ready <= 1'b0;

		if( counter[3:0] == 4'd15 )
			ready <= 1'b1;

		if( load )
			counter <= 4'd0;
		else
			counter <= counter + 4'd1;

	end



	// serial multiplier section
	//
	assign add_data = ( shifter[0] ) ? adder : 6'd0; // data to be added controlled by LSB of shifter

	assign sum_unreg[6:0] = sum_reg[6:1] + add_data[5:0]; // sum of two values

	assign mul_out = sum_unreg[0];

	always @(posedge clock)
	begin

		if( !load ) // normal addition
		begin
			sum_reg[6:0] <= sum_unreg[6:0];
			shifter[6:0] <= shifter[7:1];
		end

		else // load==1

		begin
			sum_reg[6:0] <= 7'd0;

			shifter[7]   <= ~dat_in[7];   // convert to signed data (we need signed result)
			shifter[6:0] <=  dat_in[6:0];

			adder <= vol_in;

		end
	end




	// serial adder section
	//
	always @(posedge clock)
	begin
		if( load )
			clr_sum_reg <= clr_sum;
	end

	assign carry_in = (counter==4'd0) ? 1'b0 : old_carry;

	assign old_data_in = (clr_sum_reg) ? 1'b0 : sum_out[0];

	assign temp_sum[1:0] = carry_in + mul_out + old_data_in;

	always @(posedge clock)
	begin
		if( !ready )
		begin
			sum_out[15:0] <= { temp_sum[0], sum_out[15:1] };
			old_carry <= temp_sum[1];
		end
	end



endmodule

