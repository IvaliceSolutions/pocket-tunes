// Memories for the Pocket Tunes SoC.
//
// Every RAM is built as FOUR 8-bit banks (one per byte lane) instead of one
// 32-bit array with byte enables: the plain "simple dual-port, single write"
// 8-bit pattern is the one every synthesizer infers reliably. Quartus 21.1
// choked (30+ min → OOM) trying to infer byte-enabled RAM from the fancier
// single-array patterns.

`default_nettype none

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

// Data-only CPU RAM (M7a): single port, byte-writable, NO init — code lives in
// the external SRAM and crt0 fills .rodata/.data/.bss at boot. Four 8-bit
// banks as ever (reliable single-port BRAM inference).
module cpu_ram_sp #(
    parameter WORDS = 32768  // 128 KB
) (
    input  wire                     clk,
    input  wire [$clog2(WORDS)-1:0] word,
    input  wire [ 3:0]              we,
    input  wire [31:0]              wdata,
    output wire [31:0]              rdata
);

  (* ramstyle = "M10K, no_rw_check" *) reg [7:0] m0[0:WORDS-1];
  (* ramstyle = "M10K, no_rw_check" *) reg [7:0] m1[0:WORDS-1];
  (* ramstyle = "M10K, no_rw_check" *) reg [7:0] m2[0:WORDS-1];
  (* ramstyle = "M10K, no_rw_check" *) reg [7:0] m3[0:WORDS-1];

  reg [7:0] q0, q1, q2, q3;

  always @(posedge clk) begin
    if (we[0]) m0[word] <= wdata[7:0];
    q0 <= m0[word];
  end
  always @(posedge clk) begin
    if (we[1]) m1[word] <= wdata[15:8];
    q1 <= m1[word];
  end
  always @(posedge clk) begin
    if (we[2]) m2[word] <= wdata[23:16];
    q2 <= m2[word];
  end
  always @(posedge clk) begin
    if (we[3]) m3[word] <= wdata[31:24];
    q3 <= m3[word];
  end

  assign rdata = {q3, q2, q1, q0};

endmodule
