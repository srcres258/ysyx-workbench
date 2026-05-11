/*
 * ysyxSoC Second Stage Bootloader (SSBL)
 *
 * Runs from SRAM (loaded there by FSBL in start.S).
 * Responsible for loading the application from FLASH into PSRAM,
 * then jumping to _trm_init to begin execution.
 *
 * Design constraints:
 *   - Must NOT call functions that reside in PSRAM (not yet loaded).
 *   - Must NOT use global/static variables from other compilation
 *     units (those are in PSRAM, not yet loaded or initialized).
 *   - Uses only inline functions (riscv.h) and local stack variables.
 *   - Uses 32-bit word copies (lw/sw equivalent) to avoid potential
 *     byte-write bugs in the CPU RTL.
 */

#include <stdint.h>
#include <riscv/riscv.h>

/* Linker-defined symbols for the application sections.
 * _start / _end: VMA in PSRAM (destination).
 * _lma: LMA in FLASH (source). */
extern char _text_start, _text_end, _text_lma;
extern char _data_start, _data_end, _data_lma;
extern char _bss_start, _bss_end;

/* Forward declaration of the TRM initialization entry point,
 * which lives in PSRAM after SSBL loads it there. */
void _trm_init(void);

/*
 * Copy `words` 32-bit words from `src` to `dst`.
 * Uses volatile uint32_t* to prevent compiler optimization from
 * reordering/eliding the copy loop. Equivalent to the lw/sw loop
 * in start.S, preserving 32-bit aligned word access to avoid
 * potential byte-write issues in the CPU RTL's SRAM path.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
static void copy_words(volatile uint32_t *dst, volatile const uint32_t *src,
                       uint32_t words) {
    for (uint32_t i = 0; i < words; i++) {
        dst[i] = src[i];
    }
}
#pragma GCC diagnostic pop

/*
 * SSBL entry point. Called by FSBL (start.S) after loading
 * the .ssbl section from FLASH to SRAM.
 *
 * Stack is already set up in SRAM by FSBL.
 *
 * The -Warray-bounds suppression below is necessary because the
 * linker-defined symbols (_text_start, etc.) are declared as
 * `extern char` (size 1) but represent base addresses of multi-byte
 * regions. This is a well-known pattern in embedded bootloaders and
 * bare-metal runtime init code that copies sections between memory
 * regions.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
void ssbl_entry(void) {
    uint32_t len_words;

    /* --- 1. Copy .text section from FLASH (LMA) to PSRAM (VMA) --- */
    len_words = ((uint32_t)(uintptr_t)&_text_end -
                 (uint32_t)(uintptr_t)&_text_start) / 4;
    copy_words((volatile uint32_t *)(uintptr_t)&_text_start,
               (volatile const uint32_t *)(uintptr_t)&_text_lma,
               len_words);

    /* --- 2. Copy .data section from FLASH (LMA) to PSRAM (VMA) --- */
    len_words = ((uint32_t)(uintptr_t)&_data_end -
                 (uint32_t)(uintptr_t)&_data_start) / 4;
    copy_words((volatile uint32_t *)(uintptr_t)&_data_start,
               (volatile const uint32_t *)(uintptr_t)&_data_lma,
               len_words);

    /* --- 3. Zero .bss section in PSRAM --- */
    {
        volatile uint8_t *p;
        for (p = (volatile uint8_t *)(uintptr_t)&_bss_start;
             p < (volatile uint8_t *)(uintptr_t)&_bss_end;
             p++) {
            *p = 0;
        }
    }

    /* --- 4. Jump to _trm_init, now resident in PSRAM --- */
    _trm_init();

    /* Should never reach here. */
    while (1) {
        /* spin */
    }
}
#pragma GCC diagnostic pop
