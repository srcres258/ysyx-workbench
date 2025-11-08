ENTRY(_start)
PHDRS { text PT_LOAD; rodata PT_LOAD; data PT_LOAD; }

MEMORY {
    /*
    SRAM 区域, 大小 8 KB.
    因为比较小, 所以只有 4 KB 分配给 .data 和 .bss 段,
    剩余部分为堆和栈的共用空间.
    其中堆的起始地址为剩余内存部分的最低地址处, 栈的起始地址为剩余内存部分的最高地址处.
    */
    sram_data_bss : ORIGIN = _sram_start, LENGTH = 4K
    sram_heap_stack : ORIGIN = _sram_start + 4K, LENGTH = 4K

    /*
    MROM 区域, 大小 16 MB.
    也是比较小, 还要同时分配给 .text 和 .rodata 段.
    */
    flash : ORIGIN = _flash_start, LENGTH = 16M
}

SECTIONS {
    . = ORIGIN(sram_heap_stack);
    _heap_start = .;
    . += LENGTH(sram_heap_stack);
    _stack_top = .;
    _stack_pointer = .;

    . = ORIGIN(flash);
    .text : {
        *(entry)
        *(.text*)
    } > flash AT> flash : text
    etext = .;
    _etext = .;
    .rodata : {
        *(.rodata*)
    } > flash AT> flash : rodata
    _data_start = .;
    .data : {
        *(.data*)
    } > sram_data_bss AT> flash : data
    _data_end = .;
    _data_len = _data_end - _data_start;
    .bss : {
        _bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(.scommon)
    } > sram_data_bss AT> flash
    . = LENGTH(flash);
    end = .;
    _end = .;
}
