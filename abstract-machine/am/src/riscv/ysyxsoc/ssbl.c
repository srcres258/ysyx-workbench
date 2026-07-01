/*
 * ysyxSoC Second Stage Bootloader (SSBL)
 *
 * Runs from SRAM (loaded there by FSBL in start.S).
 * Responsible for loading the application from FLASH into PSRAM/SDRAM,
 * then jumping to _trm_init to begin execution.
 *
 * Design constraints:
 *   - Must NOT call functions that reside in PSRAM/SDRAM (not yet loaded).
 *   - Must NOT use global/static variables from other compilation
 *     units (those are in PSRAM/SDRAM, not yet loaded or initialized).
 *   - Uses only inline functions (riscv.h) and local stack variables.
 *   - Uses 32-bit word copies (lw/sw equivalent) to avoid potential
 *     byte-write bugs in the CPU RTL.
 *
 * UART logging is provided by self-contained static inline MMIO
 * functions. All logging code and string literals are linked into
 * the .ssbl section in SRAM, never touching PSRAM/SDRAM before
 * those regions are loaded.
 */

#include <stdint.h>
#include <riscv/riscv.h>

/* ================================================================
 * UART 16550 Minimum Driver (polling TX, write-only)
 * Base: 0x1000_0000 (SoC.scala:34)
 * SysClock: 10 MHz -> Divisor = 10_000_000 / (16 * 115_200) ≈ 5
 * ================================================================ */

/* UART 16550 Registers (via volatile pointer to base + offset) */
#define UART_BASE   ((volatile uint8_t *)0x10000000UL)
#define UART_DLL    UART_BASE[0]   /* Divisor Latch Low (DLAB=1) / THR (DLAB=0) */
#define UART_DLM    UART_BASE[1]   /* Divisor Latch High (DLAB=1) / IER (DLAB=0) */
#define UART_FCR    UART_BASE[2]   /* FIFO Control */
#define UART_LCR    UART_BASE[3]   /* Line Control (bit 7: DLAB) */
#define UART_LSR    UART_BASE[5]   /* Line Status (bit 5: THR empty) */

/* Minimal UART init: 115200 8N1, TX-only configuration
 * SysClock 10MHz -> Divisor = 10_000_000 / (16 * 115_200) ≈ 5 */
static void uart_init(void) {
    /* Step 1: Enable Divisor Latch Access (LCR bit 7 = 1) */
    UART_LCR = 0x80;

    /* Step 2: Write divisor (DLL=5, DLM=0, little-endian) */
    UART_DLL = 5;     /* Divisor Latch Low */
    UART_DLM = 0;     /* Divisor Latch High */

    /* Step 3: Set 8N1 (8 data bits, no parity, 1 stop bit), close DLAB */
    UART_LCR = 0x03;

    /* Step 4: Disable FIFO (power-on default is disabled; explicit clear) */
    UART_FCR = 0x00;
}

/* Output a single character to UART (poll LSR bit 5 for THR empty)
 * Ref: trm.c putch() — delay loop removed for faster SSBL output */
static void uart_putc(char ch) {
    while (!(UART_LSR & 0x20)) {
        /* Wait until Transmit Holding Register is empty */
    }
    UART_DLL = ch;  /* DLL and THR share offset 0 */
}

/* Output a null-terminated C string */
static void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

/* Output uint32_t as fixed 8-digit hexadecimal
 * Ref: mem-test.c print_hex_nibbles() */
static void uart_puthex(uint32_t val) {
    static const char hex[] = "0123456789abcdef";
    int shift;
    for (shift = 28; shift >= 0; shift -= 4) {
        uart_putc(hex[(val >> shift) & 0xf]);
    }
}

/* Output small integer (0~65535) in decimal
 * Uses power-of-10 subtraction to avoid libgcc division calls
 * (those may link to PSRAM .text, unreachable during SSBL)
 * Ref: mem-test.c print_small_int() style — digit-by-digit output */
static void uart_putdec(uint32_t val) {
    /* Precomputed powers of 10 (uint16_t range: at most 5 digits) */
    static const uint32_t pow10[] = {10000, 1000, 100, 10, 1};
    int started = 0;
    int i;

    if (val == 0) {
        uart_putc('0');
        return;
    }

    for (i = 0; i < 5; i++) {
        uint32_t digit = 0;
        while (val >= pow10[i]) {
            val -= pow10[i];
            digit++;
        }
        if (digit > 0 || started) {
            uart_putc('0' + (char)digit);
            started = 1;
        }
    }
}

/* ================================================================
 * Progress Bar (ref: mem-test.c progress_dot style)
 * ================================================================ */

/* Progress config: one '.' every 256 words (1KB), 64 dots per line */
#define PROGRESS_STEP       256
#define PROGRESS_DOTS_WRAP  64

/* Output a single progress dot, auto-wrapping lines */
static void progress_dot(uint32_t *pline) {
    uart_putc('.');
    if (++(*pline) >= PROGRESS_DOTS_WRAP) {
        uart_puts("\r\n      ");
        *pline = 0;
    }
}

/* Conditionally emit a progress dot when words_done reaches next_dot threshold */
static void progress_tick(uint32_t words_done, uint32_t *next_dot,
                          uint32_t *pline) {
    if (words_done >= *next_dot) {
        progress_dot(pline);
        *next_dot += PROGRESS_STEP;
    }
}

/* ================================================================
 * Section Copy (word granularity + tail-byte fix + progress bar)
 * ================================================================ */

/* Linker-defined symbols for the application sections.
 * _start / _end: VMA in PSRAM/SDRAM (destination).
 * _lma: LMA in FLASH (source). */
extern char _text_start, _text_end, _text_lma;
extern char _data_start, _data_end, _data_lma;
extern char _bss_start, _bss_end;

/* Forward declaration of the TRM initialization entry point,
 * which lives in PSRAM/SDRAM after SSBL loads it there. */
void _trm_init(void);

/* Copy `words` 32-bit words from `src` to `dst` with dot progress bar.
 * Parameters:
 *   dst    Destination address (PSRAM/SDRAM VMA)
 *   src    Source address (FLASH LMA)
 *   words  32-bit word count
 * All progress variables are local stack variables — no global state. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
static void copy_words_with_progress(volatile uint32_t *dst,
                                     volatile const uint32_t *src,
                                     uint32_t words) {
    uint32_t next_dot = PROGRESS_STEP;
    uint32_t pline = 0;
    uint32_t i;

    for (i = 0; i < words; i++) {
        dst[i] = src[i];
        progress_tick(i + 1, &next_dot, &pline);
    }

    /* Newline after copy completes (if mid-line) */
    if (pline > 0) {
        uart_puts("\r\n");
    }
}
#pragma GCC diagnostic pop

static void copy_tail_bytes(volatile uint8_t *dst,
                            volatile const uint8_t *src,
                            uint32_t bytes) {
    while (bytes--) {
        *dst++ = *src++;
    }
}

/* ================================================================
 * SSBL Entry — Load + Log Output
 * ================================================================ */

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
    uint32_t src_addr, dst_addr;
    uint32_t text_bytes, data_bytes;

    /* --- UART Initialization --- */
    uart_init();

    /* --- Startup Banner --- */
    uart_puts("\r\n========================================\r\n");
    uart_puts("ysyxSoC SSBL (Second Stage Bootloader)\r\n");
    uart_puts("  UART: 115200 8N1 @ 0x10000000\r\n");
    uart_puts("========================================\r\n\r\n");

    /* ================================================================
     * 1. Copy .text section: FLASH LMA -> PSRAM/SDRAM VMA
     * ================================================================ */
    len_words = ((uint32_t)(uintptr_t)&_text_end -
                 (uint32_t)(uintptr_t)&_text_start) / 4;
    src_addr  = (uint32_t)(uintptr_t)&_text_lma;
    dst_addr  = (uint32_t)(uintptr_t)&_text_start;
    text_bytes = (uint32_t)(uintptr_t)&_text_end -
                 (uint32_t)(uintptr_t)&_text_start;

    uart_puts("[.text   ] ");
    uart_puthex(src_addr);
    uart_puts(" -> ");
    uart_puthex(dst_addr);
    uart_puts("  ");
    uart_putdec(len_words);
    uart_puts(" words\r\n      ");

    copy_words_with_progress((volatile uint32_t *)dst_addr,
                             (volatile const uint32_t *)src_addr,
                             len_words);
    copy_tail_bytes((volatile uint8_t *)(uintptr_t)(dst_addr + len_words * 4),
                    (volatile const uint8_t *)(uintptr_t)(src_addr + len_words * 4),
                    text_bytes & 0x3);
    uart_puts("      done\r\n\r\n");

    /* ================================================================
     * 2. Copy .data section: FLASH LMA -> PSRAM/SDRAM VMA
     * ================================================================ */
    len_words = ((uint32_t)(uintptr_t)&_data_end -
                 (uint32_t)(uintptr_t)&_data_start) / 4;
    src_addr  = (uint32_t)(uintptr_t)&_data_lma;
    dst_addr  = (uint32_t)(uintptr_t)&_data_start;
    data_bytes = (uint32_t)(uintptr_t)&_data_end -
                 (uint32_t)(uintptr_t)&_data_start;

    if (len_words > 0) {
        uart_puts("[.data   ] ");
        uart_puthex(src_addr);
        uart_puts(" -> ");
        uart_puthex(dst_addr);
        uart_puts("  ");
        uart_putdec(len_words);
        uart_puts(" words\r\n      ");

        copy_words_with_progress((volatile uint32_t *)dst_addr,
                                 (volatile const uint32_t *)src_addr,
                                 len_words);
        copy_tail_bytes((volatile uint8_t *)(uintptr_t)(dst_addr + len_words * 4),
                        (volatile const uint8_t *)(uintptr_t)(src_addr + len_words * 4),
                        data_bytes & 0x3);
        uart_puts("      done\r\n\r\n");
    } else {
        uart_puts("[.data   ] (empty, skipped)\r\n\r\n");
    }

    /* ================================================================
     * 3. Zero-initialize .bss section (PSRAM/SDRAM)
     * ================================================================ */
    // NOTE: 此处暂时 comment out .bss section 的清零操作以换取仿真速度.
    //       后续待 NPC 性能优化进行后/体系结构仿真后台逻辑优化后, 再考虑恢复此处的 .bss 清零操作.
    //       但需要注意, 不清零 .bss section 可能会导致 undefined behavior 从而造成潜在的软件程序非预期行为.
    // {
    //     uint32_t bss_bytes = (uint32_t)(uintptr_t)&_bss_end -
    //                          (uint32_t)(uintptr_t)&_bss_start;
    //     if (bss_bytes > 0) {
    //         volatile uint8_t *p;
    //         uint32_t next_dot = PROGRESS_STEP;
    //         uint32_t pline = 0;
    //         uint32_t done = 0;

    //         uart_puts("[.bss    ] ");
    //         uart_puthex((uint32_t)(uintptr_t)&_bss_start);
    //         uart_puts(" ~ ");
    //         uart_puthex((uint32_t)(uintptr_t)&_bss_end);
    //         uart_puts("  ");
    //         uart_putdec(bss_bytes);
    //         uart_puts(" bytes\r\n      ");

    //         for (p = (volatile uint8_t *)(uintptr_t)&_bss_start;
    //              p < (volatile uint8_t *)(uintptr_t)&_bss_end;
    //              p++) {
    //             *p = 0;
    //             done++;
    //             if (done >= next_dot) {
    //                 progress_dot(&pline);
    //                 next_dot += PROGRESS_STEP;
    //             }
    //         }

    //         if (pline > 0) {
    //             uart_puts("\r\n");
    //         }
    //         uart_puts("      done\r\n\r\n");
    //     } else {
    //         uart_puts("[.bss    ] (empty, skipped)\r\n\r\n");
    //     }
    // }

    /* ================================================================
     * 4. Jump to Application
     * ================================================================ */
    uart_puts("========================================\r\n");
    uart_puts("SSBL: Jumping to application...\r\n");
    uart_puts("========================================\r\n\r\n");

    _trm_init();

    /* Should never reach here */
    while (1) {
        /* spin */
    }
}
#pragma GCC diagnostic pop
