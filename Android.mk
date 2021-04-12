#Android.mk
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := eng
LOCAL_MODULE := pidstat
LOCAL_SRC_FILES := common.c ioconf.c rd_stats.c count.c
LOCAL_SRC_FILES += pidstat.c
# LOCAL_CFLAGS += -Werror,-Wdate-time,-Wsign-compare,-Wunused-parameter

LOCAL_C_INCLUDES += \
	$(LOCAL_PATH)

LOCAL_CFLAGS := -g -Wall -Wno-unused-parameter -Wdate-time
LOCAL_CFLAGS += -Wno-error=date-time
LOCAL_CFLAGS += -Wno-date-time
# LOCAL_CFLAGS += -Werror # 把警告当错误输出
LOCAL_CFLAGS += -Wno-sign-compare
# -Wno-unused- 加上对应的变量或参数类型，即可忽略当前警告，编译通过。
LOCAL_CFLAGS += -Wno-unused-const-variable -Wno-unused-variable
LOCAL_CFLAGS += -Wno-unused-result
LOCAL_CFLAGS += -Wno-implicit-function-declaration
include $(BUILD_EXECUTABLE)