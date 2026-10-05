// Simulation top for the TS-Conf CPU memory path:
// clock (common/clock.v) + zclock (z80/zclock.v) + zsignals (z80/zsignals.v) +
// zmem (z80/zmem.v) + arbiter (dram/arbiter.v) + video_top (video/*.v), wired as
// in top.v. The Z80 itself is not in the RTL tree: its bus pins (address, MREQ_n,
// RD_n, WR_n, M1_n, RFSH_n, data out) are inputs driven by the C++ bus-cycle model
// (cpuharness.cpp), which advances on the Z80 clock edges zclock produces
// (zpos / zneg), so the RTL alone decides when the Z80 clock stops.
//
// DRAM: behavioral, closer to the chip than tbtop.v's read model, because the CPU
// samples the data bus at its own clock edge: the cycle decided at c3 is a read, a
// write or a refresh (as dram.v state); a read drives the word on the data bus only
// while CAS is low (the c2 and c3 periods of the cycle) and a garbage pattern
// otherwise, so a Z80 that samples outside that window reads garbage and the
// harness notices. A write stores the word latched at c0 (dram.v dram_wd) with the
// byte selects latched at c3.
`include "tune.v"

module tbcpu
(
  input  wire        clk,
  input  wire        res,          // video reset
  input  wire        rst_n,        // Z80 / zmem reset (resetter output in top.v)

  // video registers (same strobes as top.v)
  input  wire [7:0]  xt_wr_data,
  input  wire        border_wr,
  input  wire        vpage_wr,
  input  wire        vconf_wr,
  input  wire        tsconf_wr,

  // CPU configuration (zports.v registers in top.v)
  input  wire [1:0]  turbo,        // SYS_CONFIG[1:0]
  input  wire [3:0]  cache_en,     // CACHE_CONFIG[3:0]
  input  wire [3:0]  memconf,      // MEM_CONFIG[3:0]
  input  wire [31:0] xt_page,      // page registers of windows 0..3
  input  wire        ext_stall,    // drives zclock.ide_stall (only used to set the clock phase)

  // Z80 bus pins (driven by the bus-cycle model)
  input  wire [15:0] za,
  input  wire [7:0]  zdo,          // data the Z80 drives in a write cycle
  input  wire        mreq_n,
  input  wire        rd_n,
  input  wire        wr_n,
  input  wire        m1_n,
  input  wire        rfsh_n,

  output wire        zpos_o,
  output wire        zneg_o,
  output wire [7:0]  zd_out_o,     // zmem data to the Z80
  output wire        zd_ena_o,
  output wire        cpu_stall_o,
  output wire        stall14_o,
  output wire        stall357_o,
  output wire        dram_beg_o,
  output wire        cpu_req_o,
  output wire        cpu_next_o,
  output wire        curr_cpu_o,
  output wire        cpu_latch_o,
  output wire        curr_vid_o,
  output wire        video_go_o,
  output wire        video_start_o,
  output wire        csrom_o,
  output wire        c0_o, c1_o, c2_o, c3_o,
  output wire [8:0]  ray_x,
  output wire [8:0]  ray_y
);
  import "DPI-C" function int dram_read(input int addr);
  import "DPI-C" function void dram_write(input int addr, input int data, input int bsel);

  wire f0, f1, h0, h1, c0, c1, c2, c3, ay_clk;

  clock clock
  (
    .clk(clk), .f0(f0), .f1(f1), .h0(h0), .h1(h1),
    .c0(c0), .c1(c1), .c2(c2), .c3(c3), .ay_clk(ay_clk), .ay_mod(2'b00)
  );

  // ---- Z80 bus decoding (zsignals.v) ----
  wire rst, m1, rfsh, zrd, zwr, iorq, mreq, rdwr, iord, iowr, iordwr, memrd, memwr, memrw, opfetch, intack;
  wire iorq_s, mreq_s, iord_s, iowr_s, iordwr_s, memrd_s, memwr_s, memrw_s, opfetch_s;
  wire zpos, zneg;

  zsignals zsignals
  (
    .clk(clk), .zpos(zpos), .rst_n(rst_n), .iorq_n(1'b1), .mreq_n(mreq_n), .m1_n(m1_n),
    .rfsh_n(rfsh_n), .rd_n(rd_n), .wr_n(wr_n),
    .rst(rst), .m1(m1), .rfsh(rfsh), .rd(zrd), .wr(zwr), .iorq(iorq), .mreq(mreq), .rdwr(rdwr),
    .iord(iord), .iowr(iowr), .iordwr(iordwr), .memrd(memrd), .memwr(memwr), .memrw(memrw),
    .opfetch(opfetch), .intack(intack),
    .iorq_s(iorq_s), .mreq_s(mreq_s), .iord_s(iord_s), .iowr_s(iowr_s), .iordwr_s(iordwr_s),
    .memrd_s(memrd_s), .memwr_s(memwr_s), .memrw_s(memrw_s), .opfetch_s(opfetch_s)
  );

  // ---- Z80 clock (zclock.v) ----
  wire clkz_out, cpu_stall, dos_on, dos_off, dos, vdos, pre_vdos;

  zclock zclock
  (
    .clk(clk), .zclk_out(clkz_out), .c0(c0), .c2(c2),
    .iorq_s(iorq_s), .external_port(1'b0),
    .zpos(zpos), .zneg(zneg),
    .cpu_stall(cpu_stall), .ide_stall(ext_stall), .dos_on(dos_on), .vdos_off(1'b0),
    .turbo(turbo)
  );

  // ---- CPU memory manager (zmem.v) ----
  wire [7:0] zd_out;
  wire zd_ena;
  wire [4:0] rompg;
  wire csrom, romoe_n, romwe_n;
  wire cpu_req, cpu_wrbsel, cpu_strobe, cpu_latch, cpu_next;
  wire [20:0] cpu_addr;
  reg  [15:0] rd;

  zmem zmem
  (
    .clk(clk), .c1(c1), .c2(c2), .c3(c3), .zneg(zneg), .rst(rst),
    .za(za), .zd_out(zd_out), .zd_ena(zd_ena),
    .opfetch(opfetch), .opfetch_s(opfetch_s), .memrd(memrd), .memwr(memwr), .memwr_s(memwr_s),
    .turbo(turbo), .cache_en(cache_en), .memconf(memconf), .xt_page(xt_page),
    .rompg(rompg), .csrom(csrom), .romoe_n(romoe_n), .romwe_n(romwe_n),
    .dos(dos), .dos_on(dos_on), .dos_off(dos_off), .vdos(vdos), .pre_vdos(pre_vdos),
    .vdos_on(1'b0), .vdos_off(1'b0),
    .cpu_req(cpu_req), .cpu_addr(cpu_addr), .cpu_wrbsel(cpu_wrbsel), .cpu_rddata(rd),
    .cpu_next(cpu_next), .cpu_strobe(cpu_strobe), .cpu_latch(cpu_latch), .cpu_stall(cpu_stall)
  );

  // ---- DRAM arbiter (dram/arbiter.v) ----
  wire [20:0] daddr, video_addr, ts_addr, tm_addr;
  wire dreq, drnw;
  wire [1:0] dbsel;
  wire [15:0] dram_wrdata;
  wire [4:0] video_bw;
  wire video_go, video_strobe, video_pre_next, video_next, next_video;
  wire ts_req, ts_pre_next, ts_next, tm_req, tm_next, dma_next;

  arbiter arbiter
  (
    .clk(clk), .c1(c1), .c2(c2), .c3(c3),
    .dram_addr(daddr), .dram_req(dreq), .dram_rnw(drnw), .dram_bsel(dbsel), .dram_wrdata(dram_wrdata),
    .cpu_addr(cpu_addr), .cpu_wrdata(zdo), .cpu_req(cpu_req), .cpu_rnw(zrd), .cpu_wrbsel(cpu_wrbsel),
    .cpu_next(cpu_next), .cpu_strobe(cpu_strobe), .cpu_latch(cpu_latch),
    .video_go(video_go), .video_bw(video_bw), .video_addr(video_addr),
    .video_strobe(video_strobe), .video_pre_next(video_pre_next), .video_next(video_next), .next_vid(next_video),
    .dma_addr(21'd0), .dma_wrdata(16'd0), .dma_req(1'b0), .dma_rnw(1'b1), .dma_next(dma_next),
    .ts_req(ts_req), .ts_addr(ts_addr), .ts_pre_next(ts_pre_next), .ts_next(ts_next),
    .tm_addr(tm_addr), .tm_req(tm_req), .tm_next(tm_next)
  );

  // ---- Behavioral DRAM (see the header) ----
  reg        cyc_rd = 0, cyc_wr = 0;
  reg [20:0] cyc_addr = 0;
  reg [1:0]  cyc_bsel = 0;
  reg [15:0] cyc_data = 0, cyc_wd = 0;

  always @(posedge clk)
  begin
    if (c3)
    begin
      cyc_rd   <= dreq && drnw;
      cyc_wr   <= dreq && !drnw;
      cyc_addr <= daddr;
      cyc_bsel <= dbsel;
      cyc_data <= (dreq && drnw) ? 16'(dram_read({11'd0, daddr})) : 16'h0;
    end
    if (c0)
      cyc_wd <= dram_wrdata;                 // dram.v: dram_wd latched at c0
    if (c1 && cyc_wr)
      dram_write({11'd0, cyc_addr}, {16'd0, cyc_wd}, {30'd0, cyc_bsel});  // CAS falls at the end of c1
  end

  // the data bus carries the word only while CAS is low (c2, c3 periods)
  always @*
    rd = (cyc_rd && (c2 || c3)) ? cyc_data : 16'hD3B5;

  // ---- Video (video/*.v): the DRAM load the CPU competes with ----
  wire [1:0] vred, vgrn, vblu;
  wire [4:0] vred_raw, vgrn_raw, vblu_raw;
  wire vdac_mode, hsync, vsync, csync, int_start_s, line_start_s, frame_start_s;

  video_top video_top
  (
    .clk(clk), .f1(f1), .h1(h1), .c0(c0), .c1(c1), .c3(c3),
    .vred(vred), .vgrn(vgrn), .vblu(vblu),
    .vred_raw(vred_raw), .vgrn_raw(vgrn_raw), .vblu_raw(vblu_raw), .vdac_mode(vdac_mode),
    .hsync(hsync), .vsync(vsync), .csync(csync), .ray_x(ray_x), .ray_y(ray_y),
    .d(8'd0), .zmd(16'd0), .zma(8'd0), .cram_we(1'b0), .sfile_we(1'b0), .xt_wr_data(xt_wr_data),
    .zborder_wr(1'b0), .border_wr(border_wr), .zvpage_wr(1'b0), .vpage_wr(vpage_wr), .vconf_wr(vconf_wr),
    .gx_offsl_wr(1'b0), .gx_offsh_wr(1'b0), .gy_offsl_wr(1'b0), .gy_offsh_wr(1'b0),
    .t0x_offsl_wr(1'b0), .t0x_offsh_wr(1'b0), .t0y_offsl_wr(1'b0), .t0y_offsh_wr(1'b0),
    .t1x_offsl_wr(1'b0), .t1x_offsh_wr(1'b0), .t1y_offsl_wr(1'b0), .t1y_offsh_wr(1'b0),
    .tsconf_wr(tsconf_wr), .palsel_wr(1'b0), .tmpage_wr(1'b0), .t0gpage_wr(1'b0), .t1gpage_wr(1'b0),
    .sgpage_wr(1'b0), .hint_beg_wr(1'b0), .vint_begl_wr(1'b0), .vint_begh_wr(1'b0),
    .res(res), .int_start_s(int_start_s), .line_start_s(line_start_s), .frame_start_s(frame_start_s),
    .video_addr(video_addr), .video_bw(video_bw), .video_go(video_go), .dram_rdata(rd),
    .video_pre_next(video_pre_next), .video_strobe(video_strobe),
    .ts_addr(ts_addr), .ts_req(ts_req), .ts_pre_next(ts_pre_next), .ts_next(ts_next),
    .tm_addr(tm_addr), .tm_req(tm_req), .tm_next(tm_next),
    .cfg_60hz(1'b0), .vga_on(1'b0)
  );

  assign zpos_o = zpos;
  assign zneg_o = zneg;
  assign zd_out_o = zd_out;
  assign zd_ena_o = zd_ena;
  assign cpu_stall_o = cpu_stall;
  assign stall14_o = zmem.stall14;
  assign stall357_o = zmem.stall357;
  assign dram_beg_o = zmem.dram_beg;
  assign cpu_req_o = cpu_req;
  assign cpu_next_o = cpu_next;
  assign curr_cpu_o = arbiter.curr_cpu;
  assign curr_vid_o = arbiter.curr_vid;
  assign cpu_latch_o = cpu_latch;
  assign video_go_o = video_go;
  assign video_start_o = arbiter.video_start;
  assign csrom_o = csrom;
  assign c0_o = c0;
  assign c1_o = c1;
  assign c2_o = c2;
  assign c3_o = c3;
endmodule
