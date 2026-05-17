ENTRY(_start)

/* ===================================================================
 * ysyxSoC Flash XIP (eXecute In Place) Linker Script
 *
 * Memory Map:
 *   FLASH (0x30000000, 16MB): .boot, .rodata, .text (all XIP),
 *                              .data LMA (loaded to SRAM by bootloader)
 *   SRAM  (0x0f000000, 8KB):  .data VMA, .bss VMA, stack, heap
 *
 * Boot Flow (single-stage):
 *   1. Bootloader (.boot in FLASH) sets up stack in SRAM,
 *      copies .data from FLASH LMA to SRAM VMA, zeroes .bss,
 *      jumps to _trm_init
 *   2. Application .text executes directly from FLASH (XIP)
 *
 * Key difference from linker.ld (SRAM-native):
 *   .text stays in FLASH (VMA = LMA = FLASH) instead of being
 *   copied to SRAM, enabling programs with large code footprints
 *   that would otherwise overflow the 8KB SRAM.
 * =================================================================== */

MEMORY {
    /*
     * FLASH: 16 MB, read-only + executable at runtime (XIP).
     * Holds bootloader (.boot), read-only data (.rodata),
     * program code (.text), and the load image of .data.
     */
    flash (rx) : ORIGIN = _flash_start, LENGTH = 16M

    /*
     * SRAM: 8 KB, fast read/write.
     * Holds .data VMA, .bss, stack, and optionally heap.
     */
    sram  (rwx): ORIGIN = _sram_start,  LENGTH = 8K
}

SECTIONS {
    /*
     * ================================================================
     * 1. FLASH-resident sections (VMA = LMA = FLASH, XIP)
     * ================================================================
     */
    . = ORIGIN(flash);

    /* Bootloader: single-stage minimal startup, executed in-place */
    .boot : {
        KEEP(*(.boot))
    } > flash : boot

    /* Read-only data: format strings, const globals, accessed via XIP */
    .rodata : {
        *(.rodata*)
    } > flash : rodata

    /* Program code: executed directly from FLASH (XIP) */
    .text : {
        _text_start = .;
        *(.text*)
        _text_end = .;
    } > flash : text

    /*
     * ================================================================
     * 2. SRAM-resident sections (VMA = SRAM, LMA = FLASH)
     * ================================================================
     * .data is stored in FLASH (LMA) and copied to SRAM (VMA) by the
     * bootloader at startup.  objcopy -O binary uses p_paddr (LMA),
     * so the generated .bin is a contiguous FLASH image.
     */
    . = ORIGIN(sram);

    .data : {
        _data_start = .;
        *(.data*)
        *(.sdata*)
        _data_end = .;
    } > sram AT> flash : data

    _data_lma = LOADADDR(.data);

    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(.scommon)
        _bss_end = .;
    } > sram

    /*
     * ================================================================
     * 3. Heap and Stack (in SRAM)
     * ================================================================
     * Heap: page-aligned after .bss.  Defaults to empty (heap_end ==
     * heap_start) to reserve the remaining SRAM for the stack.  Programs
     * needing a large heap should use PSRAM or SDRAM mode instead.
     *
     * Stack: at top of SRAM, grows downward.
     */
    _heap_start = ALIGN(0x1000);
    _heap_end = _heap_start;

    _stack_top = ORIGIN(sram) + LENGTH(sram);
    _stack_pointer = _stack_top;

    end = .;
    _end = .;
}

PHDRS {
    boot   PT_LOAD;
    rodata PT_LOAD;
    text   PT_LOAD;
    data   PT_LOAD;
}
