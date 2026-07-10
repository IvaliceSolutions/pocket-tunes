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
//   Port A: VexRiscv iBus instruction fetch (a_we tied 0 in the SoC -> read only)
//   Port B: VexRiscv dBus data access (read + byte-enabled write)
//
// Both ports are declared SYMMETRICALLY (each with its own write path) so
// Quartus infers a single true-dual-port M10K per bank. Declaring port A as
// pure-read made Quartus replicate every bank into two M10K blocks (a read-only
// copy + a 1W1R copy) — doubling the whole 128 KB RAM to ~2 Mbit and blowing the
// 308-block M10K budget. ramstyle forces block RAM; no_rw_check drops the
// read-during-write bypass logic the ports never rely on.
module cpu_ram_dp #(
    parameter WORDS = 32768,  // 128 KB
    parameter INIT_B0 = "firmware_b0.hex",
    parameter INIT_B1 = "firmware_b1.hex",
    parameter INIT_B2 = "firmware_b2.hex",
    parameter INIT_B3 = "firmware_b3.hex"
) (
    input  wire                    clk,
    // port A: instruction fetch (a_we held 0 -> read only)
    input  wire [$clog2(WORDS)-1:0] a_word,
    input  wire                     a_we,
    input  wire [31:0]              a_wdata,
    output wire [31:0]              a_rdata,
    // port B: data (read + byte-enabled write)
    input  wire [$clog2(WORDS)-1:0] b_word,
    input  wire [ 3:0]              b_we,
    input  wire [31:0]              b_wdata,
    output wire [31:0]              b_rdata
);

  (* ramstyle = "M10K, no_rw_check" *) reg [7:0] m0[0:WORDS-1];
  (* ramstyle = "M10K, no_rw_check" *) reg [7:0] m1[0:WORDS-1];
  (* ramstyle = "M10K, no_rw_check" *) reg [7:0] m2[0:WORDS-1];
  (* ramstyle = "M10K, no_rw_check" *) reg [7:0] m3[0:WORDS-1];

  initial begin
    $readmemh(INIT_B0, m0);
    $readmemh(INIT_B1, m1);
    $readmemh(INIT_B2, m2);
    $readmemh(INIT_B3, m3);
  end

  reg [7:0] a0, a1, a2, a3;   // port A read regs
  reg [7:0] qb0, qb1, qb2, qb3;  // port B read regs

  // Each bank: two symmetric R/W ports -> one true-dual-port M10K.
  // bank 0
  always @(posedge clk) begin
    if (a_we) m0[a_word] <= a_wdata[7:0];
    a0 <= m0[a_word];
  end
  always @(posedge clk) begin
    if (b_we[0]) m0[b_word] <= b_wdata[7:0];
    qb0 <= m0[b_word];
  end
  // bank 1
  always @(posedge clk) begin
    if (a_we) m1[a_word] <= a_wdata[15:8];
    a1 <= m1[a_word];
  end
  always @(posedge clk) begin
    if (b_we[1]) m1[b_word] <= b_wdata[15:8];
    qb1 <= m1[b_word];
  end
  // bank 2
  always @(posedge clk) begin
    if (a_we) m2[a_word] <= a_wdata[23:16];
    a2 <= m2[a_word];
  end
  always @(posedge clk) begin
    if (b_we[2]) m2[b_word] <= b_wdata[23:16];
    qb2 <= m2[b_word];
  end
  // bank 3
  always @(posedge clk) begin
    if (a_we) m3[a_word] <= a_wdata[31:24];
    a3 <= m3[a_word];
  end
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
