#include <nvboard.h>
#include "Vtop.h"

static Vtop top;

void nvboard_bind_all_pins(Vtop *top);

int main() {
    nvboard_bind_all_pins(&top);
    nvboard_init();

    while (1) {
        nvboard_update();
        top.eval();
    }

    return 0;
}