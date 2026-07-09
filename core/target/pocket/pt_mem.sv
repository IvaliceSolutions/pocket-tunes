// Memories for the Pocket Tunes SoC.
//
// Both use the canonical byte-enable dual-port BRAM inference pattern
// (Quartus infers M10K with byte enables; iverilog simulates it as-is).

`default_nettype none

// CPU RAM: 32-bit, byte-writable, initialized with the firmware image.
module cpu_ram #(
    parameter WORDS = 32768,           // 128 KB
    parameter INIT_FILE = "firmware.hex"
) (
    input  wire        clk,
    input  wire        sel,
    input  wire [ 3:0] wstrb,
    input  wire [31:0] addr,           // byte address
    input  wire [31:0] wdata,
    output reg  [31:0] rdata
);

  reg [31:0] mem[0:WORDS-1];

  initial $readmemh(INIT_FILE, mem);

  wire [$clog2(WORDS)-1:0] word_addr = addr[$clog2(WORDS)+1:2];

  always @(posedge clk) begin
    if (sel) begin
      if (wstrb[0]) mem[word_addr][7:0] <= wdata[7:0];
      if (wstrb[1]) mem[word_addr][15:8] <= wdata[15:8];
      if (wstrb[2]) mem[word_addr][23:16] <= wdata[23:16];
      if (wstrb[3]) mem[word_addr][31:24] <= wdata[31:24];
    end
    rdata <= mem[word_addr];
  end

endmodule

// Framebuffer: 320x288 @ 8bpp stored as 32-bit words (4 pixels/word).
// Port A: CPU (clk_sys), byte-writable + readable.
// Port B: scanout (clk_vid), read-only 32-bit words.
module pt_framebuffer #(
    parameter WORDS = 23040  // 320*288/4
) (
    input  wire        a_clk,
    input  wire        a_sel,
    input  wire [ 3:0] a_wstrb,
    input  wire [31:0] a_addr,          // byte address
    input  wire [31:0] a_wdata,
    output reg  [31:0] a_rdata,

    input  wire        b_clk,
    input  wire [14:0] b_word_addr,
    output reg  [31:0] b_rdata
);

  reg [31:0] mem[0:WORDS-1];

  wire [14:0] a_word_addr = a_addr[16:2];

  always @(posedge a_clk) begin
    if (a_sel) begin
      if (a_wstrb[0]) mem[a_word_addr][7:0] <= a_wdata[7:0];
      if (a_wstrb[1]) mem[a_word_addr][15:8] <= a_wdata[15:8];
      if (a_wstrb[2]) mem[a_word_addr][23:16] <= a_wdata[23:16];
      if (a_wstrb[3]) mem[a_word_addr][31:24] <= a_wdata[31:24];
    end
    a_rdata <= mem[a_word_addr];
  end

  always @(posedge b_clk) begin
    b_rdata <= mem[b_word_addr];
  end

endmodule
