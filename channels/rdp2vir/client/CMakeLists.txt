# FreeRDP: RDP2VIR Custom Business DVC Virtual Channel Client

define_channel_client("rdp2vir")

set(${MODULE_PREFIX}_SRCS rdp2vir_main.c rdp2vir_main.h)

set(${MODULE_PREFIX}_LIBS winpr)
include_directories(..)

# 使用运行时通道名称 "mytest"
add_channel_client_library(${MODULE_PREFIX} ${MODULE_NAME} "mytest" TRUE "DVCPluginEntry")
