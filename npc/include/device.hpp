#ifndef __DEVICE_HPP__
#define __DEVICE_HPP__ 1

/**
 * @brief 更新外部设备驱动程序的状态。
 * 应该在仿真环境每执行一步后调用一次。
 */
void device_update();

/**
 * @brief 初始化外部设备驱动程序 (包括 MMIO 映射)。
 */
void device_init();

#endif /* __DEVICE_HPP__ */
