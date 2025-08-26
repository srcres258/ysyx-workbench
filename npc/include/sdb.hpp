#ifndef __SDB_HPP__
#define __SDB_HPP__ 1

#include <common.hpp>

#define NR_EXPR_LEN 1024

struct WatchPoint {
    int no;
    WatchPoint *next;

    char expr[NR_EXPR_LEN];
    int64_t val;
    bool evaluated;
};

/**
 * @brief SDB：对表达式求值。
 * 
 * @param e 表达式字符串
 * @param success 是否成功
 * @return word_t 该表达式的求值结果
 */
word_t sdb_expr(const char *e, bool *success);

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

#endif /* __SDB_HPP__ */