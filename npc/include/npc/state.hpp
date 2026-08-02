#ifndef NPC_STATE_HPP
#define NPC_STATE_HPP

#include <cstdint>
#include <string>

#include <processor.hpp>

namespace npc {

/**
 * @brief 仿真器运行状态枚举。
 *
 * 对应内部 @c SimStateEnum，语义一致：
 * - Running: 仿真正在执行
 * - Stopped: 已停止（因断点、SDB 等）
 * - Ended:   正常结束
 * - Aborted: 异常终止
 * - Quit:    用户请求退出
 */
enum class SimStatus {
    Running,
    Stopped,
    Ended,
    Aborted,
    Quit
};

/**
 * @brief 单条已退休指令的执行快照（用于 TUI / trace 等消费者）。
 */
struct ExecInfo {
    std::uint32_t pc;   ///< 退休 PC
    std::uint32_t inst; ///< 退休指令字
};

/**
 * @brief 处理器状态类型别名，保持与 @c ProcessorState 的 DiffTest ABI 完全兼容。
 *
 * @c ProcessorState 定义为 @c extern "C" 结构体，其二进制布局与
 * NEMU riscv32_CPU_state 一致，供 DiffTest memcpy 使用。
 * 通过此别名将类型导入 @c npc 命名空间，方便公开 API 消费者引用。
 */
using ProcessorState = ::ProcessorState;

/**
 * @brief 获取当前仿真环境的处理器状态（便捷函数，供尚未迁移的代码使用）。
 *
 * @return ProcessorState 处理器状态快照
 */
ProcessorState getProcessorState();

} // namespace npc

#endif // NPC_STATE_HPP
