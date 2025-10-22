ENTRY(_start)
PHDRS { text PT_LOAD; data PT_LOAD; }

MEMORY {
    /*
    SRAM 区域, 大小 8 KB.
    因为比较小, 所以只有 2 KB 分配给 .data 和 .bss 段,
    剩余部分为堆和栈的共用空间.
    其中堆的起始地址为剩余内存部分的最低地址处, 栈的起始地址为剩余内存部分的最高地址处.
    */
    sram_data_bss : ORIGIN = _sram_start, LENGTH = 2K
    sram_heap_stack : ORIGIN = _sram_start + 2K, LENGTH = 6K

    /*
    MROM 区域, 大小 4 KB.
    也是比较小, 还要同时分配给 .text 和 .rodata 段.
    */
    mrom : ORIGIN = _mrom_start, LENGTH = 4K
}

SECTIONS {
    . = ORIGIN(sram_heap_stack);
    _heap_start = .;
    . += LENGTH(sram_heap_stack);
    _stack_top = .;
    _stack_pointer = .;

    . = ORIGIN(mrom);
    .text : {
        *(entry)
        *(.text*)
    } > mrom AT> mrom : text
    etext = .;
    _etext = .;
    .rodata : {
        *(.rodata*)
    }
    _data_start = .;
    .data : {
        *(.data*)
    } > sram_data_bss AT> mrom : data
    _data_end = .;
    _data_len = _data_end - _data_start;
    .bss : {
        _bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(.scommon)
    } > sram_data_bss AT> mrom
    . = LENGTH(mrom);
    end = .;
    _end = .;
}
