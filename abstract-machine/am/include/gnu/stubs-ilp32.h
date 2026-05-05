/* Dummy stub for bare-metal cross-compilation.
 * The riscv64-unknown-linux-gnu toolchain lacks multilib support for ILP32,
 * so glibc's stubs.h fails to find stubs-ilp32.h when targeting -mabi=ilp32e.
 * This file silences that check — AM kernels are bare-metal and don't use glibc.
 */
