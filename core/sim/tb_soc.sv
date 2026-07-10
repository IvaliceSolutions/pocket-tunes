// Full-SoC testbench with a behavioral APF model.
//
// The firmware streams library.json through 0x0180 target-read commands; this
// TB implements the APF side: it watches the target wires, serves file bytes
// as bridge word-writes to the requested bridge address, and answers the
// datatable. Captures the same three UI screens as M3b.

`timescale 1ns / 1ps

module tb_soc;

  reg clk_sys = 0;
  always #10.417 clk_sys = ~clk_sys;  // 48 MHz
  reg clk_vid = 0;
  always #41.666 clk_vid = ~clk_vid;  // 12 MHz
  reg clk_74a = 0;
  always #6.734 clk_74a = ~clk_74a;  // 74.25 MHz

  reg reset_n = 0;
  reg [15:0] cont1_key = 0;

  localparam LIB_SIZE = 390;

  // bridge driven by the APF model
  reg        bridge_wr = 0;
  reg        bridge_rd = 0;
  reg [31:0] bridge_addr = 0;
  reg [31:0] bridge_wr_data = 0;

  // target-command wires
  wire target_read, target_openfile, target_getfile;
  reg tgt_ack = 0, tgt_done = 0;
  reg [2:0] tgt_err = 0;
  wire [15:0] tgt_id;
  wire [31:0] tgt_offset, tgt_bridgeaddr, tgt_length, tgt_param_ptr, tgt_resp_ptr;
  wire [31:0] param_rd_data;

  wire [9:0] dt_addr;
  localparam AUDIO_SIZE = 33062;
  wire [31:0] dt_q = (dt_addr == 10'd1) ? LIB_SIZE
                   : (dt_addr == 10'd2) ? 32'd1        // slot id 1 (audio)
                   : (dt_addr == 10'd3) ? AUDIO_SIZE
                   : 32'd0;

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
      .bridge_wr           (bridge_wr),
      .bridge_rd           (bridge_rd),
      .bridge_endian_little(1'b0),
      .bridge_addr         (bridge_addr),
      .bridge_wr_data      (bridge_wr_data),
      .param_rd_data       (param_rd_data),

      .target_dataslot_read    (target_read),
      .target_dataslot_openfile(target_openfile),
      .target_dataslot_getfile (target_getfile),
      .target_dataslot_ack     (tgt_ack),
      .target_dataslot_done    (tgt_done),
      .target_dataslot_err     (tgt_err),
      .target_dataslot_id        (tgt_id),
      .target_dataslot_slotoffset(tgt_offset),
      .target_dataslot_bridgeaddr(tgt_bridgeaddr),
      .target_dataslot_length    (tgt_length),
      .target_buffer_param_struct(tgt_param_ptr),
      .target_buffer_resp_struct (tgt_resp_ptr),

      .datatable_addr(dt_addr),
      .datatable_q   (dt_q),

      .cont1_key(cont1_key),

      .audio_l(),
      .audio_r(),

      .video_de (de),
      .video_hs (hs),
      .video_vs (vs),
      .video_rgb(rgb)
  );

  // --------------------------------------------------------- file images
  reg [7:0] lib_img[0:131071];
  initial $readmemh("library_bytes.hex", lib_img);
  reg [7:0] audio_img[0:65535];
  initial $readmemh("audio_bytes.hex", audio_img);

  // ------------------------------------------------------- APF model
  integer w, nwords, base;
  reg prev_read = 0, prev_open = 0;

  task do_bridge_write(input [31:0] addr, input [31:0] data);
    begin
      @(posedge clk_74a);
      bridge_addr <= addr;
      bridge_wr_data <= data;
      bridge_wr <= 1;
      @(posedge clk_74a);
      bridge_wr <= 0;
      // real APF paces ~one word per 75 clk_74a cycles; data_loader's 4-deep
      // CDC fifo drains slower than 8-cycle pacing and silently drops words
      repeat (73) @(posedge clk_74a);
    end
  endtask

  always @(posedge clk_74a) begin
    prev_read <= target_read;
    prev_open <= target_openfile;
  end

  // serve 0x0180 reads
  always @(posedge clk_74a) begin
    if (target_read && !prev_read) begin
      tgt_done <= 0;
      tgt_err <= 0;
      tgt_ack <= 1;
      serve_read(tgt_id, tgt_offset, tgt_bridgeaddr, tgt_length);
    end
  end

  task serve_read(input [15:0] id, input [31:0] off, input [31:0] baddr, input [31:0] len);
    begin
      if (id == 0) begin
        $display("APF model: serve lib read off=%0d len=%0d t=%0t", off, len, $time); $fflush;
        nwords = (len + 3) / 4;
        for (w = 0; w < nwords; w = w + 1) begin
          do_bridge_write(baddr + w * 4,
                          {lib_img[off+w*4], lib_img[off+w*4+1],
                           lib_img[off+w*4+2], lib_img[off+w*4+3]});
        end
      end else if (id == 1) begin
        $display("APF model: serve AUDIO read off=%0d len=%0d t=%0t", off, len, $time); $fflush;
        nwords = (len + 3) / 4;
        for (w = 0; w < nwords; w = w + 1) begin
          do_bridge_write(baddr + w * 4,
                          {audio_img[off+w*4], audio_img[off+w*4+1],
                           audio_img[off+w*4+2], audio_img[off+w*4+3]});
        end
      end else begin
        $display("APF model: read on unknown slot %0d", id);
        tgt_err <= 3'd2;
      end
      repeat (30) @(posedge clk_74a);  // real APF: ok-status write trails data
      tgt_ack  <= 0;
      tgt_done <= 1;
    end
  endtask

  // acknowledge openfile with a path dump (audio serving arrives with M4b)
  reg [7:0] path_bytes[0:255];
  integer pi;
  always @(posedge clk_74a) begin
    if (target_openfile && !prev_open) begin
      tgt_done <= 0;
      tgt_err <= 0;
      tgt_ack <= 1;
      dump_path(tgt_param_ptr);
    end
  end

  task dump_path(input [31:0] pbase);
    reg [31:0] word;
    begin
      $write("APF model: openfile slot %0d path: ", tgt_id);
      for (pi = 0; pi < 64; pi = pi + 1) begin
        @(posedge clk_74a);
        bridge_addr <= pbase + pi * 4;
        bridge_rd <= 1;
        repeat (3) @(posedge clk_74a);
        bridge_rd <= 0;
        word = param_rd_data;
        path_bytes[pi*4]   = word[7:0];
        path_bytes[pi*4+1] = word[15:8];
        path_bytes[pi*4+2] = word[23:16];
        path_bytes[pi*4+3] = word[31:24];
      end
      for (pi = 0; pi < 256 && path_bytes[pi] != 0; pi = pi + 1) $write("%c", path_bytes[pi]);
      $write("\n");
      @(posedge clk_74a);
      tgt_ack  <= 0;
      tgt_done <= 1;
    end
  endtask

  // ---------------------------------------------------------- PCM capture
  integer fpcm, pcm_n = 0;
  initial fpcm = $fopen("pcm_out.txt", "w");
  always @(posedge clk_sys) begin
    if (dut.pcm_tick && dut.pcm_level != 0) begin
      $fwrite(fpcm, "%0d %0d\n", $signed(dut.audio_l), $signed(dut.audio_r));
      pcm_n = pcm_n + 1;
      if (pcm_n % 100 == 0) begin $display("  PCM %0d samples t=%0t", pcm_n, $time); $fflush; $fflush(fpcm); end
    end
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
      $display("  [progress] frame_no=%0d t=%0t pcm_n=%0d", frame_no, $time, pcm_n); $fflush;
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
  task press(input [15:0] key);
    begin
      cont1_key = key;
      wait_frames(1);
      cont1_key = 0;
      wait_frames(1);
    end
  endtask

  task wait_frames(input integer n);
    integer target;
    begin
      target = frame_no + n;
      wait (frame_no == target);
    end
  endtask

  initial begin
    repeat (20) @(posedge clk_sys);
    reset_n = 1;

    // tiny library parses in ~2 frames
    wait (frame_no == 6);
    press(16'h0010);  // A → select artist 0, focus main
    press(16'h0010);  // A → open drawer → mp3_start on track 0
    start_capture("out_soc_c.ppm");
    wait_frames(2);

    // let it play: capture enough PCM to prove sustained, correct decode
    wait (pcm_n >= 512);
    $display("PCM captured: %0d samples", pcm_n);
    $fclose(fpcm);

    $display("DONE");
    $finish;
  end

  initial begin
    #1_500_000_000;  // 1.5 s sim-time safety
    $display("FAIL timeout");
    $finish;
  end

endmodule
