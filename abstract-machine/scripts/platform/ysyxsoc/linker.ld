ENTRY(_start)

MEMORY {
    /*
    FLASH 区域, 大小 16 MB.
    用于存放 .text 段 (可执行代码) 和 .rodata 段 (只读数据),
    同时也作为 .data 段的加载地址 (LMA),
    .data 段在启动时会被 copy 到 PSRAM 的 VMA 地址.
    */
    flash (rx) : ORIGIN = _flash_start, LENGTH = 16M

    /*
    PSRAM 区域, 大小 4 MB.
    用于存放 .data 段 (VMA), .bss 段,
    以及堆和栈的共用空间.
    堆从 _heap_start (紧跟 .bss 之后) 向上增长,
    栈从 _stack_top (PSRAM 顶部) 向下增长,
    中间剩余空间由堆和栈共享使用.
    */
    psram (rw) : ORIGIN = _psram_start, LENGTH = 4M
}

SECTIONS {
    . = ORIGIN(flash);

    .text : {
        *(entry)
        *(.text*)
    } > flash : text

    etext = .;
    _etext = .;

    .rodata : {
        *(.rodata*)
    } > flash : rodata

    /*
    在切换到 PSRAM VMA 之前, 记录 .data 段在 FLASH 中的 LMA 起始地址.
    启动代码 _data_lma_copy() 会使用这个地址作为拷贝源.
    */
    _data_lma = .;

    . = ORIGIN(psram);

    .data : {
        _data_start = .;
        *(.data*)
        *(.sdata*)
    } > psram AT> flash : data

    _data_end = .;

    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(.scommon)
        _bss_end = .;
    } > psram

    _heap_start = ALIGN(0x1000);

    /* 栈位于 PSRAM 顶部, 预留 32 KB 给堆和栈之间的安全间隔 */
    _stack_top = ORIGIN(psram) + LENGTH(psram);
    _heap_end = _stack_top - 0x8000;
    _stack_pointer = _stack_top;

    end = .;
    _end = .;
}

PHDRS {
    text PT_LOAD;
    rodata PT_LOAD;
    data PT_LOAD;
}
