#include <iostream>
#include <cstring>
#include <string>
#include <vector>
#include <regex>
#include <memory>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <isa.hpp>
#include <sim_top.hpp>
#include <utils.hpp>
#include <device/io/mmio.hpp>
#include <sdb.hpp>
#include <tui/tui_events.hpp>
#include "npc/simulator_impl.hpp"

#define NR_WP 32

// ---------- 监视点相关 ----------

/**
 * @brief 监视点池。
 */
static WatchPoint wpPool[NR_WP] = {};
/**
 * @brief 正在被使用的监视点链表头。
 */
static WatchPoint *wpHead = nullptr;
/**
 * @brief 空闲的监视点链表头。
 */
static WatchPoint *wpFree = nullptr;

/**
 * @brief 初始化监视点池。
 */
static void initWPPool() {
    int i;
    WatchPoint *wp;

    for (i = 0; i < NR_WP; i++) {
        wp = &wpPool[i];
        wp->no = i;
        wp->next = i == NR_WP - 1 ? nullptr : &wpPool[i + 1];
        wp->expr = nullptr;
        wp->expr_ast = nullptr;
        wp->cond_ast = nullptr;
        wp->val = sdb_value_make(0, sizeof(word_t) * 8, false);
        wp->evaluated = false;
        wp->enabled = true;
        wp->temporary = false;
        wp->hit_count = 0;
        wp->ignore_count = 0;
        wp->stop_count = 0;
    }

    wpHead = nullptr;
    wpFree = wpPool;
}

/**
 * @brief 打印监视点池信息。
 */
static void printWPPool() {
    WatchPoint *cur;

    cur = wpHead;
    std::cout << "Watchpoints:" << std::endl;
    if (cur) {
        while (cur) {
            std::cout << "Watchpoint " << cur->no << ": "
                << (cur->expr ? cur->expr : "<null>") << std::endl;
            std::cout << "Value: " << sdb_value_as_i64(cur->val)
                << ", Evaluated: " << cur->evaluated
                << ", Enabled: " << cur->enabled << std::endl;
            cur = cur->next;
        }
    } else {
        std::cout << "No watchpoint at present." << std::endl;
    }
}

/**
 * @brief 从监视点池分配监视点。
 *
 * @return WatchPoint* 新分配的监视点；若无空闲监视点则返回空指针
 */
static WatchPoint *newWP() {
    WatchPoint *result, *cur;

    if (!wpFree) {
        std::cerr << "Error: no free watchpoint." << std::endl;
        return nullptr;
    }

    result = wpFree;
    wpFree = wpFree->next;
    result->next = nullptr;
    result->expr = nullptr;
    result->expr_ast = nullptr;
    result->cond_ast = nullptr;
    result->val = sdb_value_make(0, sizeof(word_t) * 8, false);
    result->evaluated = false;
    result->enabled = true;
    result->temporary = false;
    result->hit_count = 0;
    result->ignore_count = 0;
    result->stop_count = 0;
    if (wpHead) {
        for (cur = wpHead; cur->next; cur = cur->next);
        cur->next = result;
    } else {
        wpHead = result;
    }

    return result;
}

/**
 * @brief 释放来自监视点池的监视点。
 *
 * @param wp 要释放的监视点
 */
static void freeWP(WatchPoint *wp) {
    WatchPoint *cur, *prev;

    if (!wp) {
        return;
    }
    if (!wpHead) {
        return;
    }

    prev = nullptr;
    for (cur = wpHead; cur; cur = cur->next) {
        if (cur == wp) {
            if (prev) {
                prev->next = cur->next;
            } else {
                wpHead = cur->next;
            }
            cur->next = nullptr;
            break;
        }
    }
    if (!cur) {
        return;
    }
    free(wp->expr);
    sdb_expr_free(wp->expr_ast);
    sdb_expr_free(wp->cond_ast);
    if (wpFree) {
        for (cur = wpFree; cur->next; cur = cur->next);
        cur->next = wp;
    } else {
        wpFree = wp;
    }
}

// ---------- SDB相关 ----------

static void printBadArguments() {
    std::cerr << "Bad command arguments. Please type 'help' for usage." << std::endl;
}

/* 定义 SDB 的所有命令 */

static int cmd_c(char *args) {
    npc::internal::simExecImpl(static_cast<uint64_t>(-1));

    return 0;
}

static int cmd_q(char *args) {
    sim_state.state = SIM_QUIT;

    return -1;
}

static int cmd_help(char *args);

/**
 * @brief 单步执行
 *
 * 让程序单步执行N条指令后暂停执行,
 * 当N没有给出时, 缺省为1
 *
 * 格式：si [N]
 *
 * 使用举例：si 10
 *
 * @param args 指令的条数
 * @return int 始终返回0
 */
static int cmd_si(char *args) {
    int n;

    try {
        n = (args && strlen(args) > 0) ? std::stoi(args) : 1;
        npc::internal::simExecImpl(static_cast<uint64_t>(n));
    } catch (const std::invalid_argument &e) {
        std::cout << "Error: invalid argument for 'si' command." << std::endl;
    }

    return 0;
}

/**
 * @brief 单步执行 (时钟周期)
 *
 * 让程序单步执行N个时钟周期后暂停执行,
 * 当N没有给出时, 缺省为1
 *
 * 格式：si [N]
 *
 * 使用举例：si 10
 *
 * @param args 指令的条数
 * @return int 始终返回0
 */
static int cmd_sic(char *args) {
    int n;

    try {
        n = (args && strlen(args) > 0) ? std::stoi(args) : 1;
        npc::internal::simExecClockPeriodImpl(static_cast<uint64_t>(n));
    } catch (const std::invalid_argument &e) {
        std::cout << "Error: invalid argument for 'si' command." << std::endl;
    }

    return 0;
}

/**
 * @brief 打印程序状态
 *
 * 打印寄存器状态
 * 打印监视点信息
 *
 * 格式：info SUBCMD
 *
 * 使用举例：
 * info r
 * info w
 *
 * @param args
 * @return int 始终返回0
 */
static int cmd_info(char *args) {
    if (args) {
        if (strcmp(args, "r") == 0) {
            isaRegDisplay();
            return 0;
        } else if (strcmp(args, "w") == 0) {
            printWPPool();
            return 0;
        }
    }
    printBadArguments();
    return 0;
}

/**
 * @brief 扫描内存
 *
 * 求出表达式EXPR的值, 将结果作为起始内存
 * 地址, 以十六进制形式输出连续的N个4字节
 *
 * 注：GDB相比, 我们在这里做了简化, 更改了命令的格式
 *
 * 格式：x N EXPR
 *
 * 使用举例：x 10 $esp
 *
 * @param args
 * @return int
 */
static int cmd_x(char *args) {
    char *N_str, *EXPR_str;
    addr_t addr, cur_addr;
    int N, i;
    uint32_t value;
    bool success;
    SdbValue memv = {0};

    if (!args) {
        printBadArguments();
        return 0;
    }

    N_str = strtok(args, " ");
    EXPR_str = N_str ? N_str + strlen(N_str) + 1 : nullptr; // 去掉前缀"0x"
    addr = sdb_expr(EXPR_str, &success);
    N = std::stoi(N_str);

    if (!success || N <= 0) {
        printBadArguments();
        return 0;
    }

    printf("Memory scan: addr=0x%08x, N=%d\n", addr, N);
    cur_addr = addr;
    for (i = 0; i < N; i++) {
        if (!sdb_npc_target_ops()->read_memory(
            nullptr, SDB_ADDR_MEM, cur_addr, 32, false, false, &memv, nullptr
        )) {
            printf("0x%08X: N/A\n", cur_addr);
        } else {
            value = (uint32_t)sdb_value_as_u64(memv);
            printf("0x%08X: %08X\n", cur_addr, value);
        }
        cur_addr += 4;
    }

    return 0;
}

/**
 * @brief 表达式求值
 *
 * 求出表达式EXPR的值
 *
 * 格式：p EXPR
 *
 * 使用举例：p $eax + 1
 *
 * @param args
 * @return int
 */
static int cmd_p(char *args) {
    bool success;
    SdbEvalResult result{};

    if (!args) {
        printBadArguments();
        return 0;
    }

    if (sdb_expr_eval_text(args, sdb_npc_target_ops(), &result)) {
        std::cout << "$1 = " << std::dec << sdb_value_as_i64(result.value)
            << std::endl;
        std::cout << "unsigned = " << std::dec << sdb_value_as_u64(result.value)
            << std::endl;
        std::cout << "hex = 0x" << std::hex << sdb_value_as_u64(result.value)
            << std::dec << std::endl;
    } else {
        std::cout << "求值失败，请检查您输入的表达式是否有误！" << std::endl;
    }

    return 0;
}

/**
 * @brief 设置监视点
 *
 * 当表达式EXPR的值发生变化时, 暂停程序执行
 *
 * 格式：w EXPR
 *
 * 使用举例：w *0x2000
 *
 * @param args
 * @return int
 */
static int cmd_w(char *args) {
    WatchPoint *wp;
    SdbError err{};

    if (!args) {
        std::cout << "请给定要进行监视的表达式的内容！" << std::endl;
        return 0;
    }
    wp = sdb_newWP();
    if (!wp) {
        std::cout << "Error: no free watchpoint." << std::endl;
        return 0;
    }
    wp->expr = new char[std::strlen(args) + 1];
    std::strcpy(wp->expr, args);
    wp->expr_ast = sdb_expr_parse(wp->expr, &err);
    if (wp->expr_ast) {
        SdbEvalResult result{};
        if (sdb_expr_eval(wp->expr_ast, sdb_npc_target_ops(), &result)) {
            wp->val = result.value;
            wp->evaluated = true;
            std::cout << "成功设置监视点" << wp->no << "，内容为：" << wp->expr
                << "，初始值为：" << sdb_value_as_i64(wp->val) << std::endl;
        } else {
            std::cout << "成功设置监视点" << wp->no << "，内容为：" << wp->expr
                << "，此时无法求值。" << std::endl;
        }
    } else {
        std::cout << "监视点表达式解析失败：" << err.message << std::endl;
        sdb_freeWP(wp);
    }

    return 0;
}

/**
 * @brief 删除监视点
 *
 * 删除序号为N的监视点
 *
 * 格式：d N
 *
 * 使用举例：d 2
 *
 * @param args
 * @return int
 */
static int cmd_d(char *args) {
    WatchPoint *wp;
    int no;

    if (!args) {
        std::cout << "请给定要删除的监视点编号！" << std::endl;
        return 0;
    }
    no = std::stoi(args);
    wp = sdb_findWP(no);
    if (wp) {
        sdb_freeWP(wp);
        std::cout << "监视点" << no << "删除成功！" << std::endl;
    } else {
        std::cout << "未找到监视点" << no << "，删除失败！" << std::endl;
    }

    return 0;
}

static struct {
    const char *name;
    const char *description;
    int (*handler) (char *);
} cmd_table[] = {
    { "help", "Display information about all supported commands", cmd_help },
    { "c", "Continue the execution of the program", cmd_c },
    { "q", "Exit simulation", cmd_q },
    { "si", "Run the given number of instructions of the program and pause", cmd_si },
    { "sic", "Run the given number of clock periods of the program and pause", cmd_sic },
    { "info", "Display information about registers or watchpoints", cmd_info },
    { "x", "Display the contents of memory", cmd_x },
    { "p", "Evaluate an expression and display the result", cmd_p },
    { "w", "Set a watchpoint on an expression", cmd_w },
    { "d", "Delete a watchpoint", cmd_d }
};

#define NR_CMD ARRLEN(cmd_table)

static int cmd_help(char *args) {
    // Extract the first argument.
    char *arg = strtok(NULL, " ");
    int i;

    if (!arg) {
        // No argument given.
        for (i = 0; i < NR_CMD; i++) {
            std::cout << cmd_table[i].name << " - " << cmd_table[i].description
                << std::endl;
        }
    } else {
        for (i = 0; i < NR_CMD; i++) {
            if (strcmp(arg, cmd_table[i].name) == 0) {
                std::cout << cmd_table[i].name << " - " << cmd_table[i].description
                    << std::endl;
                return 0;
            }
        }
        std::cout << "Unknown command: " << arg << std::endl;
    }

    return 0;
}

// ---------- 函数实现 ----------

/**
 * @brief 从标准输入流读取一行。
 *
 * @return std::string 读取到的一行内容（不包含末尾换行符）
 */
static std::string readLine() {
    std::string r;

    std::getline(std::cin, r);

    return std::move(r);
}

/**
 * @brief 从标准输入流读取一行命令输入（在读取前先输出命令提示符）。
 *
 * @return std::string 读取到的一行内容（不包含末尾换行符）
 */
static std::string readCmdInput() {
    std::cout << "(npc) " << std::flush;

    return std::move(readLine());
}

/**
 * @brief SDB：对表达式求值。
 *
 * @param e 表达式字符串
 * @param success 是否成功
 * @return word_t 该表达式的求值结果
 */
word_t sdb_expr(const char *e, bool *success) {
    SdbEvalResult result{};
    if (!sdb_expr_eval_text(e, sdb_npc_target_ops(), &result)) {
        *success = false;
        return 0;
    }
    *success = true;
    return static_cast<word_t>(sdb_value_as_u64(result.value));
}

/**
 * @brief SDB：添加监视点。
 *
 * @return WatchPoint* 新添加的监视点；若监视点已用尽则返回空指针
 */
WatchPoint *sdb_newWP() {
    WatchPoint *result, *cur;

    if (!wpFree) {
        std::cerr << "Error: no free watchpoint." << std::endl;
        return nullptr;
    }

    result = wpFree;
    wpFree = wpFree->next;
    result->next = nullptr;
    result->evaluated = false;
    if (wpHead) {
        for (cur = wpHead; cur->next; cur = cur->next);
        cur->next = result;
    } else {
        wpHead = result;
    }

    return result;
}

/**
 * @brief SDB：删除监视点。
 *
 * @param wp 要删除的监视点
 */
void sdb_freeWP(WatchPoint *wp) {
    WatchPoint *cur, *prev;

    if (!wp || !wpHead) {
        return;
    }

    for (
        prev = nullptr, cur = wpHead;
        cur;
        prev = cur, cur = cur->next
    ) {
        if (cur == wp) {
            if (prev) {
                prev->next = cur->next;
            } else {
                wpHead = cur->next;
            }
            cur->next = nullptr;
            break;
        }
    }
    if (!cur) {
        return;
    }
    delete[] wp->expr;
    wp->expr = nullptr;
    sdb_expr_free(wp->expr_ast);
    sdb_expr_free(wp->cond_ast);
    wp->expr_ast = nullptr;
    wp->cond_ast = nullptr;
    if (wpFree) {
        for (cur = wpFree; cur->next; cur = cur->next);
        cur->next = wp;
    } else {
        wpFree = wp;
    }
}

/**
 * @brief SDB：查找指定编号的监视点。
 *
 * @param no 监视点编号
 * @return WatchPoint* 监视点指针；若未找到返回空指针
 */
WatchPoint *sdb_findWP(int no) {
    WatchPoint *cur;

    for (cur = wpHead; cur; cur = cur->next) {
        if (cur->no == no) {
            break;
        }
    }

    return cur;
}

/**
 * @brief SDB：对所有监视点求值并更新监视点状态。
 * 该函数应在处理器每执行一次后调用一次。
 */
void sdb_evalAndUpdateWP() {
    WatchPoint *cur;
    SdbEvalResult result{};

    for (cur = wpHead; cur; cur = cur->next) {
        if (!cur->enabled || !cur->expr_ast) {
            continue;
        }
        if (!sdb_expr_eval(cur->expr_ast, sdb_npc_target_ops(), &result)) {
            continue;
        }
        if (cur->evaluated && result.value.bits != cur->val.bits) {
            sim_state.state = SIM_STOP;
            std::cout << "Watchpoint " << cur->no << " triggered: "
                << (cur->expr ? cur->expr : "<null>") << std::endl;
            std::cout << "Old value: " << std::dec << sdb_value_as_i64(cur->val)
                << ", new value: " << sdb_value_as_i64(result.value) << std::endl;
            tui::g_eventFeed.push(getExecCount(), tui::EventType::WATCHPOINT,
                                  simExecInfo.pc, static_cast<word_t>(cur->no),
                                  cur->expr ? cur->expr : "<null>");
        }
        cur->val = result.value;
        cur->evaluated = true;
    }
}

/**
 * @brief SDB：初始化。执行主循环前必须调用一次此函数！
 */
void sdb_init() {
    // 初始化监视点池
    initWPPool();
}

/**
 * @brief SDB：执行主循环。
 */
void sdb_mainLoop() {
    int i;

    for (;;) {
        std::string cmdStr = readCmdInput();
        std::unique_ptr<char []> cStr(new char[cmdStr.length() + 1]);
        memset(cStr.get(), 0, cmdStr.length() + 1);
        strcpy(cStr.get(), cmdStr.c_str());
        char *cmd = strtok(cStr.get(), " ");
        if (!cmd) {
            continue;
        }
        /*
        treat the remaining string as the arguments,
        which may need further parsing
        */
        char *args = cmd + strlen(cmd) + 1;

        for (i = 0; i < NR_CMD; i++) {
            if (strcmp(cmd, cmd_table[i].name) == 0) {
                if (cmd_table[i].handler(args) < 0) {
                    return;
                }
                break;
            }
        }

        if (i == NR_CMD) {
            std::cout << "Unknown command: " << cmd << std::endl;
        }
    }
}

// ── TUI‑overlay string‑returning command execution ──

std::string sdb_cmdInfoRegs() {
    std::ostringstream oss;
    auto *oldBuf = std::cout.rdbuf(oss.rdbuf());
    isaRegDisplay();
    std::cout.rdbuf(oldBuf);
    return oss.str();
}

std::string sdb_cmdInfoWatchpoints() {
    std::ostringstream oss;
    oss << "Watchpoints:" << std::endl;
    WatchPoint *cur = wpHead;
    if (cur) {
        while (cur) {
            oss << "Watchpoint " << cur->no << ": "
                << (cur->expr ? cur->expr : "<null>") << std::endl;
            oss << "Value: " << sdb_value_as_i64(cur->val)
                << ", Evaluated: " << (cur->evaluated ? "true" : "false")
                << std::endl;
            cur = cur->next;
        }
    } else {
        oss << "No watchpoint at present." << std::endl;
    }
    return oss.str();
}

std::string sdb_cmdX(int n, const char *exprStr) {
    std::ostringstream oss;
    if (!exprStr || n <= 0) {
        oss << "Error: invalid arguments for 'x' command." << std::endl;
        return oss.str();
    }

    bool success = false;
    word_t addr = static_cast<word_t>(sdb_expr(exprStr, &success));
    if (!success) {
        oss << "Error: failed to evaluate expression \"" << exprStr << "\""
            << std::endl;
        return oss.str();
    }

    char buf[64];
    std::snprintf(buf, sizeof(buf), "Memory scan: addr=0x%08x, N=%d", addr, n);
    oss << buf << std::endl;

    for (int i = 0; i < n; i++) {
        addr_t cur = static_cast<addr_t>(addr) + static_cast<addr_t>(i * 4);
        if (device_io_mmio_isAddrValid(cur)) {
            uint32_t val = device_io_mmio_read(cur, sizeof(uint32_t));
            std::snprintf(buf, sizeof(buf), "0x%08X: %08X", cur, val);
        } else {
            std::snprintf(buf, sizeof(buf), "0x%08X: N/A", cur);
        }
        oss << buf << std::endl;
    }
    return oss.str();
}

std::string sdb_cmdP(const char *exprStr) {
    std::ostringstream oss;
    if (!exprStr) {
        oss << "Error: no expression provided." << std::endl;
        return oss.str();
    }

    SdbEvalResult result{};
    if (sdb_expr_eval_text(exprStr, sdb_npc_target_ops(), &result)) {
        oss << "$1 = " << std::dec << sdb_value_as_i64(result.value)
            << std::endl;
        oss << "unsigned = " << std::dec << sdb_value_as_u64(result.value)
            << std::endl;
        oss << "hex = 0x" << std::hex << sdb_value_as_u64(result.value)
            << std::dec;
    } else {
        oss << "Evaluation failed — check your expression.";
    }
    return oss.str();
}

std::string sdb_cmdW(const char *exprStr) {
    std::ostringstream oss;
    if (!exprStr) {
        oss << "Please provide an expression to watch." << std::endl;
        return oss.str();
    }

    WatchPoint *wp = sdb_newWP();
    if (!wp) {
        oss << "Error: no free watchpoint slots." << std::endl;
        return oss.str();
    }

    wp->expr = new char[std::strlen(exprStr) + 1];
    std::strcpy(wp->expr, exprStr);
    SdbError err{};
    wp->expr_ast = sdb_expr_parse(wp->expr, &err);
    if (wp->expr_ast) {
        SdbEvalResult result{};
        if (sdb_expr_eval(wp->expr_ast, sdb_npc_target_ops(), &result)) {
            wp->val = result.value;
            wp->evaluated = true;
            oss << "Watchpoint " << wp->no << " set: " << wp->expr
                << " (initial value: " << sdb_value_as_i64(wp->val) << ")";
        } else {
            oss << "Watchpoint " << wp->no << " set: " << wp->expr
                << " (expression cannot be evaluated yet)";
        }
    } else {
        oss << "Watchpoint parse failed: " << err.message;
        sdb_freeWP(wp);
    }
    return oss.str();
}

std::string sdb_cmdD(int no) {
    std::ostringstream oss;
    WatchPoint *wp = sdb_findWP(no);
    if (wp) {
        sdb_freeWP(wp);
        oss << "Watchpoint " << no << " deleted.";
    } else {
        oss << "Watchpoint " << no << " not found.";
    }
    return oss.str();
}
