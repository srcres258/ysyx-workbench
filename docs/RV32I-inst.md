# RV32I 指令分类与功能

## R-Type 寄存器（仅寄存器，无立即数字段）

指令 | 功能
:-: | :-:
add rd, rs1, rs2    |   $R\mathrm{[rd]} \gets R\mathrm{[rs1]} + R\mathrm{[rs2]}$
sub rd, rs1, rs2    |   $R\mathrm{[rd]} \gets R\mathrm{[rs1]} - R\mathrm{[rs2]}$
sll rd, rs1, rs2    |   逻辑左移，$R\mathrm{[rd]} \gets R\mathrm{[rs1]} <<< R\mathrm{[rs2]}(只看低5位)$
slt rd, rs1, rs2    |   有符号比较大小，若 $R\mathrm{[rs1]} < R\mathrm{[rs2]}$ 则 $R\mathrm{[rd]} \gets 1$ ，否则 $R\mathrm{[rd]} \gets 0$
sltu rd, rs1, rs2   |   无符号比较大小，若 $R\mathrm{[rs1]} < R\mathrm{[rs2]}$ 则 $R\mathrm{[rd]} \gets 1$ ，否则 $R\mathrm{[rd]} \gets 0$
xor rd, rs1, rs2    |   $R\mathrm{[rd]} \gets R\mathrm{[rs1]} 与 R\mathrm{[rs2]} 按位异或$
srl rd, rs1, rs2    |   逻辑右移（无视符号位），$R\mathrm{[rd]} \gets R\mathrm{[rs1]} >>> R\mathrm{[rs2]}(只看低5位)$
sra rd, rs1, rs2    |   算术右移（考虑符号位），$R\mathrm{[rd]} \gets R\mathrm{[rs1]} >> R\mathrm{[rs2]}(只看低5位)$
or rd, rs1, rs2     |   $R\mathrm{[rd]} \gets R\mathrm{[rs1]} 与 R\mathrm{[rs2]} 按位或$
and rd, rs1, rs2    |   $R\mathrm{[rd]} \gets R\mathrm{[rs1]} 与 R\mathrm{[rs2]} 按位与$

## I-Type 立即数（短立即数和从内存load数据）

指令 | 功能
:-: | :-:
jalr rd, rs1, imm   |   $R\mathrm{[rd]} \gets \mathrm{PC} + 单条指令字长，然后\mathrm{PC} \gets R\mathrm{[rs1]} + \mathrm{imm}$；若目标PC地址未对齐将造成地址未对齐异常
lb rd, imm(rs1)     |   $R\mathrm{[rd]} \gets M[R\mathrm{[rs1]} + \mathrm{imm}]$，加载1个字的数据，高位符号扩展到与目标寄存器位数相同
lh rd, imm(rs1)     |   $R\mathrm{[rd]} \gets M[R\mathrm{[rs1]} + \mathrm{imm}]$，加载2个字的数据，高位符号扩展到与目标寄存器位数相同
lw rd, imm(rs1)     |   $R\mathrm{[rd]} \gets M[R\mathrm{[rs1]} + \mathrm{imm}]$，加载4个字的数据
lbu rd, imm(rs1)    |   $R\mathrm{[rd]} \gets M[R\mathrm{[rs1]} + \mathrm{imm}]$，加载1个字的数据，高位零扩展到与目标寄存器位数相同
lhu rd, imm(rs1)    |   $R\mathrm{[rd]} \gets M[R\mathrm{[rs1]} + \mathrm{imm}]$，加载2个字的数据，高位零扩展到与目标寄存器位数相同
addi rd, rs1, imm   |   $R\mathrm{[rd]} \gets R\mathrm{[rs1]} + \mathrm{imm}$
slti rd, rs1, imm   |   有符号比较大小，若 $R\mathrm{[rs1]} < \mathrm{imm}$ 则 $R\mathrm{[rd]} \gets 1$ ，否则 $R\mathrm{[rd]} \gets 0$
sltiu rd, rs1, imm  |   无符号比较大小，若 $R\mathrm{[rs1]} < \mathrm{imm}$ 则 $R\mathrm{[rd]} \gets 1$ ，否则 $R\mathrm{[rd]} \gets 0$
xori rd, rs1, imm   |   $R\mathrm{[rd]} \gets R\mathrm{[rs1]} 与 \mathrm{imm} 按位异或$
ori rd, rs1, imm    |   $R\mathrm{[rd]} \gets R\mathrm{[rs1]} 与 \mathrm{imm} 按位或$
andi rd, rs1, imm   |   $R\mathrm{[rd]} \gets R\mathrm{[rs1]} 与 \mathrm{imm} 按位与$
slli rd, rs1, imm   |   逻辑左移，$R\mathrm{[rd]} \gets R\mathrm{[rs1]} >>> \mathrm{imm}$
srli rd, rs1, imm   |   逻辑右移（无视符号位），$R\mathrm{[rd]} \gets R\mathrm{[rs1]} >>> \mathrm{imm}$
srai rd, rs1, imm   |   算术右移（考虑符号位），$R\mathrm{[rd]} \gets R\mathrm{[rs1]} >> \mathrm{imm}$

## S-Type 访存（向内存store数据）

指令 | 功能
:-: | :-:
sb rs2, imm(rs1)    |   $M[R\mathrm{[rs1]} + \mathrm{imm}] \gets R\mathrm{[rs2]}$，仅将低1字的位写入内存
sh rs2, imm(rs1)    |   $M[R\mathrm{[rs1]} + \mathrm{imm}] \gets R\mathrm{[rs2]}$，仅将低2字的位写入内存
sw rs2, imm(rs1)    |   $M[R\mathrm{[rs1]} + \mathrm{imm}] \gets R\mathrm{[rs2]}$，仅将低4字的位写入内存

## B-Type 条件跳转（S-Type的变种）

以下指令若用于更新PC的地址未对齐，将造成地址未对齐异常。

指令 | 功能
:-: | :-:
beq rs1, rs2, offset    |   若 $R\mathrm{[rs1]} = R\mathrm{[rs2]}$ ，则 $\mathrm{PC} \gets \mathrm{PC} + \mathrm{offset}$
bne rs1, rs2, offset    |   若 $R\mathrm{[rs1]} \ne R\mathrm{[rs2]}$ ，则 $\mathrm{PC} \gets \mathrm{PC} + \mathrm{offset}$
blt rs1, rs2, offset    |   有符号比较，若 $R\mathrm{[rs1]} \lt R\mathrm{[rs2]}$ ，则 $\mathrm{PC} \gets \mathrm{PC} + \mathrm{offset}$
bge rs1, rs2, offset    |   有符号比较，若 $R\mathrm{[rs1]} \ge R\mathrm{[rs2]}$ ，则 $\mathrm{PC} \gets \mathrm{PC} + \mathrm{offset}$
bltu rs1, rs2, offset   |   无符号比较，若 $R\mathrm{[rs1]} \lt R\mathrm{[rs2]}$ ，则 $\mathrm{PC} \gets \mathrm{PC} + \mathrm{offset}$
bgeu rs1, rs2, offset   |   无符号比较，若 $R\mathrm{[rs1]} \ge R\mathrm{[rs2]}$ ，则 $\mathrm{PC} \gets \mathrm{PC} + \mathrm{offset}$

## U-Type 长立即数

指令 | 功能
:-: | :-:
lui rd, imm     |   $R\mathrm{[rd]}(高20位，低位全部置0) \gets \mathrm{imm}$
auipc rd, imm   |   $R\mathrm{[rd]} \gets \mathrm{imm}(高20位，低位全部置0) + \mathrm{PC}$

## J-Type 无条件跳转（U-Type的变种）

指令 | 功能
:-: | :-:
jal rd, imm     |   $R\mathrm{[rd]} \gets \mathrm{PC} + 单条指令字长，然后\mathrm{PC} \gets \mathrm{PC} + \mathrm{imm}$；若目标PC地址未对齐将造成地址未对齐异常
