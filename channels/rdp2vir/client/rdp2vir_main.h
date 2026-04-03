/**
 * FreeRDP: 自定义业务DVC虚拟通道头文件
 */

#ifndef FREERDP_CHANNEL_RDP2VIR_CLIENT_MAIN_H
#define FREERDP_CHANNEL_RDP2VIR_CLIENT_MAIN_H

#include <freerdp/config.h>
#include <freerdp/dvc.h>
#include <freerdp/types.h>
#include <freerdp/addin.h>
#include <freerdp/channels/log.h>

#define RDP2VIR_DVC_CHANNEL_NAME "mytest"

#define DVC_TAG CHANNELS_TAG("rdp2vir.client")
#ifdef WITH_DEBUG_DVC
#define DEBUG_DVC(...) WLog_DBG(DVC_TAG, __VA_ARGS__)
#else
#define DEBUG_DVC(...) \
    do                 \
    {                  \
    } while (0)
#endif

// 业务消息类型定义
#define RDP2VIR_MSG_ECHO      1  // 回显消息
#define RDP2VIR_MSG_DATA      2  // 数据消息
#define RDP2VIR_MSG_CONTROL   3  // 控制消息
#define RDP2VIR_MSG_RESPONSE  4  // 响应消息

#endif /* FREERDP_CHANNEL_RDP2VIR_CLIENT_MAIN_H */
