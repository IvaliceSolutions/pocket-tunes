// Full-SoC testbench: the real firmware runs on the real pt_soc RTL.
//
// - library RAM preloaded hierarchically with the real library.json
// - captures frame N to out_soc_a.ppm
// - presses DOWN then A, captures a later frame to out_soc_b.ppm
//   (cursor should move and the "* OPEN" tag appear)

`timescale 1ns / 1ps

module tb_soc;

  reg clk_sys = 0;
  always #10.417 clk_sys = ~clk_sys;  // 48 MHz
  reg clk_vid = 0;
  always #41.666 clk_vid = ~clk_vid;  // 12 MHz
  reg clk_74a = 0;
  always #6.734 clk_74a = ~clk_74a;  // 74.25 MHz (unused: no bridge writes here)

  reg reset_n = 0;
  reg [15:0] cont1_key = 0;

  wire de, hs, vs;
  wire [23:0] rgb;

  pt_soc #(
      .FIRMWARE_B0 ("../projects/firmware_b0.hex"),
      .FIRMWARE_B1 ("../projects/firmware_b1.hex"),
      .FIRMWARE_B2 ("../projects/firmware_b2.hex"),
      .FIRMWARE_B3 ("../projects/firmware_b3.hex"),
      .PALETTE_FILE("../projects/palette.hex")
  ) dut (
      .clk_sys(clk_sys),
      .clk_vid(clk_vid),
      .reset_n(reset_n),

      .clk_74a             (clk_74a),
      .bridge_wr           (1'b0),
      .bridge_endian_little(1'b0),
      .bridge_addr         (32'd0),
      .bridge_wr_data      (32'd0),

      .cont1_key(cont1_key),

      .video_de (de),
      .video_hs (hs),
      .video_vs (vs),
      .video_rgb(rgb)
  );

  // ------------------------------------------------- library preload (words)
  integer lib_size = 0;
  initial begin
    $readmemh("library_b0.hex", dut.lib_slot.mb0);
    $readmemh("library_b1.hex", dut.lib_slot.mb1);
    $readmemh("library_b2.hex", dut.lib_slot.mb2);
    $readmemh("library_b3.hex", dut.lib_slot.mb3);
    // bytes_loaded is a plain reg — poke the real byte size
    dut.lib_slot.bytes_loaded = 18'd65133;
  end

  // -------------------------------------------------------- frame capture
  integer frame_no = 0;
  integer fppm = 0;
  integer px_count = 0;
  reg vs_prev = 0;
  reg capturing = 0;
  reg [1023:0] capture_name;

  task start_capture(input [1023:0] name);
    begin
      capture_name = name;
      capturing = 1;
    end
  endtask

  always @(posedge clk_vid) begin
    vs_prev <= vs;
    if (vs && !vs_prev) begin
      frame_no = frame_no + 1;
      if (fppm != 0) begin
        $fclose(fppm);
        $display("frame %0d captured (%0d px)", frame_no - 1, px_count);
        if (px_count != 320 * 288) $display("FAIL pixel count %0d", px_count);
        fppm = 0;
      end
      if (capturing) begin
        fppm = $fopen(capture_name, "w");
        $fwrite(fppm, "P3\n320 288\n255\n");
        px_count = 0;
        capturing = 0;
      end
    end
    if (fppm != 0 && de) begin
      $fwrite(fppm, "%0d %0d %0d\n", rgb[23:16], rgb[15:8], rgb[7:0]);
      px_count = px_count + 1;
    end
  end

  // ---------------------------------------------------------------- script
  initial begin
    repeat (20) @(posedge clk_sys);
    reset_n = 1;

    // frame 1-2: CPU draws. Capture frame 3.
    wait (frame_no == 2);
    start_capture("out_soc_a.ppm");
    wait (frame_no == 4);

    // press DOWN for a frame, release, then A
    cont1_key = 16'h0002;  // DOWN
    wait (frame_no == 5);
    cont1_key = 0;
    wait (frame_no == 6);
    cont1_key = 16'h0010;  // A
    wait (frame_no == 7);
    cont1_key = 0;

    wait (frame_no == 8);
    start_capture("out_soc_b.ppm");
    wait (frame_no == 10);

    $display("DONE");
    $finish;
  end

  initial begin
    #200_000_000;  // 200 ms sim-time safety
    $display("FAIL timeout");
    $finish;
  end

endmodule
