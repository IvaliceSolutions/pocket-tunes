/* M7a memory layout — code in the 256 KB external async SRAM, data in the
 * 128 KB BRAM (1-cycle, hot DSP state).
 *
 * The whole firmware image (text + rodata + .data init) is one flat blob
 * loaded into SRAM by the APF "Firmware" data slot at boot. crt0 (running
 * from SRAM through the I-cache) copies .rodata+.data to BRAM and zeros
 * .bss, so all data accesses stay 1-cycle exactly as before M7a.
 * Opus's big cold tables will stay SRAM-resident in M7b (separate section).
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY {
  SRAM (rx)  : ORIGIN = 0x50000000, LENGTH = 256K
  RAM  (rwx) : ORIGIN = 0x00000000, LENGTH = 128K
}
SECTIONS {
  .text : { *(.text.init) *(.text*) } > SRAM

  /* Opus cold tables (M7b): SRAM-resident, read uncached via the dBus */
  .sramdata : { *(.sram.rodata*) } > SRAM

  /* fast rodata + data: run from BRAM, load image in SRAM (crt0 copies) */
  .rodata : { __fast_start = .; *(.rodata*) *(.srodata*) } > RAM AT> SRAM
  .data   : { *(.data*) *(.sdata*) ; __fast_end = .; } > RAM AT> SRAM
  __fast_lma = LOADADDR(.rodata);

  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(.sbss*) *(COMMON) ; __bss_end = .; } > RAM

  _stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
