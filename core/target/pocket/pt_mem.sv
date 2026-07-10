// Memories for the Pocket Tunes SoC.
//
// Every RAM is built as FOUR 8-bit banks (one per byte lane) instead of one
// 32-bit array with byte enables: the plain "simple dual-port, single write"
// 8-bit pattern is the one every synthesizer infers reliably. Quartus 21.1
// choked (30+ min → OOM) trying to infer byte-enabled RAM from the fancier
// single-array patterns.

`default_nettype none

// CPU RAM: 32-bit, byte-writable, TRUE DUAL-PORT, initialized with the firmware
// image (one init file per byte lane, from firmware/build.sh).
//   Port A: read-only  — VexRiscv iBus instruction fetch
//   Port B: read/write  — VexRiscv dBus data access (byte-enabled)
// Four 8-bit banks, each a true dual-port BRAM (two always blocks per array).
module cpu_ram_dp #(
    parameter WORDS = 32768,  // 128 KB
    parameter INIT_B0 = "firmware_b0.hex",
    parameter INIT_B1 = "firmware_b1.hex",
    parameter INIT_B2 = "firmware_b2.hex",
    parameter INIT_B3 = "firmware_b3.hex"
) (
    input  wire                    clk,
    // port A: instruction fetch (read only)
    input  wire [$clog2(WORDS)-1:0] a_word,
    output wire [31:0]              a_rdata,
    // port B: data (read + byte-enabled write)
    input  wire [$clog2(WORDS)-1:0] b_word,
    input  wire [ 3:0]              b_we,
    input  wire [31:0]              b_wdata,
    output wire [31:0]              b_rdata
);

  reg [7:0] m0[0:WORDS-1];
  reg [7:0] m1[0:WORDS-1];
  reg [7:0] m2[0:WORDS-1];
  reg [7:0] m3[0:WORDS-1];

  initial begin
    $readmemh(INIT_B0, m0);
    $readmemh(INIT_B1, m1);
    $readmemh(INIT_B2, m2);
    $readmemh(INIT_B3, m3);
  end

  reg [7:0] a0, a1, a2, a3;   // port A read regs
  reg [7:0] qb0, qb1, qb2, qb3;  // port B read regs

  // bank 0
  always @(posedge clk) a0 <= m0[a_word];
  always @(posedge clk) begin
    if (b_we[0]) m0[b_word] <= b_wdata[7:0];
    qb0 <= m0[b_word];
  end
  // bank 1
  always @(posedge clk) a1 <= m1[a_word];
  always @(posedge clk) begin
    if (b_we[1]) m1[b_word] <= b_wdata[15:8];
    qb1 <= m1[b_word];
  end
  // bank 2
  always @(posedge clk) a2 <= m2[a_word];
  always @(posedge clk) begin
    if (b_we[2]) m2[b_word] <= b_wdata[23:16];
    qb2 <= m2[b_word];
  end
  // bank 3
  always @(posedge clk) a3 <= m3[a_word];
  always @(posedge clk) begin
    if (b_we[3]) m3[b_word] <= b_wdata[31:24];
    qb3 <= m3[b_word];
  end

  assign a_rdata = {a3, a2, a1, a0};
  assign b_rdata = {qb3, qb2, qb1, qb0};

endmodule

// Framebuffer: 320x288 @ 8bpp stored as 32-bit words (4 pixels/word).
// Port A: CPU (clk_sys), write-only (the firmware never reads pixels back).
// Port B: scanout (clk_vid), read-only 32-bit words.
module pt_framebuffer #(
    parameter WORDS = 23040  // 320*288/4
) (
    input wire        a_clk,
    input wire        a_sel,
    input wire [ 3:0] a_wstrb,
    input wire [31:0] a_addr,   // byte address
    input wire [31:0] a_wdata,

    input  wire        b_clk,
    input  wire [14:0] b_word_addr,
    output wire [31:0] b_rdata
);

  wire [14:0] a_word_addr = a_addr[16:2];

  reg [7:0] b0[0:WORDS-1];
  reg [7:0] b1[0:WORDS-1];
  reg [7:0] b2[0:WORDS-1];
  reg [7:0] b3[0:WORDS-1];

  always @(posedge a_clk) begin
    if (a_sel && a_wstrb[0]) b0[a_word_addr] <= a_wdata[7:0];
  end
  always @(posedge a_clk) begin
    if (a_sel && a_wstrb[1]) b1[a_word_addr] <= a_wdata[15:8];
  end
  always @(posedge a_clk) begin
    if (a_sel && a_wstrb[2]) b2[a_word_addr] <= a_wdata[23:16];
  end
  always @(posedge a_clk) begin
    if (a_sel && a_wstrb[3]) b3[a_word_addr] <= a_wdata[31:24];
  end

  reg [7:0] q0, q1, q2, q3;

  always @(posedge b_clk) q0 <= b0[b_word_addr];
  always @(posedge b_clk) q1 <= b1[b_word_addr];
  always @(posedge b_clk) q2 <= b2[b_word_addr];
  always @(posedge b_clk) q3 <= b3[b_word_addr];

  assign b_rdata = {q3, q2, q1, q0};

endmodule
