#include <common.h>
#include <utils.h>

static bool query_name_through_symbol_table(char *dest, word_t addr) {
    size_t i;
    Symbol *sym;

    for (i = 0; i < nemu_state.ftrace_func_syms_size; i++) {
        sym = &nemu_state.ftrace_func_syms[i];
        if (sym->addr <= addr && addr < sym->addr + sym->size) {
            strcpy(dest, sym->name);
            return true;
        }
    }

    return false;
}

#ifdef CONFIG_FTRACE

bool nemu_ftrace_record_and_log(CallType type, word_t src_addr, word_t addr) {
    CallStackInfo *stack_top;
    char func_name[256], dest_func_name[256], *buf;
    size_t i;

    buf = nemu_state.ftrace_logbuf;
    buf += strlen(nemu_state.ftrace_logbuf);

    if (type == CALL_TYPE_CALL) {
        /* call 到函数的调用 */
        if (!query_name_through_symbol_table(func_name, addr)) {
            return false;
        }

        // 记录入栈信息：调用至目的函数
        nemu_state.ftrace_available = true;
        buf += sprintf(buf, "[ftrace] " FMT_PADDR ": ", src_addr);
        for (i = 0; i < nemu_state.ftrace_call_stack_top; i++) {
            buf += sprintf(buf, "  ");
        }
        buf += sprintf(buf, "call to [%s@" FMT_PADDR "]\n", func_name, addr);

        // 将该函数入栈
        stack_top = &nemu_state.ftrace_call_stack[nemu_state.ftrace_call_stack_top];
        stack_top->addr = addr;
        strcpy(stack_top->name, func_name);
        nemu_state.ftrace_call_stack_top++;
        
        return true;
    }

    if (type == CALL_TYPE_TAIL) {
        /* tail 从当前函数进行尾调用到另一个函数 */
        if (!query_name_through_symbol_table(func_name, src_addr)) {
            return false;
        }
        if (!query_name_through_symbol_table(dest_func_name, addr)) {
            return false;
        }

        // 先将当前函数出栈
        // 【注意】由于编译器/汇编器可能进行尾调用消除优化，出栈时要出到目标函数层级
        // （可能需要出不止一层栈）
        while (
            nemu_state.ftrace_call_stack_top > 0 &&
            strcmp(nemu_state.ftrace_call_stack[nemu_state.ftrace_call_stack_top - 1].name, dest_func_name) != 0
        ) {
            // 没到达目标层级，则继续出栈
            nemu_state.ftrace_call_stack_top--;
        }

        // 记录信息：尾调用至另一个函数
        nemu_state.ftrace_available = true;
        buf += sprintf(buf, "[ftrace] " FMT_PADDR ": ", src_addr);
        for (i = 0; i < nemu_state.ftrace_call_stack_top; i++) {
            buf += sprintf(buf, "  ");
        }
        buf += sprintf(buf, "tail from [%s@" FMT_PADDR "] to [%s@" FMT_PADDR "]\n", func_name, src_addr, dest_func_name, addr);

        // 再将目的函数入栈
        stack_top = &nemu_state.ftrace_call_stack[nemu_state.ftrace_call_stack_top];
        stack_top->addr = addr;
        strcpy(stack_top->name, dest_func_name);
        nemu_state.ftrace_call_stack_top++;

        return true;
    }

    if (type == CALL_TYPE_RET) {
        /* ret 从当前函数返回 */
        if (!query_name_through_symbol_table(func_name, src_addr)) {
            return false;
        }
        if (!query_name_through_symbol_table(dest_func_name, addr)) {
            strcpy(dest_func_name, "<unknown>");
        }

        // 将当前函数出栈
        // 【注意】由于编译器/汇编器可能进行尾调用消除优化，出栈时要出到目标函数层级
        // （可能需要出不止一层栈）
        if (strcmp(dest_func_name, "<unknown>") == 0) {
            nemu_state.ftrace_call_stack_top--;
        } else {
            while (
                nemu_state.ftrace_call_stack_top > 0 &&
                strcmp(nemu_state.ftrace_call_stack[nemu_state.ftrace_call_stack_top - 1].name, dest_func_name) != 0
            ) {
                // 没到达目标层级，则继续出栈
                nemu_state.ftrace_call_stack_top--;
            }
        }

        // 记录出栈信息：从当前函数返回
        // 【注意】返回到的目的地址不是函数的起始地址，而是在函数体内部
        //        所以不方便记录目的函数信息，只能记录当前函数信息（从哪里返回）
        nemu_state.ftrace_available = true;
        buf += sprintf(buf, "[ftrace] " FMT_PADDR ": ", src_addr);
        for (i = 0; i < nemu_state.ftrace_call_stack_top; i++) {
            buf += sprintf(buf, "  ");
        }
        buf += sprintf(buf, "ret from [%s@" FMT_PADDR "] to [%s@" FMT_PADDR "]\n", func_name, src_addr, dest_func_name, addr);

        return true;
    }

    return false;
}

#endif
