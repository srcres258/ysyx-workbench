ENTRY(_start)

MEMORY {
    /*
    FLASH 区域, 大小 16 MB.
    存放 .boot (bootloader, VMA=LMA), .rodata (只读数据, VMA=LMA),
    同时作为 .text / .data 段的加载地址 (LMA).
    bootloader 在启动时将 .text / .data 从 FLASH copy 到 PSRAM,
    然后跳转到 PSRAM 执行.  .rodata 保持 XIP 从 FLASH 读取.
    */
    flash (rx) : ORIGIN = _flash_start, LENGTH = 16M

    /*
    SRAM 区域, 大小 8 KB.
    仅存放栈空间. 栈从 SRAM 顶部向下增长,
    利用 SRAM 的低延迟特性提升函数调用和局部变量访问效率.
    */
    sram  (rwx): ORIGIN = _sram_start,  LENGTH = 8K

    /*
    PSRAM 区域, 大小 4 MB.
    存放 .text (VMA), .data (VMA), .bss (VMA),
    以及堆空间. 这是程序指令执行时的运行地址
    (被 bootloader 从 FLASH copy 过来).
    */
    psram (rwx): ORIGIN = _psram_start, LENGTH = 4M
}

SECTIONS {
    /*
    1. FLASH 中的段 (VMA = LMA = FLASH):
       - .boot:  bootloader 代码, NPC 复位后最先执行
       - .rodata: 只读数据 (printf 格式字符串等), XIP 从 FLASH 读取
    */
    . = ORIGIN(flash);

    .boot : {
        KEEP(*(.boot))
    } > flash : boot

    .rodata : {
        *(.rodata*)
    } > flash : rodata

    /*
    2. 切换到 PSRAM VMA, 后续段 (.text / .data / .bss) 的
       VMA 均在 PSRAM 中, LMA 则在 FLASH 中 (紧接 .rodata 之后).
    */
    . = ORIGIN(psram);

    .text : {
        _text_start = .;
        *(.text*)
        _text_end = .;
    } > psram AT> flash : text

    _text_lma = LOADADDR(.text);

    .data : {
        _data_start = .;
        *(.data*)
        *(.sdata*)
        _data_end = .;
    } > psram AT> flash : data

    _data_lma = LOADADDR(.data);

    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(.scommon)
        _bss_end = .;
    } > psram

    /* 堆从 PSRAM 中紧接 .bss 的位置开始 (向上增长) */
    _heap_start = ALIGN(0x1000);

    /* 堆结束于 PSRAM 末尾, 为程序提供最大 4MB 的堆空间 */
    _heap_end = ORIGIN(psram) + LENGTH(psram);

    /* 栈位于 SRAM 顶部 (向下增长), 充分利用 SRAM 的低延迟 */
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
