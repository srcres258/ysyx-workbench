#ifndef NPC_SRC_NPC_GPR_FIELDS_HPP
#define NPC_SRC_NPC_GPR_FIELDS_HPP

// Internal GPR field lists shared by processor snapshot and SDB register
// write paths. Keep the lists here so the register ordering lives in one
// place only.
#define NPC_GPR_FIELDS_0_15(X) \
    X(0)  X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7)  \
    X(8)  X(9)  X(10) X(11) X(12) X(13) X(14) X(15)

#define NPC_GPR_FIELDS_16_31(X) \
    X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) \
    X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31)

#endif // NPC_SRC_NPC_GPR_FIELDS_HPP
