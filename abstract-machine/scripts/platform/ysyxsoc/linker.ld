ENTRY(_start)
PHDRS { text PT_LOAD; data PT_LOAD; }

SECTIONS {
    /*
    SRAM 区域, 大小 8 KB.
    因为比较小, 所以只有 2 KB 分配给 .data 和 .bss 段,
    剩余部分为堆和栈的共用空间.
    其中栈给 2 KB, 剩下的都给堆.
    */
    . = _sram_start;
    .data : {
        *(.data*)
    } : data
    edata = .;
    _data = .;
    .bss : {
        _bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(.scommon)
    }
    _stack_top = ALIGN(2 * 1024);
    . = _stack_top + 2 * 1024;
    _stack_pointer = .;
    _heap_start = .;

    /*
    MROM 区域, 大小 4 KB.
    也是比较小, 还要同时分配给 .text 和 .rodata 段.
    */
    . = _mrom_start;
    .text : {
        *(entry)
        *(.text*)
    } : text
    etext = .;
    _etext = .;
    .rodata : {
        *(.rodata*)
    }
    . = ALIGN(4 * 1024);
    end = .;
    _end = .;
}
