// Framebuffer scanout: 320x288 @ 8bpp indexed → 24-bit RGB via a 256-entry
// palette. Two-stage pipeline, everything shifted uniformly:
//   t-1 : word address (from video_gen's one-dot lookahead) → fb port B
//   t   : fb word valid; byte lane picked by x[1:0]; palette lookup issued
//   t+1 : palette RGB valid; de/hs/vs delayed once to match
// core_top's final output register adds one more uniform shift.

`default_nettype none

module fb_scanout #(
    parameter PALETTE_FILE = "palette.hex"
) (
    input wire clk_vid,

    // from video_gen
    input wire       de,
    input wire       hs,
    input wire       vs,
    input wire [9:0] x,
    input wire [9:0] y,
    input wire [9:0] next_x,
    input wire [9:0] next_y,

    // framebuffer port B
    output wire [14:0] fb_word_addr,
    input  wire [31:0] fb_rdata,

    // aligned outputs (register once more downstream)
    output reg        de_out,
    output reg        hs_out,
    output reg        vs_out,
    output reg [23:0] rgb_out
);

  // word address for the NEXT dot: (y*320 + x) / 4 = y*80 + x[9:2]
  wire [14:0] row_base = ({5'd0, next_y} << 6) + ({5'd0, next_y} << 4);  // y*80
  assign fb_word_addr = row_base + {7'd0, next_x[9:2]};

  // palette
  reg [23:0] palette[0:255];
  initial $readmemh(PALETTE_FILE, palette);

  // stage t: pick the byte lane of the current dot
  reg [7:0] idx;
  always @(*) begin
    case (x[1:0])
      2'd0: idx = fb_rdata[7:0];
      2'd1: idx = fb_rdata[15:8];
      2'd2: idx = fb_rdata[23:16];
      2'd3: idx = fb_rdata[31:24];
    endcase
  end

  reg de_d, hs_d, vs_d;
  reg [23:0] pal_q;

  always @(posedge clk_vid) begin
    pal_q <= palette[idx];
    de_d  <= de;
    hs_d  <= hs;
    vs_d  <= vs;

    de_out  <= de_d;
    hs_out  <= hs_d;
    vs_out  <= vs_d;
    rgb_out <= de_d ? pal_q : 24'h0;
  end

endmodule
