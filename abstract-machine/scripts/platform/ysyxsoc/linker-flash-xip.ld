ENTRY(_start)

/* ===================================================================
 * ysyxSoC Flash XIP (eXecute In Place) Linker Script
 *
 * Memory Map:
 *   FLASH (0x30000000, 16MB): .boot, .rodata, .text (all XIP),
 *                              .data LMA (loaded to SRAM by bootloader)
 *   SRAM  (0x0f000000, 8KB):  .data VMA, stack
 *   PSRAM (0x80000000, 4MB):  .bss VMA, heap
 *
 * Boot Flow (single-stage):
 *   1. Bootloader (.boot in FLASH) sets up stack in SRAM,
 *      copies .data from FLASH LMA to SRAM VMA, zeroes .bss in PSRAM,
 *      jumps to _trm_init
 *   2. Application .text executes directly from FLASH (XIP)
 *
 * Key difference from linker.ld (SRAM-native):
 *   .text stays in FLASH (VMA = LMA = FLASH) instead of being
 *   copied to SRAM, enabling programs with large code footprints
 *   that would otherwise overflow the 8KB SRAM.
 *   .bss moved to PSRAM for programs with large uninitialized data
 *   (e.g., am-tests GPU framebuffer).
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
     * Holds .data VMA and stack.
     */
    sram  (rwx): ORIGIN = _sram_start,  LENGTH = 8K

    /*
     * PSRAM: 4 MB, large read/write memory.
     * Holds .bss and heap for programs with large data footprints.
     */
    psram (rwx): ORIGIN = _psram_start, LENGTH = 4M
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
     * 2. SRAM-resident section: .data (VMA = SRAM, LMA = FLASH)
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

    /*
     * ================================================================
     * 3. PSRAM-resident section: .bss (NOLOAD, zeroed by bootloader)
     * ================================================================
     * .bss is placed in PSRAM to accommodate large uninitialized
     * data (e.g., GPU framebuffers, audio buffers) that would
     * otherwise overflow the 8KB SRAM.
     */
    . = ORIGIN(psram);

    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(.scommon)
        _bss_end = .;
    } > psram

    /*
     * ================================================================
     * 4. Heap and Stack
     * ================================================================
     * Heap: page-aligned after .bss in PSRAM, grows upward.
     * PSRAM provides 4MB total space for large heaps.
     *
     * Stack: at top of SRAM, grows downward.
     * Using SRAM for the stack leverages its low-latency access
     * for function calls and local variables.
     */
    _heap_start = ALIGN(0x1000);
    _heap_end = ORIGIN(psram) + LENGTH(psram);

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
