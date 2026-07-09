// Pocket Tunes SoC: PicoRV32 + memories + framebuffer scanout + MMIO.
//
// Instantiated by core_top for synthesis and by the testbench for simulation,
// so the simulated design is exactly what ships.
//
// CPU memory map (decoded on addr[31:28]):
//   0x0000_0000  CPU RAM, 128 KB, byte-writable, initialized from firmware.hex
//   0x1000_0000  library.json slot RAM, 128 KB, read-only for the CPU
//   0x2000_0000  framebuffer 320x288 @ 8bpp (92,160 bytes), byte-writable
//   0xF000_0000  MMIO:
//     +0x00  r  cont1_key (buttons, already synchronized)
//     +0x04  r  frame counter (increments each vblank)
//     +0x08  r  bytes_loaded of the library slot
//     +0x0C  r  vblank flag (1 while in vertical blanking)
//     +0x10  r  free-running cycle counter (clk_sys)

`default_nettype none

module pt_soc #(
    parameter FIRMWARE_FILE = "firmware.hex",
    parameter PALETTE_FILE  = "palette.hex"
) (
    input wire clk_sys,
    input wire clk_vid,
    input wire reset_n,  // async-ish (bridge domain); synchronized internally

    // APF bridge passthrough for the library data slot (clk_74a domain)
    input wire        clk_74a,
    input wire        bridge_wr,
    input wire        bridge_endian_little,
    input wire [31:0] bridge_addr,
    input wire [31:0] bridge_wr_data,

    // controller (raw; synchronized internally)
    input wire [15:0] cont1_key,

    // video out (registered, aligned)
    output wire        video_de,
    output wire        video_hs,
    output wire        video_vs,
    output wire [23:0] video_rgb
);

  // ------------------------------------------------------------------ resets
  wire reset_n_sys;
  synch_3 rst_sys_s (reset_n, reset_n_sys, clk_sys);
  wire reset_n_vid;
  synch_3 rst_vid_s (reset_n, reset_n_vid, clk_vid);

  // ------------------------------------------------------------------ inputs
  wire [15:0] cont1_key_s;
  synch_3 #(.WIDTH(16)) key_s (cont1_key, cont1_key_s, clk_sys);

  // ------------------------------------------------------------------ video
  wire de, hs, vs, de_falling, frame_start;
  wire [9:0] vx, vy, vnx, vny;

  video_gen #(
      .H_ACTIVE(320),
      .H_FP    (40),
      .H_SYNC  (32),
      .H_BP    (108),  // total 500
      .V_ACTIVE(288),
      .V_FP    (30),
      .V_SYNC  (3),
      .V_BP    (79)    // total 400 → 500*400 @ 12 MHz = exact 60 Hz
  ) vg (
      .clk        (clk_vid),
      .reset_n    (reset_n_vid),
      .de         (de),
      .hs         (hs),
      .vs         (vs),
      .de_falling (de_falling),
      .x          (vx),
      .y          (vy),
      .next_x     (vnx),
      .next_y     (vny),
      .frame_start(frame_start)
  );

  wire [14:0] fb_word_addr;
  wire [31:0] fb_b_rdata;

  fb_scanout #(
      .PALETTE_FILE(PALETTE_FILE)
  ) scanout (
      .clk_vid(clk_vid),
      .de(de),
      .hs(hs),
      .vs(vs),
      .x(vx),
      .y(vy),
      .next_x(vnx),
      .next_y(vny),
      .fb_word_addr(fb_word_addr),
      .fb_rdata(fb_b_rdata),
      .de_out(video_de),
      .hs_out(video_hs),
      .vs_out(video_vs),
      .rgb_out(video_rgb)
  );

  // vblank → clk_sys (flag + frame counter for the CPU)
  wire in_vblank_vid = (vy >= 10'd288);
  wire in_vblank_s;
  synch_3 vbl_s (in_vblank_vid, in_vblank_s, clk_sys);

  reg [31:0] frame_count = 0;
  reg vbl_prev = 0;
  always @(posedge clk_sys) begin
    vbl_prev <= in_vblank_s;
    if (in_vblank_s && !vbl_prev) frame_count <= frame_count + 1;
  end

  reg [31:0] cycles = 0;
  always @(posedge clk_sys) cycles <= cycles + 1;

  // ------------------------------------------------------------------- CPU
  wire        mem_valid;
  wire        mem_instr;
  reg         mem_ready;
  wire [31:0] mem_addr;
  wire [31:0] mem_wdata;
  wire [ 3:0] mem_wstrb;
  wire [31:0] mem_rdata;

  picorv32 #(
      .ENABLE_COUNTERS  (1),
      .ENABLE_COUNTERS64(0),
      .BARREL_SHIFTER   (1),
      .COMPRESSED_ISA   (0),
      .ENABLE_MUL       (1),
      .ENABLE_DIV       (1),
      .ENABLE_IRQ       (0),
      .PROGADDR_RESET   (32'h0000_0000),
      .STACKADDR        (32'h0002_0000)   // top of 128 KB RAM
  ) cpu (
      .clk   (clk_sys),
      .resetn(reset_n_sys),
      .trap  (),

      .mem_valid(mem_valid),
      .mem_instr(mem_instr),
      .mem_ready(mem_ready),
      .mem_addr (mem_addr),
      .mem_wdata(mem_wdata),
      .mem_wstrb(mem_wstrb),
      .mem_rdata(mem_rdata),

      .mem_la_read (),
      .mem_la_write(),
      .mem_la_addr (),
      .mem_la_wdata(),
      .mem_la_wstrb(),

      .pcpi_valid(),
      .pcpi_insn (),
      .pcpi_rs1  (),
      .pcpi_rs2  (),
      .pcpi_wr   (1'b0),
      .pcpi_rd   (32'd0),
      .pcpi_wait (1'b0),
      .pcpi_ready(1'b0),

      .irq(32'd0),
      .eoi(),

      .trace_valid(),
      .trace_data ()
  );

  // one-wait-state handshake: ready pulses the cycle after valid
  always @(posedge clk_sys) begin
    if (!reset_n_sys) mem_ready <= 0;
    else mem_ready <= mem_valid && !mem_ready;
  end

  wire [3:0] region = mem_addr[31:28];
  wire wr_phase = mem_valid && !mem_ready;  // write exactly once

  // ------------------------------------------------------------- memories
  wire [31:0] ram_rdata;
  cpu_ram #(
      .WORDS(32768),
      .INIT_FILE(FIRMWARE_FILE)
  ) ram (
      .clk  (clk_sys),
      .sel  (wr_phase && region == 4'h0),
      .wstrb(mem_wstrb),
      .addr (mem_addr),
      .wdata(mem_wdata),
      .rdata(ram_rdata)
  );

  wire [31:0] lib_rdata;
  wire [17:0] bytes_loaded;

  library_slot #(
      .ADDR_BITS(17)
  ) lib_slot (
      .clk_74a(clk_74a),
      .clk_sys(clk_sys),

      .bridge_wr           (bridge_wr),
      .bridge_endian_little(bridge_endian_little),
      .bridge_addr         (bridge_addr),
      .bridge_wr_data      (bridge_wr_data),

      .rd_word_addr(mem_addr[16:2]),
      .rd_data     (lib_rdata),

      .bytes_loaded(bytes_loaded)
  );

  wire [31:0] fb_a_rdata;
  pt_framebuffer fb (
      .a_clk  (clk_sys),
      .a_sel  (wr_phase && region == 4'h2),
      .a_wstrb(mem_wstrb),
      .a_addr (mem_addr),
      .a_wdata(mem_wdata),
      .a_rdata(fb_a_rdata),

      .b_clk      (clk_vid),
      .b_word_addr(fb_word_addr),
      .b_rdata    (fb_b_rdata)
  );

  // ---------------------------------------------------------------- MMIO
  reg [31:0] mmio_rdata;
  always @(posedge clk_sys) begin
    case (mem_addr[7:0])
      8'h00:   mmio_rdata <= {16'd0, cont1_key_s};
      8'h04:   mmio_rdata <= frame_count;
      8'h08:   mmio_rdata <= {14'd0, bytes_loaded};
      8'h0C:   mmio_rdata <= {31'd0, in_vblank_s};
      8'h10:   mmio_rdata <= cycles;
      default: mmio_rdata <= 32'd0;
    endcase
  end

  assign mem_rdata = (region == 4'h0) ? ram_rdata
                   : (region == 4'h1) ? lib_rdata
                   : (region == 4'h2) ? fb_a_rdata
                   : mmio_rdata;

endmodule
