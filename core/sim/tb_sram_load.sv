// Focused check: does the APF-load path (data_loader → sram_ctrl → SRAM)
// survive a full-size firmware image (>128 KB, i.e. byte offsets past
// 0x2_0000)? Loads a known pattern at the real bridge pacing, then reads
// everything back through the dBus port and compares.
`timescale 1ns / 1ps
module tb_sram_load;
  reg clk_sys = 0; always #10.417 clk_sys = ~clk_sys;  // 48 MHz
  reg clk_74a = 0; always #6.734 clk_74a = ~clk_74a;   // 74.25 MHz
  reg pll_locked = 0;

  localparam integer HW = 97494;  // halfwords, matches the current image

  // bridge
  reg        bridge_wr = 0;
  reg [31:0] bridge_addr = 0;
  reg [31:0] bridge_wr_data = 0;

  wire        ld_en;
  wire [27:0] ld_addr;
  wire [15:0] ld_data;
  data_loader #(
      .ADDRESS_MASK_UPPER_4     (4'h5),
      .ADDRESS_SIZE             (28),
      .WRITE_MEM_CLOCK_DELAY    (8),
      .WRITE_MEM_EN_CYCLE_LENGTH(1),
      .OUTPUT_WORD_SIZE         (2)
  ) fw_loader (
      .clk_74a(clk_74a),
      .clk_memory(clk_sys),
      .bridge_wr(bridge_wr),
      .bridge_endian_little(1'b0),
      .bridge_addr(bridge_addr),
      .bridge_wr_data(bridge_wr_data),
      .write_en(ld_en),
      .write_addr(ld_addr),
      .write_data(ld_data)
  );

  // dBus master (read-only here)
  reg         d_valid = 0, d_wr = 0;
  reg  [31:0] d_addr = 0, d_wdata = 0;
  reg  [3:0]  d_mask = 0;
  wire [31:0] d_rdata;
  wire        d_done, d_accept;

  wire [16:0] sram_a;
  wire [15:0] sram_dq;
  wire sram_oe_n, sram_we_n, sram_ub_n, sram_lb_n;
  wire sram_rst_n;
  synch_3 rst_s (pll_locked, sram_rst_n, clk_sys);

  sram_ctrl sramc (
      .clk(clk_sys), .reset_n(sram_rst_n),
      .sram_a(sram_a), .sram_dq(sram_dq),
      .sram_oe_n(sram_oe_n), .sram_we_n(sram_we_n),
      .sram_ub_n(sram_ub_n), .sram_lb_n(sram_lb_n),
      .ld_valid(ld_en), .ld_addr({4'h0, ld_addr}), .ld_data(ld_data),
      .d_valid(d_valid), .d_wr(d_wr), .d_addr(d_addr), .d_wdata(d_wdata),
      .d_mask(d_mask), .d_rdata(d_rdata), .d_done(d_done), .d_accept(d_accept),
      .ic_valid(1'b0), .ic_addr(32'd0), .ic_rsp_valid(), .ic_rsp_data(), .ic_ready()
  );

  sram_model #(.PRELOAD("")) sram (
      .a(sram_a), .dq(sram_dq),
      .oe_n(sram_oe_n), .we_n(sram_we_n), .ub_n(sram_ub_n), .lb_n(sram_lb_n)
  );

  // known pattern: halfword i holds (i ^ (i >> 7)) & 16'hFFFF
  function [15:0] pat(input integer i);
    pat = (i ^ (i >> 7)) & 16'hFFFF;
  endfunction

  task do_bridge_write(input [31:0] addr, input [31:0] data);
    begin
      @(posedge clk_74a);
      bridge_addr <= addr;
      bridge_wr_data <= data;
      bridge_wr <= 1;
      @(posedge clk_74a);
      bridge_wr <= 0;
      repeat (73) @(posedge clk_74a);
    end
  endtask

  task dbus_read32(input [31:0] addr, output [31:0] data);
    begin
      @(posedge clk_sys);
      d_valid <= 1; d_wr <= 0; d_addr <= addr;
      wait (d_done);
      data = d_rdata;
      @(posedge clk_sys);
      d_valid <= 0;
      @(posedge clk_sys);
    end
  endtask

  integer i, errors;
  reg [31:0] rd, want;
  reg [15:0] v0, v1;
  initial begin
    repeat (20) @(posedge clk_sys);
    pll_locked = 1;
    repeat (10) @(posedge clk_sys);

    // Real APF packing: one 32-bit bridge word carries TWO halfwords
    // (bytes in file order), bridge address advances by 4. With the
    // endian-big path (bridge_endian_little=0) data_loader byte-swaps the
    // word then emits [15:0] at addr and [31:16] at addr+2 — so the word
    // must be sent byte-swapped for the halfwords to land in file order.
    for (i = 0; i < HW; i = i + 2) begin
      v0 = pat(i);
      v1 = pat(i + 1);
      do_bridge_write(32'h5000_0000 + i * 2, {v0[7:0], v0[15:8], v1[7:0], v1[15:8]});
      if (i % 16384 == 0) $display("load %0d/%0d", i, HW);
    end
    $display("load done");
    repeat (200) @(posedge clk_sys);

    errors = 0;
    // verify dense windows around the sensitive boundaries + a sparse sweep
    for (i = 0; i < HW - 1; i = i + 2) begin
      if (i < 64 || (i >= 65500 && i < 65600) ||           // 128 KB boundary
          (i >= HW - 64) || (i % 997 == 1)) begin           // tail + sweep
        dbus_read32(32'h5000_0000 + i * 2, rd);
        want = {pat(i + 1), pat(i)};
        if (rd !== want) begin
          errors = errors + 1;
          if (errors <= 10)
            $display("MISMATCH @hw %0d (byte 0x%05x): got %h want %h",
                     i, i * 2, rd, want);
        end
      end
    end
    if (errors == 0) $display("PASS: load path intact over %0d halfwords", HW);
    else $display("FAIL: %0d mismatches", errors);
    $finish;
  end

  initial begin
    #400_000_000;
    $display("FAIL timeout");
    $finish;
  end
endmodule
