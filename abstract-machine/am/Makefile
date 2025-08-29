NAME     := am
SRCS      = $(addprefix src/, $(AM_SRCS))
INC_PATH += $(AM_HOME)/am/src

ifeq ($(CONFIG_AUDIO),y)
CFLAGS = -DCONFIG_AUDIO=1
endif

include $(AM_HOME)/Makefile
