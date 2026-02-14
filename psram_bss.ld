/* Supplementary linker script: map PSRAM_BSS variables to PSRAM.
   The default Arduino ESP32 sections.ld catches .ext_ram.bss inside
   .dram0.bss (line 641).  We use a custom section name .psram_bss
   that is NOT caught by the default script, so our INSERT places it
   into the extern_ram_seg (PSRAM) memory region. */

SECTIONS
{
    .psram_bss (NOLOAD) :
    {
        _psram_bss_start = ABSOLUTE(.);
        *(.psram_bss .psram_bss.*)
        . = ALIGN(4);
        _psram_bss_end = ABSOLUTE(.);
    } > extern_ram_seg
}
INSERT BEFORE .dram0.bss
