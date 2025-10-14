ENTRY(_start)
PHDRS { text PT_LOAD; data PT_LOAD; }

SECTIONS {
    /*
    SRAM 区域, 大小 8 KB.
    因为比较小, 所以只有 2 KB 分配给 .data 和 .bss 段,
    剩余部分为堆和栈的共用空间.
    其中堆的起始地址为剩余内存部分的最低地址处, 栈的起始地址为剩余内存部分的最高地址处.
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
    _heap_start = _sram_start + 2 * 1024;
    . =  _sram_start + 8 * 1024;
    _stack_top = .;
    _stack_pointer = .;

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
    . = _mrom_start + 4 * 1024;
    end = .;
    _end = .;
}
