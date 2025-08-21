#include <utils.h>

bool nemu_iringbuf_init(void) {
    bool success;

    nemu_state.iringbuf = RingBuffer_create(NEMU_IRINGBUF_SIZE, &success);
    return success;
}

void nemu_iringbuf_destroy(void) {
    RingBuffer_destroy(nemu_state.iringbuf);
}

void nemu_iringbuf_dump(void) {
    bstring bstr;
    char *cstr;
    bool success;

    bstr = RingBuffer_gets_all(nemu_iringbuf, &success);
    if (success) {
        cstr = bstr2cstr(bstr, ' ');
        printf("iringbuf data:\n");
        printf("%s\n", cstr);
        bcstrfree(cstr);
        bdestroy(bstr);
    } else {
        printf("failed to get iringbuf data\n");
    }
}
