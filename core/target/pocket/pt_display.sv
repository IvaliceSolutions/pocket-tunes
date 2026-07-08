// Pocket Tunes M2 diagnostic screen painter (no CPU yet).
//
// Draws, on the 400x360 canvas:
//   - 2px amber border + dark warm background      → proves boot/video path
//   - amber gradient band                          → proves RGB bit depth
//   - 384x256 window rendering library.json bytes
//     as 1bpp texture (8 px per byte, MSB first)   → proves the data actually arrived
//   - progress bar sized by bytes_loaded           → proves full-file transfer length
//   - bouncing block driven by the frame counter   → proves 60 fps liveness
//
// Combinational paint: core_top registers {rgb, de, hs, vs} one stage after.
// The library BRAM has 1-cycle read latency, so its address is derived from
// next_x/next_y — data for pixel (x,y) is on lib_byte exactly when x/y are current.

`default_nettype none

module pt_display (
    input wire [9:0] x,        // current pixel (valid when de)
    input wire [9:0] y,
    input wire [9:0] next_x,   // one-dot lookahead, for the BRAM fetch
    input wire [9:0] next_y,

    input wire [17:0] bytes_loaded,  // library.json byte count (0..128K-1 shown)
    input wire [9:0]  frame,         // frame counter (liveness animation)
    input wire [7:0]  lib_byte,      // BRAM data for the CURRENT pixel

    output wire [16:0] lib_addr,     // BRAM read address (for the NEXT pixel)
    output reg  [23:0] rgb
);

  // ------------------------------------------------------------------ layout
  localparam HEX_X0 = 8, HEX_X1 = 392;   // 384 px = 48 bytes per row
  localparam HEX_Y0 = 48, HEX_Y1 = 304;  // 256 rows → 12,288 bytes visible
  localparam GRAD_Y0 = 12, GRAD_Y1 = 28;
  localparam BAR_Y0 = 316, BAR_Y1 = 332;
  localparam BNC_Y0 = 340, BNC_Y1 = 348;

  // ------------------------------------------------- BRAM fetch (next pixel)
  wire nx_in_hex = (next_x >= HEX_X0) && (next_x < HEX_X1)
                && (next_y >= HEX_Y0) && (next_y < HEX_Y1);
  wire [9:0] nhx = next_x - HEX_X0;
  wire [9:0] nhy = next_y - HEX_Y0;
  // byte index = row*48 + col  (48 = 32 + 16)
  wire [16:0] hex_index = ({9'd0, nhy[7:0]} << 5) + ({9'd0, nhy[7:0]} << 4)
                        + {11'd0, nhx[8:3]};
  assign lib_addr = nx_in_hex ? hex_index : 17'd0;

  // --------------------------------------------------------- zone predicates
  wire in_border = (x < 2) || (x >= 398) || (y < 2) || (y >= 358);
  wire in_grad = (y >= GRAD_Y0) && (y < GRAD_Y1) && (x >= 8) && (x < 392);
  wire in_hex = (x >= HEX_X0) && (x < HEX_X1) && (y >= HEX_Y0) && (y < HEX_Y1);
  wire in_bar = (y >= BAR_Y0) && (y < BAR_Y1) && (x >= 8) && (x < 392);
  wire in_bnc_row = (y >= BNC_Y0) && (y < BNC_Y1);

  // gradient level: x*5/8 → 0..249 (12-bit sum: 399*5 = 1995 needs 11 bits)
  wire [11:0] grad5 = {2'b00, x} + {x, 2'b00};
  wire [7:0] lvl = grad5[10:3];

  // hexdump bit select (MSB first; HEX_X0 is 8-aligned so x[2:0] is the column)
  wire [2:0] bitsel = 3'd7 - x[2:0];
  wire hex_on = lib_byte[bitsel];

  // gradient: G = 0.625*R (8-bit safe), B = R/16
  wire [7:0] grad_g = {1'b0, lvl[7:1]} + {3'b000, lvl[7:3]};
  wire [7:0] grad_b = {4'b0000, lvl[7:4]};

  // progress bar: 384 px full scale ↔ 96 KB (256 bytes per pixel)
  wire [9:0] fill_px_raw = bytes_loaded[17:8];
  wire [9:0] fill_px = (fill_px_raw > 10'd384) ? 10'd384 : fill_px_raw;
  wire bar_filled = ({1'b0, x} - 11'd8) < {1'b0, fill_px};

  // bouncing block: triangle wave over 768 frames (frame wraps at 768), 8 px wide
  // (named tri_pos because `tri` is a Verilog net-type keyword)
  wire [9:0] tri_pos = (frame >= 10'd384) ? (10'd767 - frame) : frame;  // 0..383
  wire [9:0] bx = 10'd8 + ((tri_pos > 10'd375) ? 10'd375 : tri_pos);
  wire in_bnc = in_bnc_row && (x >= bx) && (x < bx + 10'd8);

  // ------------------------------------------------------------------ paint
  always @(*) begin
    if (in_border)        rgb = 24'hB47818;
    else if (in_bnc)      rgb = 24'hFFE0A0;
    else if (in_grad)     rgb = {lvl, grad_g, grad_b};
    else if (in_hex)      rgb = hex_on ? 24'hFFB428 : 24'h281A08;
    else if (in_bar)      rgb = bar_filled ? 24'hFFA000 : 24'h40280A;
    else                  rgb = 24'h140D04;
  end

endmodule
