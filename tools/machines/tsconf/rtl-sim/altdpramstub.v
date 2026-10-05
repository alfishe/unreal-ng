// Behavioral stand-in for the Altera altdpram megafunction (simulation only).
// Honors the parameters the TS-Conf video RTL uses: width, widthad,
// rdaddress_reg ("INCLOCK" = registered read address, else combinational) and
// outdata_reg ("UNREGISTERED" = combinational output, else registered on outclock).
// All other parameters are accepted and ignored. Memory starts zeroed.
module altdpram
#(
  parameter width = 8,
  parameter widthad = 8,
  parameter indata_aclr = "OFF",
  parameter indata_reg = "INCLOCK",
  parameter intended_device_family = "ACEX1K",
  parameter lpm_file = "",
  parameter lpm_type = "altdpram",
  parameter outdata_aclr = "OFF",
  parameter outdata_reg = "UNREGISTERED",
  parameter rdaddress_aclr = "OFF",
  parameter rdaddress_reg = "INCLOCK",
  parameter rdcontrol_aclr = "OFF",
  parameter rdcontrol_reg = "UNREGISTERED",
  parameter wraddress_aclr = "OFF",
  parameter wraddress_reg = "INCLOCK",
  parameter wrcontrol_aclr = "OFF",
  parameter wrcontrol_reg = "INCLOCK"
)
(
  input  wire               inclock,
  input  wire               outclock,
  input  wire               wren,
  input  wire [width-1:0]   data,
  input  wire [widthad-1:0] rdaddress,
  input  wire [widthad-1:0] wraddress,
  output wire [width-1:0]   q,
  input  wire               aclr,
  input  wire               byteena,
  input  wire               inclocken,
  input  wire               outclocken,
  input  wire               rdaddressstall,
  input  wire               rden,
  input  wire               wraddressstall
);
  reg [width-1:0] mem [0:(1<<widthad)-1];
  reg [widthad-1:0] rda_r = 0;
  reg [width-1:0] q_r = 0;

  integer i;
  initial for (i = 0; i < (1<<widthad); i = i + 1) mem[i] = 0;

  always @(posedge inclock)
  begin
    if (wren) mem[wraddress] <= data;
    rda_r <= rdaddress;
  end

  wire [widthad-1:0] rda = (rdaddress_reg == "INCLOCK") ? rda_r : rdaddress;
  wire [width-1:0] q_c = mem[rda];

  always @(posedge outclock)
    q_r <= q_c;

  assign q = (outdata_reg == "UNREGISTERED") ? q_c : q_r;
endmodule
