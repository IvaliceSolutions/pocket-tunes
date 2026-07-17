/* M7d memory layout — code in the 256 KB external async SRAM, the hot
 * CELT decode kernels in a 32 KB instruction TCM (1-cycle BRAM), data in the
 * 128 KB BRAM.
 *
 * The whole firmware image (text + .itcm + rodata + .data init) is one flat
 * blob loaded into SRAM by the APF "Firmware" data slot at boot. crt0 (running
 * from SRAM through the I-cache) copies .rodata+.data to the data BRAM, the
 * .itcm image to the instruction TCM, and zeros .bss.
 *
 * Why .itcm: an I-cache miss refills a 32 B line from the 55 ns SRAM in ~80
 * clk_sys cycles; libopus' CELT decode working set thrashes the 4 KB cache, so
 * the decoder ran below real time. The profile-dominant kernels (kiss_fft,
 * mdct, celt deemphasis/comb, the range decoder, the PVQ decode) live here and
 * refill from BRAM at ~1 cycle/word — the misses on the hot path become cheap.
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY {
  SRAM (rx)  : ORIGIN = 0x50000000, LENGTH = 256K
  ITCM (rwx) : ORIGIN = 0x60000000, LENGTH = 32K
  RAM  (rwx) : ORIGIN = 0x00000000, LENGTH = 128K
}
SECTIONS {
  /* .text.init (crt0 _start) MUST lead: SRAM origin 0x5000_0000 is the reset
   * vector. The hot Opus objects are excluded here and claimed by .itcm below;
   * .text keeps the LMA at SRAM origin so the reset vector stays crt0. */
  .text : {
    *(.text.init)
    EXCLUDE_FILE(
      *opus_kiss_fft.o *opus_mdct.o *opus_celt.o *opus_entdec.o
      *opus_entcode.o *opus_mathops.o *opus_laplace.o *opus_vq.o *opus_cwrs.o
      *opus_celt_decoder.o
    ) *(.text*)
  } > SRAM

  /* Hot CELT-decode objects: run from the 1-cycle instruction TCM, load image
   * trailing the .text image in SRAM (crt0 copies it to ITCM at boot). */
  .itcm : {
    __itcm_start = .;
    *opus_kiss_fft.o(.text .text.*)
    *opus_mdct.o(.text .text.*)
    *opus_celt.o(.text .text.*)
    *opus_entdec.o(.text .text.*)
    *opus_entcode.o(.text .text.*)
    *opus_mathops.o(.text .text.*)
    *opus_laplace.o(.text .text.*)
    *opus_vq.o(.text .text.*)
    *opus_cwrs.o(.text .text.*)
    *opus_celt_decoder.o(.text .text.*)
    __itcm_end = .;
  } > ITCM AT> SRAM
  __itcm_lma = LOADADDR(.itcm);

  /* Opus cold tables: SRAM-resident, read uncached via the dBus */
  .sramdata : { *(.sram.rodata*) } > SRAM

  /* fast rodata + data: run from BRAM, load image in SRAM (crt0 copies) */
  .rodata : { __fast_start = .; *(.rodata*) *(.srodata*) } > RAM AT> SRAM
  .data   : { *(.data*) *(.sdata*) ; __fast_end = .; } > RAM AT> SRAM
  __fast_lma = LOADADDR(.rodata);

  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(.sbss*) *(COMMON) ; __bss_end = .; } > RAM

  _stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
