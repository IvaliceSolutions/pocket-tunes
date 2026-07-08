// LINT-ONLY stub of Analogue core_bridge_cmd (real file uses decl-after-use, which iverilog rejects)
// Port list extracted verbatim from core_bridge_cmd.v — outputs tied off.
module core_bridge_cmd (

input   wire            clk,
output  reg             reset_n,

input   wire            bridge_endian_little,
input   wire    [31:0]  bridge_addr,
input   wire            bridge_rd,
output  reg     [31:0]  bridge_rd_data,
input   wire            bridge_wr,
input   wire    [31:0]  bridge_wr_data,

// all these signals should be synchronous to clk
// add synchronizers if these need to be used in other clock domains
input   wire            status_boot_done,           // assert when PLLs lock and logic is ready
input   wire            status_setup_done,          // assert when core is happy with what's been loaded into it
input   wire            status_running,             // assert when pocket's taken core out of reset and is running

output  reg             dataslot_requestread,
output  reg     [15:0]  dataslot_requestread_id,
input   wire            dataslot_requestread_ack,
input   wire            dataslot_requestread_ok,

output  reg             dataslot_requestwrite,
output  reg     [15:0]  dataslot_requestwrite_id,
output  reg     [31:0]  dataslot_requestwrite_size,
input   wire            dataslot_requestwrite_ack,
input   wire            dataslot_requestwrite_ok,

output  reg             dataslot_update,
output  reg     [15:0]  dataslot_update_id,
output  reg     [31:0]  dataslot_update_size,

output  reg             dataslot_allcomplete,

output  reg     [31:0]  rtc_epoch_seconds,
output  reg     [31:0]  rtc_date_bcd,
output  reg     [31:0]  rtc_time_bcd,
output  reg             rtc_valid,

input   wire            savestate_supported,
input   wire    [31:0]  savestate_addr,
input   wire    [31:0]  savestate_size,
input   wire    [31:0]  savestate_maxloadsize,

output  reg             osnotify_inmenu,

output  reg             savestate_start,        // core should detect rising edge on this,
input   wire            savestate_start_ack,    // and then assert ack for at least 1 cycle
input   wire            savestate_start_busy,   // assert constantly while in progress after ack
input   wire            savestate_start_ok,     // assert continuously when done, and clear when new process is started
input   wire            savestate_start_err,    // assert continuously on error, and clear when new process is started

output  reg             savestate_load,
input   wire            savestate_load_ack,
input   wire            savestate_load_busy,
input   wire            savestate_load_ok,
input   wire            savestate_load_err,

input   wire            target_dataslot_read,       // rising edge triggered
input   wire            target_dataslot_write,
input   wire            target_dataslot_getfile,
input   wire            target_dataslot_openfile,

output  reg             target_dataslot_ack,        // asserted upon command start until completion
output  reg             target_dataslot_done,       // asserted upon command finish until next command is issued
output  reg     [2:0]   target_dataslot_err,        // contains result of command execution. zero is OK

input   wire    [15:0]  target_dataslot_id,         // parameters for each of the read/reload/write commands
input   wire    [31:0]  target_dataslot_slotoffset,
input   wire    [31:0]  target_dataslot_bridgeaddr,
input   wire    [31:0]  target_dataslot_length,

input   wire    [31:0]  target_buffer_param_struct, // bus address of the memory region APF will fetch additional parameter struct from
input   wire    [31:0]  target_buffer_resp_struct,  // bus address of the memory region APF will write its response struct to
                                                    // this should be mapped by the developer, the buffer is not implemented in this file

input   wire    [9:0]   datatable_addr,
input   wire            datatable_wren,
input   wire    [31:0]  datatable_data,
output  wire    [31:0]  datatable_q

);

endmodule
