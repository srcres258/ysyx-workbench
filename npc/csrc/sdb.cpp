#include <iostream>
#include <cstring>
#include <string>
#include <vector>
#include <regex>
#include <memory>
#include <iomanip>
#include <memory.hpp>
#include <isa.hpp>
#include <sim_top.hpp>
#include <utils.hpp>
#include <sdb.hpp>

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
        memset(wp->expr, 0, NR_EXPR_LEN * sizeof(char));
        wp->val = 0;
        wp->evaluated = false;
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
            std::cout << "Watchpoint " << cur->no << ": " << cur->expr << std::endl;
            std::cout << "Value: " << cur->val << ", Evaluated: " << cur->evaluated << std::endl;
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
    if (wpFree) {
        for (cur = wpFree; cur->next; cur = cur->next);
        cur->next = wp;
    } else {
        wpFree = wp;
    }
}

// ---------- 表达式相关 ----------

enum TokenType {
    TK_NOTYPE = 256, TK_EQ, TK_NUM, TK_REG,
    TK_NEG_MUL, TK_NEG_DIV, TK_DEREF
};

struct Rule {
    std::string regex;
    int token_type;
};

const std::vector<Rule> rules = {
    { R"( +)", TK_NOTYPE },
    { R"(\+)", '+' },
    { R"(-)", '-' },
    { R"(\*)", '*' },
    { R"(/)", '/' },
    { R"(\()", '(' },
    { R"(\))", ')' },
    { R"(==)", TK_EQ },
    { R"((0[xX]?)?[0-9a-zA-Z]+)", TK_NUM },
    { R"(\$[0-9a-zA-Z]+)", TK_REG }
};

struct Token {
    int type;
    std::string str;
};

/**
 * @brief 表达式转换器。用于转换表达式并求值。
 */
class ExprParser {
public:
    /**
     * @brief 将表达式传入本转换器以构造 token。
     * 
     * @param e 要转换的表达式
     * @return true 转换成功
     * @return false 转换失败
     */
    bool makeToken(const std::string &e) {
        tokens.clear();
        size_t position = 0;
        while (position < e.size()) {
            bool matched = false;
            for (size_t i = 0; i < rules.size(); ++i) {
                std::smatch m;
                std::regex re(rules[i].regex);
                std::string sub = e.substr(position);
                if (std::regex_search(sub, m, re) && m.position() == 0) {
                    matched = true;
                    std::string token_str = m.str();
                    position += token_str.size();
                    if (rules[i].token_type != TK_NOTYPE) {
                        handleToken(i, token_str);
                    }
                    break;
                }
            }
            if (!matched) {
                std::cerr << "no match at position " << position << "\n";
                return false;
            }
        }
        return true;
    }

    /**
     * @brief 根据转换结果，对表达式进行求值。
     * 
     * @param p 表达式左端点
     * @param q 表达式右端点
     * @param success 是否成功
     * @return int64_t 表达式的求值结果
     */
    int64_t eval(int p, int q, bool &success) {
        if (p > q) {
            success = false;
            return 0;
        } else if (p == q) {
            if (tokens[p].type == TK_NUM) {
                success = true;
                return std::stoll(tokens[p].str, nullptr, 0);
            } else if (tokens[p].type == TK_REG) {
                std::string reg_name = tokens[p].str.substr(1);
                int64_t val = regStr2Val(reg_name, success);
                return success ? val : 0;
            } else {
                success = false;
                return 0;
            }
        } else if (tokens[p].type == TK_DEREF) {
            bool stat;
            int64_t mem_addr = eval(p + 1, q, stat);
            if (!stat) {
                success = false;
                return 0;
            }
            int64_t val = readMemory((addr_t) mem_addr);
            success = true;
            return val;
        } else if (checkParentheses(p, q)) {
            return eval(p + 1, q - 1, success);
        } else {
            int op = findOpIndex(p, q);
            if (op < 0) {
                success = false;
                return 0;
            }
            bool stat1, stat2;
            int64_t val1 = eval(p, op - 1, stat1);
            int64_t val2 = eval(op + 1, q, stat2);
            if (!stat1 || !stat2) {
                success = false;
                return 0;
            }
            switch (tokens[op].type) {
                case '+':
                    success = true;
                    return val1 + val2;
                case '-':
                    success = true;
                    return val1 - val2;
                case '*':
                    success = true;
                    return val1 * val2;
                case '/':
                    if (val2 == 0) {
                        success = false;
                        return 0;
                    }
                    success = true;
                    return val1 / val2;
                case TK_NEG_MUL:
                    success = true;
                    return -(val1 * val2);
                case TK_NEG_DIV:
                    if (val2 == 0) {
                        success = false;
                        return 0;
                    }
                    success = true;
                    return -(val1 / val2);
                case TK_EQ:
                    success = true;
                    return (val1 == val2) ? 1 : 0;
                default:
                    success = false;
                    return 0;
            }
        }
    }

    /**
     * @brief 在本转换器内对给定表达式转换并求值。
     * 
     * @param e 要转换的表达式
     * @param success 求值是否成功
     * @return int64_t 表达式的求值结果
     */
    int64_t expr(const std::string &e, bool &success) {
        if (!makeToken(e)) {
            success = false;
            return 0;
        }
        return eval(0, tokens.size() - 1, success);
    }

private:
    std::vector<Token> tokens;

    /**
     * @brief 处理表达式中的 token。
     * 
     * @param rule_idx token 规则索引
     * @param token_str 该 token 对应的正则表达式模式字符串
     */
    void handleToken(size_t rule_idx, const std::string &token_str) {
        int type = rules[rule_idx].token_type;
        if (type == '+' || type == '-') {
            if (tokens.empty() || tokens.back().type == '(') {
                tokens.push_back({TK_NUM, "0"});
                tokens.push_back({type, token_str});
            } else if (!tokens.empty() && (tokens.back().type == '*' || tokens.back().type == '/')) {
                if (type == '-') {
                    tokens.back().type = (tokens.back().type == '*') ? TK_NEG_MUL : TK_NEG_DIV;
                }
            } else {
                tokens.push_back({type, token_str});
            }
        } else if (type == '*') {
            if (tokens.empty() ||
                (tokens.back().type != TK_NUM &&
                 tokens.back().type != TK_REG &&
                 tokens.back().type != ')')) {
                tokens.push_back({TK_DEREF, "*"});
            } else {
                tokens.push_back({type, token_str});
            }
        } else {
            tokens.push_back({type, token_str});
        }
    }

    /**
     * @brief 检查表达式的括号是否匹配。
     * 
     * @param p 左括号索引
     * @param q 右括号索引
     * @return true 匹配成功
     * @return false 匹配失败
     */
    bool checkParentheses(int p, int q) {
        if (p >= q || tokens[p].type != '(' || tokens[q].type != ')') {
            return false;
        }
        int par_level = 1;
        for (int i = p + 1; i < q; ++i) {
            if (tokens[i].type == '(') {
                ++par_level;
            } else if (tokens[i].type == ')') {
                --par_level;
            }
            if (par_level <= 0) {
                return false;
            }
        }
        return par_level == 1;
    }

    /**
     * @brief 查找表达式子范围内的主运算符索引。
     * 
     * @param p 表达式左索引
     * @param q 表达式右索引
     * @return int 找到的主运算符索引；若未找到则返回 -1
     */
    int findOpIndex(int p, int q) {
        int last_pm = -1, last_td = -1, par_level = 0;
        bool found_pm = false;
        for (int i = p; i <= q; ++i) {
            switch (tokens[i].type) {
                case '(':
                    ++par_level;
                    break;
                case ')':
                    --par_level;
                    break;
                case '+':
                case '-':
                case TK_EQ:
                    if (par_level == 0) {
                        found_pm = true;
                        last_pm = i;
                    }
                    break;
                case '*':
                case '/':
                case TK_NEG_MUL:
                case TK_NEG_DIV:
                    if (par_level == 0) {
                        last_td = i;
                    }
                    break;
                default:
                    break;
            }
        }
        if (last_pm < 0 && last_td < 0) {
            return -1;
        }
        return found_pm ? last_pm : last_td;
    }

    /**
     * @brief 去除给定字符串尾部的空白字符。
     * 
     * @param str 字符串
     * @return std::string 去除空白字符后的字符串
     */
    static std::string rtrim(const std::string &str) {
        auto it = std::find_if(str.rbegin(), str.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        });
        return std::string(str.begin(), it.base());
    }

    /**
     * @brief 读取处理器的寄存器中的值。
     * 
     * @param reg_name 寄存器名称
     * @param success 是否成功
     * @return int64_t 寄存器中的值；若读取失败则返回0
     */
    static int64_t regStr2Val(const std::string &reg_name, bool &success) {
        return isaRegStr2Val(reg_name.c_str(), &success);
    }
};

// ---------- SDB相关 ----------

static void printBadArguments() {
    std::cerr << "Bad command arguments. Please type 'help' for usage." << std::endl;
}

/* 定义 SDB 的所有命令 */

static int cmd_c(char *args) {
    simExec(-1);

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
        n = args ? std::stoi(args) : 1;
        simExec(n);
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
    ExprParser parser;

    if (!args) {
        printBadArguments();
        return 0;
    }

    N_str = strtok(args, " ");
    EXPR_str = N_str ? N_str + strlen(N_str) + 1 : nullptr; // 去掉前缀"0x"
    parser.expr(EXPR_str, success);
    N = std::stoi(N_str);

    if (!success || N <= 0) {
        printBadArguments();
        return 0;
    }

    printf("Memory scan: addr=0x%08X, N=%d\n", addr, N);
    cur_addr = addr;
    for (i = 0; i < N; i++) {
        value = readMemory(cur_addr);
        printf("0x%08X: %08X\n", cur_addr, value);
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
    int64_t val;
    uint64_t uVal;
    ExprParser parser;

    if (!args) {
        printBadArguments();
        return 0;
    }

    val = parser.expr(args, success);
    if (success) {
        uVal = (uint64_t) val;
        std::cout << "求值结果：" << std::dec << val << " (0x" <<
            std::setfill('0') << std::setw(16) << std::hex <<
            uVal << std::dec << ")" << std::endl;
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
    int64_t val;
    bool success;
    ExprParser parser;

    if (!args) {
        std::cout << "请给定要进行监视的表达式的内容！" << std::endl;
        return 0;
    }
    wp = sdb_newWP();
    strcpy(wp->expr, args);
    val = parser.expr(wp->expr, success);
    if (success) {
        wp->val = val;
        wp->evaluated = true;
        std::cout << "成功设置监视点" << wp->no << "，内容为：" <<
            wp->expr << "，初始值为：" << wp->val << std::endl;
    } else {
        std::cout << "成功设置监视点" << wp->no << "，内容为：" <<
            wp->expr << "，此时无法求值。" << std::endl;
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
            std::cout << cmd_table[i].name << " - " << cmd_table[i].description << std::endl;
        }
    } else {
        for (i = 0; i < NR_CMD; i++) {
            if (strcmp(arg, cmd_table[i].name) == 0) {
                std::cout << cmd_table[i].name << " - " << cmd_table[i].description << std::endl;
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
    std::string eStr(e);
    ExprParser parser;
    return parser.expr(eStr, *success);
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
    int64_t val;
    bool success;

    for (cur = wpHead; cur; cur = cur->next) {
        val = sdb_expr(cur->expr, &success);
        if (!success) {
            continue;
        }
        if (cur->evaluated && val != cur->val) {
            sim_state.state = SIM_STOP;
            std::cout << "Watchpoint " << cur->no << " triggered: " <<
                cur->expr << std::endl;
            std::cout << "Old value: " << std::dec << cur->val <<
                ", new value: " << val << std::endl;
        }
        cur->val = val;
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
