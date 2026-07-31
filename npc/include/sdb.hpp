#ifndef __SDB_HPP__
#define __SDB_HPP__ 1

#include <common.hpp>
#include <string>

extern "C" {
#include "sdb_core.h"
}

struct WatchPoint {
    int no;
    WatchPoint *next;

    char *expr;
    SdbExpr *expr_ast;
    SdbExpr *cond_ast;
    SdbValue val;
    bool evaluated;
    bool enabled;
    bool temporary;
    int hit_count;
    int ignore_count;
    int stop_count;
};

/**
 * @brief SDB：对表达式求值。
 * 
 * @param e 表达式字符串
 * @param success 是否成功
 * @return word_t 该表达式的求值结果
 */
word_t sdb_expr(const char *e, bool *success);
const SdbTargetOps *sdb_npc_target_ops();

/**
 * @brief SDB：添加监视点。
 * 
 * @return WatchPoint* 新添加的监视点
 */
WatchPoint *sdb_newWP();

/**
 * @brief SDB：删除监视点。
 * 
 * @param wp 要删除的监视点
 */
void sdb_freeWP(WatchPoint *wp);

/**
 * @brief SDB：查找指定编号的监视点。
 * 
 * @param no 监视点编号
 * @return WatchPoint* 监视点指针；若未找到返回空指针
 */
WatchPoint *sdb_findWP(int no);

/**
 * @brief SDB：对所有监视点求值并更新监视点状态。
 * 该函数应在处理器每执行一次后调用一次。
 */
void sdb_evalAndUpdateWP();

/**
 * @brief SDB：初始化。执行主循环前必须调用一次此函数！
 */
void sdb_init();

/**
 * @brief SDB：执行主循环。
 */
void sdb_mainLoop();

// ── TUI-overlay string‑returning command execution ──

/**
 * @brief Execute "info r" and return register dump as a string.
 */
std::string sdb_cmdInfoRegs();

/**
 * @brief Execute "info w" and return watchpoint list as a string.
 */
std::string sdb_cmdInfoWatchpoints();

/**
 * @brief Execute "x N EXPR" and return memory dump as a string.
 *
 * @param n     Number of 4‑byte words to display.
 * @param expr  Expression string that evaluates to a start address.
 */
std::string sdb_cmdX(int n, const char *expr);

/**
 * @brief Execute "p EXPR" and return the evaluation result as a string.
 */
std::string sdb_cmdP(const char *expr);

/**
 * @brief Execute "w EXPR" and return watchpoint‑creation result as a string.
 */
std::string sdb_cmdW(const char *expr);

/**
 * @brief Execute "d N" and return watchpoint‑deletion result as a string.
 */
std::string sdb_cmdD(int no);

#endif /* __SDB_HPP__ */
