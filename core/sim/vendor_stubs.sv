// Behavioral stubs for Intel/Altera primitives, ONLY for iverilog lint/sim.
// Never include these in the Quartus build.

`timescale 1ns / 1ps

// altera_pll: passthrough-ish stub, enough to elaborate pt_pll.
module altera_pll #(
    parameter fractional_vco_multiplier = "true",
    parameter reference_clock_frequency = "0 MHz",
    parameter operation_mode = "normal",
    parameter number_of_clocks = 1,
    parameter output_clock_frequency0 = "0 MHz",
    parameter phase_shift0 = "0 ps",
    parameter duty_cycle0 = 50,
    parameter output_clock_frequency1 = "0 MHz",
    parameter phase_shift1 = "0 ps",
    parameter duty_cycle1 = 50,
    parameter output_clock_frequency2 = "0 MHz",
    parameter phase_shift2 = "0 ps",
    parameter duty_cycle2 = 50,
    parameter output_clock_frequency3 = "0 MHz",
    parameter phase_shift3 = "0 ps",
    parameter duty_cycle3 = 50,
    parameter output_clock_frequency4 = "0 MHz",
    parameter phase_shift4 = "0 ps",
    parameter duty_cycle4 = 50,
    parameter output_clock_frequency5 = "0 MHz",
    parameter phase_shift5 = "0 ps",
    parameter duty_cycle5 = 50,
    parameter output_clock_frequency6 = "0 MHz",
    parameter phase_shift6 = "0 ps",
    parameter duty_cycle6 = 50,
    parameter output_clock_frequency7 = "0 MHz",
    parameter phase_shift7 = "0 ps",
    parameter duty_cycle7 = 50,
    parameter output_clock_frequency8 = "0 MHz",
    parameter phase_shift8 = "0 ps",
    parameter duty_cycle8 = 50,
    parameter output_clock_frequency9 = "0 MHz",
    parameter phase_shift9 = "0 ps",
    parameter duty_cycle9 = 50,
    parameter output_clock_frequency10 = "0 MHz",
    parameter phase_shift10 = "0 ps",
    parameter duty_cycle10 = 50,
    parameter output_clock_frequency11 = "0 MHz",
    parameter phase_shift11 = "0 ps",
    parameter duty_cycle11 = 50,
    parameter output_clock_frequency12 = "0 MHz",
    parameter phase_shift12 = "0 ps",
    parameter duty_cycle12 = 50,
    parameter output_clock_frequency13 = "0 MHz",
    parameter phase_shift13 = "0 ps",
    parameter duty_cycle13 = 50,
    parameter output_clock_frequency14 = "0 MHz",
    parameter phase_shift14 = "0 ps",
    parameter duty_cycle14 = 50,
    parameter output_clock_frequency15 = "0 MHz",
    parameter phase_shift15 = "0 ps",
    parameter duty_cycle15 = 50,
    parameter output_clock_frequency16 = "0 MHz",
    parameter phase_shift16 = "0 ps",
    parameter duty_cycle16 = 50,
    parameter output_clock_frequency17 = "0 MHz",
    parameter phase_shift17 = "0 ps",
    parameter duty_cycle17 = 50,
    parameter pll_type = "General",
    parameter pll_subtype = "General"
) (
    input  wire        rst,
    input  wire        refclk,
    input  wire        fbclk,
    output wire [17:0] outclk,
    output wire        fboutclk,
    output wire        locked
);
  assign outclk   = {18{refclk}};
  assign fboutclk = refclk;
  assign locked   = ~rst;
endmodule

// altsyncram: dual-port RAM model sized for mf_datatable (32-bit x 1K).
module altsyncram (
    address_a, address_b, clock0, clock1, data_a, data_b, wren_a, wren_b,
    q_a, q_b, aclr0, aclr1, addressstall_a, addressstall_b, byteena_a,
    byteena_b, clocken0, clocken1, clocken2, clocken3, eccstatus, rden_a, rden_b
);
  parameter address_reg_b = "CLOCK1";
  parameter clock_enable_input_a = "BYPASS";
  parameter clock_enable_input_b = "BYPASS";
  parameter clock_enable_output_a = "BYPASS";
  parameter clock_enable_output_b = "BYPASS";
  parameter indata_reg_b = "CLOCK1";
  parameter init_file = "NONE";
  parameter intended_device_family = "Cyclone V";
  parameter lpm_type = "altsyncram";
  parameter numwords_a = 1024;
  parameter numwords_b = 1024;
  parameter operation_mode = "BIDIR_DUAL_PORT";
  parameter outdata_aclr_a = "NONE";
  parameter outdata_aclr_b = "NONE";
  parameter outdata_reg_a = "UNREGISTERED";
  parameter outdata_reg_b = "UNREGISTERED";
  parameter power_up_uninitialized = "FALSE";
  parameter read_during_write_mode_port_a = "NEW_DATA_NO_NBE_READ";
  parameter read_during_write_mode_port_b = "NEW_DATA_NO_NBE_READ";
  parameter widthad_a = 10;
  parameter widthad_b = 10;
  parameter width_a = 32;
  parameter width_b = 32;
  parameter width_byteena_a = 1;
  parameter width_byteena_b = 1;
  parameter wrcontrol_wraddress_reg_b = "CLOCK1";

  input wire [widthad_a-1:0] address_a;
  input wire [widthad_b-1:0] address_b;
  input wire clock0, clock1;
  input wire [width_a-1:0] data_a;
  input wire [width_b-1:0] data_b;
  input wire wren_a, wren_b;
  output reg [width_a-1:0] q_a;
  output reg [width_b-1:0] q_b;
  input wire aclr0, aclr1, addressstall_a, addressstall_b;
  input wire byteena_a, byteena_b;
  input wire clocken0, clocken1, clocken2, clocken3;
  output wire eccstatus;
  input wire rden_a, rden_b;

  reg [width_a-1:0] mem[0:numwords_a-1];
  assign eccstatus = 0;

  always @(posedge clock0) begin
    if (wren_a) mem[address_a] <= data_a;
    q_a <= mem[address_a];
  end
  always @(posedge clock1) begin
    if (wren_b) mem[address_b] <= data_b;
    q_b <= mem[address_b];
  end
endmodule

// dcfifo: minimal 4-deep dual-clock FIFO model (enough for data_loader).
module dcfifo (
    data,
    rdclk,
    rdreq,
    wrclk,
    wrreq,
    q,
    rdempty,
    aclr,
    wrfull,
    wrempty,
    rdfull,
    rdusedw,
    wrusedw,
    eccstatus
);
  parameter clocks_are_synchronized = "FALSE";
  parameter intended_device_family = "Cyclone V";
  parameter lpm_numwords = 4;
  parameter lpm_showahead = "OFF";
  parameter lpm_type = "dcfifo";
  parameter lpm_width = 36;
  parameter lpm_widthu = 2;
  parameter overflow_checking = "OFF";
  parameter rdsync_delaypipe = 5;
  parameter underflow_checking = "OFF";
  parameter use_eab = "OFF";
  parameter wrsync_delaypipe = 5;

  input wire [lpm_width-1:0] data;
  input wire rdclk, rdreq, wrclk, wrreq;
  output reg [lpm_width-1:0] q;
  output wire rdempty;
  input wire aclr;
  output wire wrfull, wrempty, rdfull;
  output wire [lpm_widthu-1:0] rdusedw, wrusedw;
  output wire eccstatus;

  reg [lpm_width-1:0] fifo_mem[0:lpm_numwords-1];
  reg [lpm_widthu:0] wr_ptr = 0, rd_ptr = 0;

  assign rdempty = (wr_ptr == rd_ptr);
  assign wrempty = rdempty;
  assign wrfull = ((wr_ptr - rd_ptr) == lpm_numwords[lpm_widthu:0]);
  assign rdfull = wrfull;
  assign rdusedw = wr_ptr - rd_ptr;
  assign wrusedw = wr_ptr - rd_ptr;
  assign eccstatus = 0;

  always @(posedge wrclk)
    if (wrreq) begin
      fifo_mem[wr_ptr[lpm_widthu-1:0]] <= data;
      wr_ptr <= wr_ptr + 1'b1;
    end

  always @(posedge rdclk)
    if (rdreq) begin
      q <= fifo_mem[rd_ptr[lpm_widthu-1:0]];
      rd_ptr <= rd_ptr + 1'b1;
    end
endmodule
