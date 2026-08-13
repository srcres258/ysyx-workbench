#ifndef __TRACE_PCTR_FORMAT_H__
#define __TRACE_PCTR_FORMAT_H__

#include <common.h>

#define PCTR_MAGIC "PCTR"
#define PCTR_MAGIC_SIZE 4

#define PCTR_V1_VERSION 1
#define PCTR_V1_HEADER_SIZE 16
#define PCTR_V1_ADDRESS_WIDTH 4
#define PCTR_ENDIANNESS_LITTLE 1

#define PCTR_V1_TAG_SINGLE_PC 0x01
#define PCTR_V1_TAG_RUN      0x02

#define PCTR_V1_RUN_STRIDE 4u

#endif
