/**
 * FreeRDP: 自定义业务DVC虚拟通道示例
 * 这是一个简单的DVC通道实现，可以用于开发测试业务程序
 */

#include <freerdp/config.h>

#include <stdio.h>
#include <stdlib.h>

#include <winpr/crt.h>
#include <winpr/assert.h>
#include <winpr/stream.h>

#include "rdp2vir_main.h"
#include <freerdp/client/channels.h>
#include <freerdp/channels/log.h>

#define TAG CHANNELS_TAG("rdp2vir.client")

// 自定义业务数据包结构
typedef struct {
    UINT32 messageType;  // 消息类型
    UINT32 dataLength;   // 数据长度
    BYTE data[1];        // 可变长度数据
} RDP2VIRPacket;

// 消息类型定义
#define RDP2VIR_MSG_ECHO 1
#define RDP2VIR_MSG_DATA 2
#define RDP2VIR_MSG_CONTROL 3

typedef struct {
    GENERIC_DYNVC_PLUGIN baseDynPlugin;
    UINT32 messagesReceived;
    UINT32 messagesSent;
} RDP2VIR_PLUGIN;

// 处理接收到的数据
static UINT rdp2vir_on_data_received(IWTSVirtualChannelCallback* pChannelCallback, wStream* data)
{
    GENERIC_CHANNEL_CALLBACK* callback = (GENERIC_CHANNEL_CALLBACK*)pChannelCallback;
    const BYTE* pBuffer = Stream_ConstPointer(data);
    size_t cbSize = Stream_GetRemainingLength(data);
    RDP2VIR_PLUGIN* plugin = NULL;

    WINPR_ASSERT(callback);
    WINPR_ASSERT(callback->channel);
    WINPR_ASSERT(callback->channel->Write);

    if (cbSize < 8)
    {
        WLog_ERR(TAG, "数据包太小，无法解析");
        return ERROR_INVALID_DATA;
    }

    // 跳过8字节头，直接处理JSON数据
    const char* jsonStart = (const char*)pBuffer + 8;
    size_t jsonLen = cbSize - 8;

    // 确保JSON数据以null结尾
    char* jsonBuffer = (char*)malloc(jsonLen + 1);
    if (!jsonBuffer)
    {
        WLog_ERR(TAG, "内存分配失败");
        return ERROR_INTERNAL_ERROR;
    }
    memcpy(jsonBuffer, jsonStart, jsonLen);
    jsonBuffer[jsonLen] = '\0';

    WLog_INFO(TAG, "=== 收到服务端响应 ===");
    WLog_INFO(TAG, "完整JSON: %s", jsonBuffer);

    // 简单解析JSON响应
    // 查找关键字段来确认消息类型
    if (strstr(jsonBuffer, "\"Type\""))
    {
        const char* type = strstr(jsonBuffer, "\"Type\"");
        if (type)
        {
            // 提取Type字段值
            const char* typeStart = strchr(type, ':');
            if (typeStart)
            {
                // 跳过空格和引号
                while (*typeStart == ' ' || *typeStart == ':' || *typeStart == '"') typeStart++;
                char typeValue[64] = {0};
                sscanf(typeStart, "%63[^\"]", typeValue);
                WLog_INFO(TAG, "响应类型: %s", typeValue);

                // 提取message字段
                const char* msg = strstr(jsonBuffer, "\"message\"");
                if (msg)
                {
                    const char* msgStart = strchr(msg, ':');
                    if (msgStart)
                    {
                        while (*msgStart == ' ' || *msgStart == ':' || *msgStart == '"') msgStart++;
                        char msgValue[256] = {0};
                        sscanf(msgStart, "%255[^\"]", msgValue);
                        WLog_INFO(TAG, "响应消息: %s", msgValue);
                    }
                }

                // 提取TypeError字段
                const char* err = strstr(jsonBuffer, "\"TypeError\"");
                if (err)
                {
                    const char* errStart = strchr(err, ':');
                    if (errStart)
                    {
                        while (*errStart == ' ' || *errStart == ':' || *errStart == '"') errStart++;
                        int errValue = atoi(errStart);
                        WLog_INFO(TAG, "操作状态: %s", errValue ? "成功" : "失败");
                    }
                }

                // 针对 secondary_ip 类型的特殊处理
                if (strcmp(typeValue, "secondary_ip") == 0)
                {
                    WLog_INFO(TAG, "=== 二级IP操作响应 ===");
                    // 可以添加更多针对二级IP的特定解析逻辑
                }
                // 针对 openapp 类型的特殊处理
                else if (strcmp(typeValue, "openapp") == 0)
                {
                    WLog_INFO(TAG, "=== 应用打开操作响应 ===");
                }
            }
        }
    }

    free(jsonBuffer);

    // 获取插件实例
    plugin = (RDP2VIR_PLUGIN*)callback->plugin;
    if (plugin)
    {
        plugin->messagesReceived++;
    }

    return CHANNEL_RC_OK;
}

// 通道关闭回调
static UINT rdp2vir_on_close(IWTSVirtualChannelCallback* pChannelCallback)
{
    GENERIC_CHANNEL_CALLBACK* callback = (GENERIC_CHANNEL_CALLBACK*)pChannelCallback;
    RDP2VIR_PLUGIN* plugin = NULL;

    if (callback)
    {
        plugin = (RDP2VIR_PLUGIN*)callback->plugin;
        if (plugin)
        {
            WLog_INFO(TAG, "通道关闭 - 接收消息数: %u, 发送消息数: %u",
                     plugin->messagesReceived, plugin->messagesSent);
        }
        free(callback);
    }

    return CHANNEL_RC_OK;
}

/**
 * 构建 openapp JSON 消息
 * @param appPath 应用程序路径
 * @param jsonLen 输出参数，返回 JSON 字符串长度（不包括 null 终止符）
 * @return 动态分配的 JSON 字符串，调用者需要使用 free() 释放
 */
static char* build_openapp_message(const char* appPath, int* jsonLen)
{
    char* jsonBuffer = NULL;
    char escapedPath[512];
    int i, j;
    int len;

    if (!appPath || !jsonLen)
        return NULL;

    // 转义路径中的反斜杠 (单 \ 变成双 \\)
    for (i = 0, j = 0; appPath[i] != '\0' && j < (int)sizeof(escapedPath) - 2; i++)
    {
        if (appPath[i] == '\\')
        {
            escapedPath[j++] = '\\';
            escapedPath[j++] = '\\';
        }
        else
        {
            escapedPath[j++] = appPath[i];
        }
    }
    escapedPath[j] = '\0';

    // 分配内存用于 JSON 消息
    jsonBuffer = (char*)malloc(1024);
    if (!jsonBuffer)
    {
        WLog_ERR(TAG, "build_openapp_message: 内存分配失败");
        return NULL;
    }
    memset(jsonBuffer, 0, 1024);

    // 构建 JSON 消息
    len = sprintf_s(jsonBuffer, 1024,
        "{\"Type\":\"openapp\",\"data\":{\"path\":\"%s\"}}",
        escapedPath);

    if (len <= 0 || len >= 1024)
    {
        WLog_ERR(TAG, "build_openapp_message: JSON构造失败");
        free(jsonBuffer);
        return NULL;
    }

    *jsonLen = len;
    return jsonBuffer;
}

/**
 * 构建 secondary_ip JSON 消息（使用结构化字段）
 *
 * JSON格式:
 * {
 *   "Type": "secondary_ip",
 *   "username": "用户名",
 *   "secondaryIp": "二级IP地址",
 *   "subnetMask": "子网掩码",
 *   "processName": "进程名称",
 *   "nicName": "网卡名称"
 * }
 *
 * 注意: channel.cpp 将负责将这些字段组装成规则字符串
 *
 * @param username 用户名
 * @param secondaryIp 二级IP地址
 * @param subnetMask 子网掩码
 * @param processName 进程名称
 * @param nicName 网卡名称
 * @param jsonLen 输出参数，返回 JSON 字符串长度（不包括 null 终止符）
 * @return 动态分配的 JSON 字符串，调用者需要使用 free() 释放
 */
static char* build_secondaryip_message(const char* username, const char* secondaryIp,
                                        const char* subnetMask, const char* processName,
                                        const char* nicName, int* jsonLen)
{
    char* jsonBuffer = NULL;
    int len;

    if (!username || !secondaryIp || !subnetMask || !processName || !nicName || !jsonLen)
        return NULL;

    // 分配内存用于 JSON 消息
    jsonBuffer = (char*)malloc(2048);
    if (!jsonBuffer)
    {
        WLog_ERR(TAG, "build_secondaryip_message: 内存分配失败");
        return NULL;
    }
    memset(jsonBuffer, 0, 2048);

    // 构建 JSON 消息 - 使用结构化字段
    // channel.cpp 将负责组合成: "update_ip#;#用户名#;#二级IP#;#子网掩码#;#进程名#;#网卡名称"
    len = sprintf_s(jsonBuffer, 2048,
        "{\"Type\":\"secondary_ip\",\"username\":\"%s\",\"secondaryIp\":\"%s\",\"subnetMask\":\"%s\",\"processName\":\"%s\",\"nicName\":\"%s\"}",
        username, secondaryIp, subnetMask, processName, nicName);

    if (len <= 0 || len >= 2048)
    {
        WLog_ERR(TAG, "build_secondaryip_message: JSON构造失败");
        free(jsonBuffer);
        return NULL;
    }

    *jsonLen = len;
    return jsonBuffer;
}

/**
 * 发送 secondary_ip 消息到服务器
 *
 * 发送结构化的JSON消息，channel.cpp将负责将其组装成规则字符串
 * 数据流: DVC客户端 → [JSON字段] → channel.cpp → [组装rule] → monitor → secondary_ip service
 *
 * @param channel 虚拟通道
 * @param username 用户名
 * @param secondaryIp 二级IP地址
 * @param subnetMask 子网掩码
 * @param processName 进程名称
 * @param nicName 网卡名称
 * @return TRUE 成功, FALSE 失败
 */
static BOOL send_secondaryip_message(IWTSVirtualChannel* channel, const char* username,
                                      const char* secondaryIp, const char* subnetMask,
                                      const char* processName, const char* nicName)
{
    wStream* s;
    char* jsonBuffer = NULL;
    int jsonLen = 0;
    UINT32 totalLen;

    if (!channel || !username || !secondaryIp || !subnetMask || !processName || !nicName)
        return FALSE;

    // 使用 build_secondaryip_message 构建 JSON 消息
    jsonBuffer = build_secondaryip_message(username, secondaryIp, subnetMask,
                                           processName, nicName, &jsonLen);
    if (!jsonBuffer || jsonLen <= 0)
    {
        WLog_ERR(TAG, "send_secondaryip_message: 构建消息失败");
        return FALSE;
    }

    WLog_INFO(TAG, "二级IP更新 JSON [%d bytes]: [%s]", jsonLen, jsonBuffer);

    // 消息总长度: JSON数据
    totalLen = jsonLen;

    // 创建数据包
    s = Stream_New(NULL, totalLen);
    if (!s)
    {
        WLog_ERR(TAG, "创建Stream失败");
        free(jsonBuffer);
        return FALSE;
    }

    // 写入 JSON 数据
    Stream_Write(s, (BYTE*)jsonBuffer, jsonLen);

    Stream_SealLength(s);

    WLog_INFO(TAG, "数据包总长度: %u 字节 (%d字节JSON)", totalLen, jsonLen);

    // 输出前20字节的十六进制
    BYTE* pData = Stream_Buffer(s);
    WLog_INFO(TAG, "前20字节十六进制:");
    for (int i = 0; i < 20 && i < (int)totalLen; i++)
    {
        printf("%02X ", pData[i]);
    }
    printf("\n");

    // 发送数据
    UINT rc = channel->Write(channel, Stream_Capacity(s), Stream_Buffer(s), NULL);
    Stream_Free(s, TRUE);
    free(jsonBuffer);

    if (rc == CHANNEL_RC_OK)
    {
        WLog_INFO(TAG, "二级IP更新消息发送成功");
    }
    else
    {
        WLog_ERR(TAG, "二级IP更新消息发送失败，错误码: %u", rc);
    }

    return (rc == CHANNEL_RC_OK);
}

/**
 * 发送 openapp 消息到服务器
 * @param channel 虚拟通道
 * @param appPath 应用程序路径
 * @return TRUE 成功, FALSE 失败
 */
static BOOL send_openapp_message(IWTSVirtualChannel* channel, const char* appPath)
{
    wStream* s;
    char* jsonBuffer = NULL;
    int jsonLen = 0;
    UINT32 totalLen;

    if (!channel || !appPath)
        return FALSE;

    // 使用 build_openapp_message 构建 JSON 消息
    jsonBuffer = build_openapp_message(appPath, &jsonLen);
    if (!jsonBuffer || jsonLen <= 0)
    {
        WLog_ERR(TAG, "send_openapp_message: 构建消息失败");
        return FALSE;
    }

    WLog_INFO(TAG, "JSON [%d bytes]: [%s]", jsonLen, jsonBuffer);

    // 消息总长度: JSON数据
    totalLen = jsonLen;

    // 创建数据包
    s = Stream_New(NULL, totalLen);
    if (!s)
    {
        WLog_ERR(TAG, "创建Stream失败");
        free(jsonBuffer);
        return FALSE;
    }


    // 写入 JSON 数据
    Stream_Write(s, (BYTE*)jsonBuffer, jsonLen);

    Stream_SealLength(s);

    WLog_INFO(TAG, "数据包总长度: %u 字节 (%d字节JSON)", totalLen, jsonLen);

    // 输出前20字节的十六进制
    BYTE* pData = Stream_Buffer(s);
    WLog_INFO(TAG, "前20字节十六进制:");
    for (int i = 0; i < 20 && i < (int)totalLen; i++)
    {
        printf("%02X ", pData[i]);
    }
    printf("\n");

    // 发送数据
    UINT rc = channel->Write(channel, Stream_Capacity(s), Stream_Buffer(s), NULL);
    Stream_Free(s, TRUE);
    free(jsonBuffer);

    if (rc == CHANNEL_RC_OK)
    {
        WLog_INFO(TAG, "发送成功");
    }
    else
    {
        WLog_ERR(TAG, "发送失败，错误码: %u", rc);
    }

    return (rc == CHANNEL_RC_OK);
}

// 通道打开回调
static UINT rdp2vir_on_open(IWTSVirtualChannelCallback* pChannelCallback)
{
    GENERIC_CHANNEL_CALLBACK* callback = (GENERIC_CHANNEL_CALLBACK*)pChannelCallback;
    WINPR_ASSERT(callback);
    WLog_INFO(TAG, "MyTest DVC通道已打开");

    // 检查环境变量，自动发送 openapp 消息
    const char* appPath = "C:\\Windows\\System32\\cmd.exe";
    if (appPath && strlen(appPath) > 0)
    {
        WLog_INFO(TAG, "openapp: %s", appPath);
        if (send_openapp_message(callback->channel, appPath))
        {
            WLog_INFO(TAG, "openapp 消息发送成功");
        }
        else
        {
            WLog_ERR(TAG, "openapp 消息发送失败");
        }
    }
    else
    {
        WLog_INFO(TAG, "未设置 openapp");
    }

    // 测试二级IP更新消息
    // 参数: 用户名, 二级IP, 子网掩码, 进程名, 网卡名
    const char* username = "test2";
    const char* secondaryIp = "192.168.136.200";
    const char* subnetMask = "255.255.255.0";
    const char* processName = "chrome.exe";
    const char* nicName = "Intel(R) 82574L Gigabit Network Connection";

    WLog_INFO(TAG, "发送二级IP更新请求");
    if (send_secondaryip_message(callback->channel, username, secondaryIp,
                                  subnetMask, processName, nicName))
    {
        WLog_INFO(TAG, "二级IP更新消息已发送");
    }
    else
    {
        WLog_ERR(TAG, "二级IP更新消息发送失败");
    }

    return CHANNEL_RC_OK;
}

static const IWTSVirtualChannelCallback rdp2vir_callbacks = {
    rdp2vir_on_data_received,
    rdp2vir_on_open,
    rdp2vir_on_close,
    nullptr
};

// DVC插件入口点 - 名称必须与 /dvc: 参数以及 CMake 中注册的名称一致
FREERDP_ENTRY_POINT(UINT VCAPITYPE mytest_DVCPluginEntry(IDRDYNVC_ENTRY_POINTS* pEntryPoints))
{
    // 注意：第三个参数 "mytest" 是通道的逻辑名称，建议与函数前缀保持一致
    return freerdp_generic_DVCPluginEntry(pEntryPoints, TAG, "mytest",
                                          sizeof(RDP2VIR_PLUGIN),
                                          sizeof(GENERIC_CHANNEL_CALLBACK),
                                          &rdp2vir_callbacks, nullptr, nullptr);
}
