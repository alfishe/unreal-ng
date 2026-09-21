// part of NeoGS project (c) 2007-2008 NedoPC
//
// memmap is memory mapper for NGS. Physical memory divided in 16kb pages.
// At (a15=0,a14=0) there is always zero page of MEM, at (a15=0,a14=1) there is
// always third page of RAM, at (a15=1,a14=0) there is page of MEM determined by
// mode_pg0 input bus, at (a15=1,a14=1) - page of MEM determined by mode_pg1 input.
// When mode_norom=0, MEM is ROM, otherwise MEM is RAM.
// When mode_ramro=1, zero and first pages of RAM are read-only.
// Memory addressed by mema14..mema18 (total 512kb) and then by either
// romcs_n (only 512kb of ROM) or ram0cs_n..ram3cs_n (2Mb of RAM).
// Memory decoding is static - it depends on only a14,a15 and mode_pg0,1 inputs.
// memoe_n and memwe_n generated from only mreq_n, rd_n and wr_n with the
// exception of read-only page of RAM (no memwe_n). ROM is always read/write (flash).

module memmap(

	a15,a14, // Z80 address signals

	mreq_n, // Z80 bus control signals
	rd_n,   //
	wr_n,   //

	mema14,mema15, // memory addresses
	mema16,mema17, //
	mema18,        // (512kB max)

	ram0cs_n, // four RAM /CS'es
	ram1cs_n, //
	ram2cs_n, //
	ram3cs_n, // (total 512kb * 4 = 2Mb)

	romcs_n, // ROM (flash) /CS

	memoe_n, // memory /OE and /WE
	memwe_n, //

	mode_ramro, // 1 - zero page (32k) of ram is R/O
	mode_norom, // 0 - ROM instead of RAM at everything except $4000-$7FFF
	mode_pg0,   // page at $8000-$BFFF
	mode_pg1    // page at $C000-$FFFF (128x16kb = 2Mb max)
);

// inputs and outputs

	input a15,a14;

	input mreq_n,rd_n,wr_n;

	output reg mema14,mema15,mema16,mema17,mema18;

	output reg ram0cs_n,ram1cs_n,ram2cs_n,ram3cs_n;

	output reg romcs_n;

	output reg memoe_n,memwe_n;

	input mode_ramro,mode_norom;
	input [6:0] mode_pg0,mode_pg1;


// internal vars and regs

	reg [6:0] high_addr;



// addresses mapping

	always @*
	begin
        case( {a15,a14} )
			2'b00: // $0000-$3FFF
				high_addr <= 7'b0000000;
			2'b01: // $4000-$7FFF
				high_addr <= 7'b0000011;
			2'b10: // $8000-$BFFF
				high_addr <= mode_pg0;
			2'b11: // $C000-$FFFF
				high_addr <= mode_pg1;
        endcase
	end


// memory addresses

	always @*
	begin
		{ mema18,mema17,mema16,mema15,mema14 } <= high_addr[4:0];
	end


// memory chip selects

	always @*
	begin
		if( (mode_norom==1'b0) && ( {a15,a14}!=2'b01 ) ) // ROM selected
		begin
			romcs_n <= 1'b0;

			ram0cs_n <= 1'b1;
			ram1cs_n <= 1'b1;
			ram2cs_n <= 1'b1;
			ram3cs_n <= 1'b1;
		end
		else // RAM
		begin
			romcs_n <= 1'b1;

			ram0cs_n <= ( high_addr[6:5]==2'b00 ) ? 1'b0 : 1'b1;
			ram1cs_n <= ( high_addr[6:5]==2'b01 ) ? 1'b0 : 1'b1;
			ram2cs_n <= ( high_addr[6:5]==2'b10 ) ? 1'b0 : 1'b1;
			ram3cs_n <= ( high_addr[6:5]==2'b11 ) ? 1'b0 : 1'b1;
		end
	end


// memory /OE and /WE

	always @*
	begin
		memoe_n <= mreq_n | rd_n;

		if( (high_addr[6:1] == 6'd0) && (mode_ramro==1'b1) && (mode_norom==1'b1) ) // R/O
			memwe_n <= 1'b1;
		else // no R/O
			memwe_n <= mreq_n | wr_n;
	end

endmodule

