// Unit test: APF bridge writes → data_loader(16-bit) → sram_ctrl → SRAM model.
// Proves the boot path byte order end-to-end: a file byte stream written over
// the bridge must land so that the CPU (little-endian) reads the right words.
//
// The bridge delivers big-endian 32-bit words: file bytes {b0,b1,b2,b3} arrive
// as bridge_wr_data = {b0,b1,b2,b3}. The CPU later expects word = {b3,b2,b1,b0}
// (b0 = LSB). This TB writes a known pattern and checks both halfword layout in
// the SRAM model and the 32-bit value sram_ctrl's dbus port returns.
`default_nettype none
`timescale 1ns/1ps
module tb_sramload;
  reg clk_74a = 0, clk_sys = 0, reset_n = 0;
  always #6.734 clk_74a = ~clk_74a;
  always #6.944 clk_sys = ~clk_sys;   // 72 MHz

  // bridge → loader
  reg         bridge_wr = 0;
  reg  [31:0] bridge_addr = 0, bridge_wr_data = 0;
  wire        ld_en;
  wire [27:0] ld_addr;
  wire [15:0] ld_data;

  data_loader #(
      .ADDRESS_MASK_UPPER_4(4'h5),
      .ADDRESS_SIZE(28),
      .WRITE_MEM_CLOCK_DELAY(4),
      .WRITE_MEM_EN_CYCLE_LENGTH(1),
      .OUTPUT_WORD_SIZE(2)
  ) loader (
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

  // sram_ctrl + model
  wire [16:0] sram_a;
  wire [15:0] sram_dq;
  wire sram_oe_n, sram_we_n, sram_ub_n, sram_lb_n;
  reg         d_valid = 0, d_wr = 0;
  reg  [31:0] d_addr = 0;
  wire        d_accept, d_done;
  wire [31:0] d_rdata;

  sram_ctrl ctrl (
      .clk(clk_sys), .reset_n(reset_n),
      .sram_a(sram_a), .sram_dq(sram_dq),
      .sram_oe_n(sram_oe_n), .sram_we_n(sram_we_n),
      .sram_ub_n(sram_ub_n), .sram_lb_n(sram_lb_n),
      .ld_valid(ld_en), .ld_addr({4'h0, ld_addr}), .ld_data(ld_data),
      .d_valid(d_valid), .d_accept(d_accept), .d_done(d_done),
      .d_wr(d_wr), .d_addr(d_addr), .d_wdata(32'h0), .d_mask(4'h0),
      .d_rdata(d_rdata),
      .ic_valid(1'b0), .ic_ready(), .ic_addr(32'h0),
      .ic_rsp_valid(), .ic_rsp_data()
  );

  sram_model sram (
      .a(sram_a), .dq(sram_dq),
      .oe_n(sram_oe_n), .we_n(sram_we_n), .ub_n(sram_ub_n), .lb_n(sram_lb_n)
  );

  integer errors = 0;
  task bridge_write(input [31:0] addr, input [31:0] data);
    begin
      @(posedge clk_74a);
      bridge_addr <= addr;
      bridge_wr_data <= data;
      bridge_wr <= 1;
      @(posedge clk_74a);
      bridge_wr <= 0;
      repeat (75) @(posedge clk_74a);  // real APF pacing
    end
  endtask

  initial begin
    repeat (5) @(posedge clk_sys);
    reset_n = 1;
    repeat (3) @(posedge clk_74a);

    // file bytes 00..07 at offset 0: two big-endian bridge words
    bridge_write(32'h5000_0000, 32'h00_01_02_03);
    bridge_write(32'h5000_0004, 32'h04_05_06_07);
    repeat (30) @(posedge clk_sys);

    // SRAM halfword layout: CPU little-endian → mem[0] must be {b1,b0}=0x0100
    if (sram.mem[0] !== 16'h0100) begin errors=errors+1; $display("FAIL mem[0]=%04x (want 0100)", sram.mem[0]); end
    if (sram.mem[1] !== 16'h0302) begin errors=errors+1; $display("FAIL mem[1]=%04x (want 0302)", sram.mem[1]); end
    if (sram.mem[2] !== 16'h0504) begin errors=errors+1; $display("FAIL mem[2]=%04x (want 0504)", sram.mem[2]); end

    // dbus 32-bit read of word 0: CPU must see 0x03020100
    d_addr = 32'h5000_0000; d_valid = 1;
    wait (d_accept); @(posedge clk_sys); d_valid = 0;
    wait (d_done);
    if (d_rdata !== 32'h03020100) begin errors=errors+1; $display("FAIL d_rdata=%08x (want 03020100)", d_rdata); end
    @(posedge clk_sys);

    if (errors == 0) $display("PASS: loader->SRAM byte order correct for a little-endian CPU");
    else $display("FAIL: %0d error(s)", errors);
    $finish;
  end

  initial begin #300000 $display("FAIL timeout"); $finish; end
endmodule
