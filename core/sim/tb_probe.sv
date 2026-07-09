// Focused probe: watch datatable access + MMIO reads during boot.
`timescale 1ns / 1ps
module tb_probe;
  reg clk_sys=0; always #10.417 clk_sys=~clk_sys;
  reg clk_vid=0; always #41.666 clk_vid=~clk_vid;
  reg clk_74a=0; always #6.734 clk_74a=~clk_74a;
  reg reset_n=0;
  localparam LIB_SIZE=65133;
  wire [9:0] dt_addr;
  wire [31:0] dt_q = (dt_addr == 10'd1) ? LIB_SIZE : 32'd0;
  wire tr, to, tg; wire [15:0] tid; wire [31:0] toff,tba,tlen,tpp,trp,prd;
  pt_soc #(.FIRMWARE_B0("../projects/firmware_b0.hex"),.FIRMWARE_B1("../projects/firmware_b1.hex"),
           .FIRMWARE_B2("../projects/firmware_b2.hex"),.FIRMWARE_B3("../projects/firmware_b3.hex"),
           .PALETTE_FILE("../projects/palette.hex")) dut (
    .clk_sys(clk_sys), .clk_vid(clk_vid), .reset_n(reset_n),
    .clk_74a(clk_74a), .bridge_wr(1'b0), .bridge_rd(1'b0), .bridge_endian_little(1'b0),
    .bridge_addr(32'd0), .bridge_wr_data(32'd0), .param_rd_data(prd),
    .target_dataslot_read(tr), .target_dataslot_openfile(to), .target_dataslot_getfile(tg),
    .target_dataslot_ack(1'b0), .target_dataslot_done(1'b0), .target_dataslot_err(3'd0),
    .target_dataslot_id(tid), .target_dataslot_slotoffset(toff), .target_dataslot_bridgeaddr(tba),
    .target_dataslot_length(tlen), .target_buffer_param_struct(tpp), .target_buffer_resp_struct(trp),
    .datatable_addr(dt_addr), .datatable_q(dt_q),
    .cont1_key(16'd0), .audio_l(), .audio_r(),
    .video_de(), .video_hs(), .video_vs(), .video_rgb());

  // trace CPU MMIO accesses
  always @(posedge clk_sys) begin
    if (dut.mem_valid && dut.mem_ready && dut.mem_addr[31:28]==4'hF) begin
      if (dut.mem_wstrb != 0)
        $display("%0t MMIO WR [%h] = %h", $time, dut.mem_addr[11:0], dut.mem_wdata);
      else
        $display("%0t MMIO RD [%h] -> %h", $time, dut.mem_addr[11:0], dut.mem_rdata);
    end
  end
  always @(dt_addr) $display("%0t dt_addr=%0d", $time, dt_addr);

  initial begin
    repeat(20) @(posedge clk_sys); reset_n=1;
    #60_000_000;  // 60 ms
    $finish;
  end
endmodule
