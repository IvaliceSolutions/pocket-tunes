// Testbench for the M2 video pipeline: video_gen + pt_display + library BRAM,
// replicating core_top's glue (frame counter, final register stage).
//
// Checks (frame 1):
//   - frame period exactly 500*400 = 200,000 dot clocks
//   - DE high exactly 400 dots on each of exactly 360 lines
//   - 400 HS pulses and 1 VS pulse per frame
// Renders (frame 2) to sim/out_frame.ppm for visual inspection, with the real
// library.json preloaded into the BRAM via $readmemh.

`timescale 1ns / 1ps

module tb_video;

  reg clk = 0;
  always #41.666 clk = ~clk;  // 12 MHz

  reg reset_n = 0;

  // ------------------------------------------------------------- DUT: timing
  wire de, hs, vs, de_falling, frame_start;
  wire [9:0] x, y, next_x, next_y;

  video_gen vg (
      .clk(clk),
      .reset_n(reset_n),
      .de(de),
      .hs(hs),
      .vs(vs),
      .de_falling(de_falling),
      .x(x),
      .y(y),
      .next_x(next_x),
      .next_y(next_y),
      .frame_start(frame_start)
  );

  // --------------------------------------------- library BRAM (behavioral)
  reg [7:0] mem[0:131071];
  integer lib_size;

  wire [16:0] lib_addr;
  reg [7:0] lib_byte;
  always @(posedge clk) lib_byte <= mem[lib_addr];  // same 1-cycle latency as bram_block_dp

  // ------------------------------------------------------------- DUT: paint
  reg [9:0] frame_count = 0;
  always @(posedge clk) if (frame_start) frame_count <= (frame_count == 767) ? 0 : frame_count + 1;

  wire [23:0] paint_rgb;

  pt_display painter (
      .x(x),
      .y(y),
      .next_x(next_x),
      .next_y(next_y),
      .bytes_loaded(18'd65133),  // real library.json size on the SD card
      .frame(frame_count),
      .lib_byte(lib_byte),
      .lib_addr(lib_addr),
      .rgb(paint_rgb)
  );

  // final register stage (as in core_top)
  reg r_de, r_hs, r_vs;
  reg [23:0] r_rgb;
  always @(posedge clk) begin
    r_de  <= de;
    r_hs  <= hs;
    r_vs  <= vs;
    r_rgb <= de ? paint_rgb : 24'h0;
  end

  // ------------------------------------------------------------------ checks
  integer clk_in_frame = 0;
  integer de_in_line = 0;
  integer lines_with_de = 0;
  integer hs_pulses = 0;
  integer vs_pulses = 0;
  integer frames_done = 0;
  integer errors = 0;
  reg prev_hs = 0, prev_vs = 0, prev_de = 0;
  reg counting = 0;

  // PPM output (frame 2)
  integer fppm = 0;
  integer px_written = 0;

  task check_eq(input integer got, input integer want, input [255:0] what);
    if (got !== want) begin
      $display("FAIL %0s: got %0d want %0d", what, got, want);
      errors = errors + 1;
    end else begin
      $display("ok   %0s = %0d", what, got);
    end
  endtask

  always @(posedge clk) begin
    prev_hs <= r_hs;
    prev_vs <= r_vs;
    prev_de <= r_de;

    if (counting) begin
      clk_in_frame <= clk_in_frame + 1;
      if (r_hs && !prev_hs) hs_pulses <= hs_pulses + 1;
      if (r_vs && !prev_vs) vs_pulses <= vs_pulses + 1;

      if (r_de) de_in_line <= de_in_line + 1;
      if (!r_de && prev_de) begin
        // end of a DE run: every active line must be exactly 400 dots
        if (de_in_line !== 400) begin
          $display("FAIL line width: %0d at line %0d", de_in_line, lines_with_de);
          errors = errors + 1;
        end
        lines_with_de <= lines_with_de + 1;
        de_in_line <= 0;
      end
    end

    // PPM capture during frame 2
    if (frames_done == 1 && r_de && fppm != 0) begin
      $fwrite(fppm, "%0d %0d %0d\n", r_rgb[23:16], r_rgb[15:8], r_rgb[7:0]);
      px_written = px_written + 1;
    end
  end

  // frame bookkeeping — frame_start fires at (0,0); registered stage lags 1 clk
  always @(posedge clk) begin
    if (frame_start && counting) begin
      // end of measured frame
      check_eq(clk_in_frame, 200000, "frame period (clks)");
      check_eq(lines_with_de, 360, "active lines");
      check_eq(hs_pulses, 400, "HS pulses");
      check_eq(vs_pulses, 1, "VS pulses");
      counting <= 0;
      frames_done <= 1;
      fppm = $fopen("out_frame.ppm", "w");
      $fwrite(fppm, "P3\n400 360\n255\n");
    end else if (frame_start && frames_done == 1) begin
      $fclose(fppm);
      fppm = 0;
      check_eq(px_written, 144000, "pixels written to PPM");
      if (errors == 0) $display("ALL CHECKS PASSED");
      else $display("%0d ERRORS", errors);
      $finish;
    end else if (frame_start && !counting && frames_done == 0) begin
      // first full frame boundary after reset: start measuring.
      // Starts at 1: the increment block is gated on `counting` and skips
      // this very cycle, which belongs to the measured frame.
      counting <= 1;
      clk_in_frame <= 1;
      de_in_line <= 0;
      lines_with_de <= 0;
      hs_pulses <= 0;
      vs_pulses <= 0;
    end
  end

  initial begin
    // preload the real library.json (converted to hex, one byte per line)
    for (lib_size = 0; lib_size < 131072; lib_size = lib_size + 1) mem[lib_size] = 8'h00;
    $readmemh("library_bytes.hex", mem);

    repeat (10) @(posedge clk);
    reset_n = 1;

    // safety timeout: 3 frames + margin
    repeat (700000) @(posedge clk);
    $display("FAIL timeout");
    $finish;
  end

endmodule
