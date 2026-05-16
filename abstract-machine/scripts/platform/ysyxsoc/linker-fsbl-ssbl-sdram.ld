ENTRY(_start)

/* ===================================================================
 * ysyxSoC Two-Stage Bootloader (FSBL + SSBL) Linker Script — SDRAM Edition
 *
 * Memory Map:
 *   FLASH (0x30000000, 16MB): .boot (FSBL), .rodata (XIP),
 *                              .ssbl LMA, .text LMA, .data LMA
 *   SRAM  (0x0f000000, 8KB):  .ssbl VMA (SSBL code+data), stack
 *   SDRAM (0xa0000000, 32MB): .text VMA, .data VMA, .bss VMA, heap
 *
 * Boot Flow:
 *   1. FSBL (.boot in FLASH) copies .ssbl from FLASH->SRAM, jumps to ssbl_entry
 *   2. SSBL (.ssbl in SRAM)  copies .text/.data from FLASH->SDRAM,
 *                             zeroes .bss, jumps to _trm_init
 *   3. Application runs from SDRAM
 *
 * Differs from linker-fsbl-ssbl.ld (PSRAM edition) only in:
 *   - sdram region replaces psram region (0xa0000000 vs 0x80000000)
 *   - 32MB size replaces 4MB size
 *   - Heap grows into the full 32MB SDRAM space
 * =================================================================== */

MEMORY {
    /*
     * FLASH: 16 MB, read-only at runtime (XIP).
     * Holds FSBL (.boot), read-only data (.rodata), and load images
     * of SSBL (.ssbl LMA) and application (.text/.data LMA).
     */
    flash (rx) : ORIGIN = _flash_start, LENGTH = 16M

    /*
     * SRAM: 8 KB, fast read/write.
     * Holds SSBL code+data (.ssbl VMA) and the stack (grows down from top).
     * Limited to 8KB -> SSBL must be kept small.
     */
    sram  (rwx): ORIGIN = _sram_start,  LENGTH = 8K

    /*
     * SDRAM: 32 MB, large read/write memory.
     * Holds application .text/.data/.bss and heap.
     * Address: 0xa0000000, per ysyxSoC address map (SoC.scala:46).
     */
    sdram (rwx): ORIGIN = _sdram_start, LENGTH = 32M
}

SECTIONS {
    /*
     * ================================================================
     * 1. FLASH-resident sections (VMA = LMA = FLASH)
     * ================================================================
     */
    . = ORIGIN(flash);

    /* FSBL: First Stage Bootloader, executed in-place from FLASH */
    .boot : {
        KEEP(*(.boot))
    } > flash : boot

    /* Read-only data: kept in FLASH for XIP access */
    .rodata : {
        *(EXCLUDE_FILE(*ssbl.o) .rodata .rodata.*)
    } > flash : rodata

    /*
     * ================================================================
     * 2. SRAM-resident section: SSBL (VMA = SRAM, LMA = FLASH)
     * ================================================================
     * SSBL code+data is stored in FLASH but loaded to SRAM by FSBL.
     * All sections from ssbl.o are collected here exclusively.
     */
    . = ORIGIN(sram);

    .ssbl : {
        _ssbl_start = .;
        *ssbl.o(.text .text.* .rodata .rodata.* .data .data.* .sdata .sdata.*)
        *ssbl.o(.bss .bss.* .sbss .sbss.* .scommon)
        . = ALIGN(4);
        _ssbl_end = .;
    } > sram AT> flash : ssbl

    _ssbl_lma = LOADADDR(.ssbl);

    /*
     * ================================================================
     * 3. SDRAM-resident sections: Application (VMA = SDRAM, LMA = FLASH)
     * ================================================================
     * The application (.text, .data, .bss, heap) runs from SDRAM after
     * being loaded from FLASH by SSBL.
     * EXCLUDE_FILE(*ssbl.o) ensures ssbl.o content stays in .ssbl above.
     */
    . = ORIGIN(sdram);

    .text : {
        _text_start = .;
        *(EXCLUDE_FILE(*ssbl.o) .text .text.*)
        _text_end = .;
    } > sdram AT> flash : text

    _text_lma = LOADADDR(.text);

    .data : {
        _data_start = .;
        *(EXCLUDE_FILE(*ssbl.o) .data .data.* .sdata .sdata.*)
        _data_end = .;
    } > sdram AT> flash : data

    _data_lma = LOADADDR(.data);

    .bss (NOLOAD) : {
        _bss_start = .;
        *(EXCLUDE_FILE(*ssbl.o) .bss .bss.* .sbss .sbss.* .scommon)
        _bss_end = .;
    } > sdram

    /*
     * ================================================================
     * 4. Heap and Stack
     * ================================================================
     */
    /* Heap: starts page-aligned after .bss in SDRAM, grows upward.
     * SDRAM provides 32MB total space, giving ample room for large heaps. */
    _heap_start = ALIGN(0x1000);
    _heap_end = ORIGIN(sdram) + LENGTH(sdram);

    /* Stack: at top of SRAM (after SSBL code), grows downward.
     * Using SRAM for the stack leverages its low-latency access
     * for function calls and local variables. */
    _stack_top = ORIGIN(sram) + LENGTH(sram);
    _stack_pointer = _stack_top;

    end = .;
    _end = .;
}

PHDRS {
    boot   PT_LOAD;
    rodata PT_LOAD;
    ssbl   PT_LOAD;
    text   PT_LOAD;
    data   PT_LOAD;
}