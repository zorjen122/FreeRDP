# FreeRDP 事件处理与虚拟通道机制技术文档

## 命令行示例
```bash
freerdp /v:192.168.1.100 /p:password /u:username
```

## 1. 系统架构概览

### 1.1 整体数据流向
```
控制端（Client）                    被控端（Server）
用户事件 → 事件捕捉 → 序列化 → TCP传输 → 反序列化 → 事件分发 → 系统执行
   ↑                                                                    ↓
   └──────────────── 界面更新 ←──────────────────────────────────────┘
```

### 1.2 核心组件层次
```
应用层 (wf_event.c, xf_event.c)
    ↓
输入接口层 (input.c/input.h)
    ↓
协议编码层 (fastpath.c)
    ↓
传输层 (transport.c)
    ↓
网络层 (TCP/TLS)
```

## 2. 控制端事件捕捉与处理流程

### 2.1 事件捕捉机制

#### Windows客户端事件捕捉 (`client/Windows/wf_event.c`)
```c
// 键盘事件捕捉 - 低级键盘钩子
LRESULT CALLBACK wf_ll_kbd_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
    PKBDLLHOOKSTRUCT p = (PKBDLLHOOKSTRUCT)lParam;
    rdpInput* input = wfc->context->input;

    // 转换为RDP扫描码
    rdp_scancode = wf_keyboard_get_rdp_scancode(p->vkCode);

    // 调用输入接口
    input->KeyboardEvent(input, flags, rdp_scancode);
}

// 鼠标事件捕捉 - 窗口消息处理
LRESULT CALLBACK wf_event_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        case WM_MOUSEMOVE:
            // 处理鼠标移动
            input->MouseEvent(input, PTR_FLAGS_MOVE, x, y);
            break;
        case WM_LBUTTONDOWN:
            // 处理鼠标左键
            input->MouseEvent(input, PTR_FLAGS_DOWN | PTR_FLAGS_BUTTON1, x, y);
            break;
    }
}
```

#### X11客户端事件捕捉 (`client/X11/xf_input.c`)
```c
// XInput2事件处理
static int xf_input_event(xfContext* xfc, const XEvent* xevent,
                         XIDeviceEvent* event, int evtype)
{
    switch(evtype)
    {
        case XI_ButtonPress:
            // 转换为RDP鼠标事件
            rdpInput* input = xfc->common.context.input;
            input->MouseEvent(input, flags, x, y);
            break;
        case XI_KeyPress:
            // 转换为RDP键盘事件
            input->KeyboardEvent(input, flags, keycode);
            break;
    }
}
```

#### SDL客户端事件捕捉 (`client/SDL/SDL3/sdl_input.cpp`)
```c
// SDL事件映射
static const scancode_entry_t map[] = {
    ENTRY(SDL_SCANCODE_A, RDP_SCANCODE_KEY_A),
    ENTRY(SDL_SCANCODE_RETURN, RDP_SCANCODE_RETURN),
    // ... 更多映射
};

// SDL事件处理
BOOL sdl_input_handle_event(SDLContext* sdl, const SDL_Event* event)
{
    switch(event->type)
    {
        case SDL_EVENT_KEY_DOWN:
            rdp_scancode = sdl_scancode_to_rdp(event->key.scancode);
            input->KeyboardEvent(input, flags, rdp_scancode);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            input->MouseEvent(input, PTR_FLAGS_MOVE, x, y);
            break;
    }
}
```

### 2.2 输入接口层 (`libfreerdp/core/input.c`)

#### 输入接口结构体
```c
struct rdp_input {
    rdpContext* context;

    // 事件回调函数指针
    pSynchronizeEvent SynchronizeEvent;         // 同步事件
    pKeyboardEvent KeyboardEvent;               // 键盘事件
    pUnicodeKeyboardEvent UnicodeKeyboardEvent; // Unicode键盘事件
    pMouseEvent MouseEvent;                     // 鼠标事件
    pExtendedMouseEvent ExtendedMouseEvent;     // 扩展鼠标事件
    pRelMouseEvent RelMouseEvent;               // 相对鼠标事件
};
```

#### 事件序列化
```c
// 键盘事件序列化
static BOOL rdp_send_keyboard_event(rdpInput* input, UINT16 flags, UINT8 code)
{
    wStream* s = input_send_pdu(input, INPUT_EVENT_SCANCODE, 8);

    // 写入Fast-Path输入事件头部
    rdp_write_input_event_header(s, 0, FASTPATH_INPUT_EVENT_SCANCODE);

    // 写入键盘数据和标志
    Stream_Write_UINT16(s, flags);  // 键盘标志
    Stream_Write_UINT16(s, code);    // 扫描码

    // 发送到传输层
    input_send_pdu(input, s);
}

// 鼠标事件序列化
static BOOL rdp_send_mouse_event(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y)
{
    wStream* s = input_send_pdu(input, INPUT_EVENT_MOUSE, 8);

    rdp_write_input_event_header(s, 0, FASTPATH_INPUT_EVENT_MOUSE);
    Stream_Write_UINT16(s, flags);  // 鼠标标志
    Stream_Write_UINT16(s, x);      // X坐标
    Stream_Write_UINT16(s, y);      // Y坐标

    input_send_pdu(input, s);
}
```

### 2.3 Fast-Path协议编码 (`libfreerdp/core/fastpath.c`)

#### Fast-Path输入事件格式
```c
// Fast-Path输入PDU结构
struct FASTPATH_INPUT_PDU {
    BYTE inputHeader;        // 输入头部
    BYTE length1;            // 长度字段1
    UINT16 length2;          // 长度字段2 (可选)
    BYTE numberEvents;       // 事件数量
    BYTE pad2;               // 填充

    // 输入事件数组
    FASTPATH_INPUT_EVENT events[];
};

// Fast-Path输入事件
struct FASTPATH_INPUT_EVENT {
    BYTE eventHeader;        // 事件头部 (1位: action, 3位: eventCode, 4位: length)
    BYTE eventData[];        // 事件数据
};
```

#### 事件编码
```c
// Fast-Path输入事件代码
enum FASTPATH_INPUT_EVENT_CODE {
    FASTPATH_INPUT_EVENT_SCANCODE = 0x0,  // 键盘扫描码
    FASTPATH_INPUT_EVENT_MOUSE = 0x1,     // 鼠标事件
    FASTPATH_INPUT_EVENT_MOUSEX = 0x2,    // 扩展鼠标事件
    FASTPATH_INPUT_EVENT_SYNC = 0x3,      // 同步事件
    FASTPATH_INPUT_EVENT_UNICODE = 0x4,   // Unicode事件
};

// 编码Fast-Path输入事件
static BOOL fastpath_send_input_pdu(rdpFastPath* fastpath, wStream* s)
{
    // 设置Fast-Path头部
    BYTE inputHeader = 0;
    inputHeader |= FASTPATH_INPUT_ACTION_FASTPATH;  // Fast-Path操作
    inputHeader |= (FASTPATH_FRAGMENT_SINGLE << 2); // 单一片段

    Stream_Seek(s, 1);  // 跳过头部位置
    Stream_Write_UINT8(s, inputHeader);

    // 计算并写入长度
    UINT16 length = Stream_GetPosition(s);
    // ... 长度编码逻辑

    // 发送到传输层
    transport_write(fastpath->transport, s);
}
```

## 3. 传输与路由机制

### 3.1 传输层 (`libfreerdp/core/transport.c`)

```c
// 传输层结构
struct rdp_transport {
    rdpContext* context;
    rdpTransportLayer* front;    // 前端层 (TLS/网络安全)
    rdpTransportLayer* transport; // 传输层 (TCP)

    // 读写回调
    TransportRecv Receive;       // 接收回调
    TransportSend Send;          // 发送回调
};

// 数据发送
BOOL transport_write(rdpTransport* transport, wStream* s)
{
    // 通过前端层发送 (TLS加密等)
    status = transport->front->write(transport->front, s);

    // 实际TCP发送
    // ... 底层网络操作
}
```

### 3.2 MCS层多路复用 (`libfreerdp/core/mcs.c`)

```c
// MCS通道结构
struct rdp_mcs_channel {
    char Name[CHANNEL_NAME_LEN + 1];  // 通道名称 (最多7字符)
    UINT32 options;                    // 通道选项
    UINT16 ChannelId;                  // 16位通道ID
    BOOL joined;                       // 是否已加入
    void* handle;                      // 通道句柄
};

// MCS层管理多个通道
struct rdp_mcs {
    UINT32 channelCount;               // 通道数量
    rdpMcsChannel* channels;           // 通道数组

    // 核心通道ID
    UINT16 IOChannelId;                // I/O通道ID
    UINT16 messageChannelId;           // 消息通道ID
};

// 发送数据到指定通道
BOOL freerdp_channel_send(rdpRdp* rdp, UINT16 channelId,
                         const BYTE* data, size_t size)
{
    // 查找通道
    for (UINT32 i = 0; i < mcs->channelCount; i++) {
        if (mcs->channels[i].ChannelId == channelId) {
            channel = &mcs->channels[i];
            break;
        }
    }

    // 分片发送
    while (left > 0) {
        chunkSize = MIN(left, VCChunkSize);
        flags = (i == 0) ? CHANNEL_FLAG_FIRST : 0;
        if (left == chunkSize)
            flags |= CHANNEL_FLAG_LAST;

        freerdp_channel_send_packet(rdp, channelId, size, flags,
                                   data, chunkSize);
    }
}
```

## 4. 被控端事件接收与分发

### 4.1 服务器端接收 (`libfreerdp/core/peer.c`)

```c
// 服务器端上下文
struct freerdp_peer {
    rdpContext* context;
    rdpInput* input;              // 输入接口

    // 虚拟通道管理
    rdpMcs* mcs;                  // MCS通道管理
    UINT32 channelCount;          // 通道数量
};

// 接收和处理PDU
static state_run_t peer_recv_pdu(freerdp_peer* client, wStream* s)
{
    // 读取PDU类型
    BYTE type = Stream_Get_UINT8(s);

    switch(type) {
        case FASTPATH_INPUT_ACTION_FASTPATH:
            // Fast-Path输入事件
            fastpath_recv_input_pdu(client->context->rdp->fastpath, s);
            break;

        case RDP_PDU_DATA:
            // 标准RDP数据PDU
            rdp_recv_data_pdu(client->context->rdp, s);
            break;
    }
}
```

### 4.2 输入事件处理 (`libfreerdp/core/input.c`)

```c
// 接收输入事件
BOOL input_recv(rdpInput* input, wStream* s)
{
    // 读取事件数量
    UINT16 numberEvents = Stream_Read_UINT16(s);
    Stream_Seek(s, 2);  // 跳过填充

    // 处理每个事件
    for (int i = 0; i < numberEvents; i++) {
        BYTE eventHeader = Stream_Read_UINT8(s);

        // 解析事件代码
        UINT8 eventCode = (eventHeader & 0x1F);
        UINT8 length = (eventHeader >> 5) & 0x03;

        switch(eventCode) {
            case FASTPATH_INPUT_EVENT_SCANCODE:
                input_recv_scancode(input, s);
                break;
            case FASTPATH_INPUT_EVENT_MOUSE:
                input_recv_mouse(input, s);
                break;
        }
    }
}

// 处理扫描码事件
static BOOL input_recv_scancode(rdpInput* input, wStream* s)
{
    UINT16 flags = Stream_Read_UINT16(s);
    UINT8 code = Stream_Read_UINT8(s);

    // 调用客户端注册的回调函数
    if (input->KeyboardEvent) {
        input->KeyboardEvent(input, flags, code);
    }
}

// 处理鼠标事件
static BOOL input_recv_mouse(rdpInput* input, wStream* s)
{
    UINT16 flags = Stream_Read_UINT16(s);
    UINT16 x = Stream_Read_UINT16(s);
    UINT16 y = Stream_Read_UINT16(s);

    if (input->MouseEvent) {
        input->MouseEvent(input, flags, x, y);
    }
}
```

### 4.3 虚拟通道事件分发

#### 服务器端通道打开 (`libfreerdp/core/peer.c`)
```c
static HANDLE freerdp_peer_virtual_channel_open(freerdp_peer* client,
                                               const char* name,
                                               UINT32 flags)
{
    // 查找已加入的通道
    for (UINT32 index = 0; index < mcs->channelCount; index++) {
        mcsChannel = &mcs->channels[index];

        if (strnicmp(name, mcsChannel->Name, length) == 0) {
            // 创建通道句柄
            peerChannel = server_channel_common_new(client,
                                                   index,
                                                   mcsChannel->ChannelId,
                                                   128, nullptr, name);
            mcsChannel->handle = peerChannel;
            return (HANDLE)peerChannel;
        }
    }
}
```

#### 虚拟通道数据接收 (`libfreerdp/core/channels.c`)
```c
BOOL freerdp_channel_peer_process(freerdp_peer* client, wStream* s,
                                 UINT16 channelId)
{
    // 读取通道数据头部
    UINT32 length = Stream_Read_UINT32(s);
    UINT32 flags = Stream_Read_UINT32(s);
    size_t chunkLength = Stream_GetRemainingLength(s);

    // 查找对应的通道句柄
    HANDLE hChannel = nullptr;
    for (UINT32 index = 0; index < mcs->channelCount; index++) {
        if (mcs->channels[index].ChannelId == channelId) {
            hChannel = (HANDLE)mcs->channels[index].handle;
            break;
        }
    }

    // 调用虚拟通道回调
    if (client->VirtualChannelRead) {
        client->VirtualChannelRead(client, hChannel,
                                  Stream_Pointer(s), chunkLength,
                                  length, flags);
    }
}
```

### 4.4 高级输入通道处理 (`channels/ainput/server/ainput_main.c`)

#### 高级输入服务器上下文
```c
typedef struct {
    // 通道接口
    IWTSVirtualChannelManager* channel_mgr;
    IWTSVirtualChannel* channel;

    // 回调函数
    pAdvancedInputServerMouse MouseEvent;
    pAdvancedInputServerTouch TouchEvent;

    void* data;                // 自定义数据
    rdpContext* rdpcontext;     // RDP上下文
} ainput_server_context;

// 鼠标事件处理
static UINT ainput_mouse_event(ainput_server_context* context,
                               UINT64 timestamp, UINT64 flags,
                               INT32 x, INT32 y)
{
    // 调用客户端注册的回调
    if (context->MouseEvent) {
        return context->MouseEvent(context, timestamp, flags, x, y);
    }
    return CHANNEL_RC_OK;
}
```

## 5. 特定通道事件处理示例

### 5.1 触摸输入通道 (`channels/rdpei/client/rdpei_main.c`)

```c
// RDPEI客户端插件
typedef struct {
    GENERIC_DYNVC_PLUGIN base;
    RdpeiClientContext* context;

    // 触摸点数据
    RDPINPUT_CONTACT_POINT contactPoints[MAX_CONTACTS];
    UINT16 maxTouchContacts;

    // 触摸笔数据
    RDPINPUT_PEN_CONTACT_POINT penContactPoints[MAX_PEN_CONTACTS];

    CRITICAL_SECTION lock;      // 线程安全
    HANDLE thread;              // 处理线程
} RDPEI_PLUGIN;

// 发送触摸事件
static UINT rdpei_send_touch_frame(RDPEI_PLUGIN* rdpei)
{
    wStream* s = Stream_New(NULL, 1024);

    // 写入触摸帧头部
    rdpei_write_touch_frame_header(s, rdpei);

    // 写入每个接触点
    for (UINT32 i = 0; i < contactCount; i++) {
        rdpei_write_contact_point(s, &rdpei->contactPoints[i]);
    }

    // 通过动态虚拟通道发送
    return rdpei->channel->Write(rdpei->channel, s->buffer,
                                 Stream_GetPosition(s));
}
```

### 5.2 剪贴板通道 (`channels/cliprdr/client/cliprdr_main.c`)

```c
// 剪贴板数据接收
static UINT cliprdr_receive_data(cliprdrPlugin* cliprdr, wStream* s)
{
    // 读取剪贴板数据
    UINT32 streamId = Stream_Read_UINT32(s);
    UINT32 dataLength = Stream_Read_UINT32(s);
    BYTE* data = Stream_Pointer(s);

    // 格式处理
    UINT32 format = cliprdr->requestedFormat;

    // 调用客户端回调
    if (cliprdr->clientCallback->ClientClipboardData) {
        cliprdr->clientCallback->ClientClipboardData(
            cliprdr, streamId, format, data, dataLength);
    }
}
```

## 6. 事件处理完整时序图

```
用户操作 (鼠标点击)
    ↓
客户端事件捕捉
    ├─ Windows: wf_event.c (WM_LBUTTONDOWN)
    ├─ X11: xf_input.c (XI_ButtonPress)
    └─ SDL: sdl_input.cpp (SDL_EVENT_MOUSE_DOWN)
    ↓
输入接口层
    └─ input->MouseEvent(input, PTR_FLAGS_DOWN | PTR_FLAGS_BUTTON1, x, y)
    ↓
事件序列化
    └─ rdp_send_mouse_event() → Fast-Path编码
    ↓
MCS层路由
    └─ freerdp_channel_send(channelId, data, size)
    ↓
传输层发送
    └─ transport_write() → TCP/TLS → 网络
    ↓
    ═══════════════════════════════════════
    ↓
服务器端接收
    └─ transport_read() → peer_recv_pdu()
    ↓
事件反序列化
    └─ input_recv() → input_recv_mouse()
    ↓
回调分发
    ├─ 如果是核心输入: input->MouseEvent()
    └─ 如果是虚拟通道: freerdp_channel_peer_process()
    ↓
具体处理
    ├─ 高级输入: ainput->MouseEvent()
    ├─ 触摸输入: rdpei->TouchEvent()
    └─ 其他通道: 各通道的回调函数
    ↓
系统执行
    └─ 本地系统API调用 (SendInput, XTest, etc.)
```

## 7. 关键技术要点

### 7.1 事件标识与路由
- **ChannelId**: 16位通道标识符，用于区分不同虚拟通道
- **EventCode**: 事件类型代码 (扫描码、鼠标、同步等)
- **Flags**: 事件标志位 (按键状态、鼠标按钮等)

### 7.2 多路复用机制
- **时分复用**: 不同通道的数据在同一TCP连接上交替传输
- **分片传输**: 大数据包分成小块 (CHANNEL_FLAG_FIRST/LAST)
- **优先级处理**: 不同通道有不同的优先级设置

### 7.3 线程安全
- **临界区保护**: `CRITICAL_SECTION lock` 保护共享数据
- **消息队列**: `wMessageQueue* queue` 异步事件处理
- **回调机制**: 函数指针回调实现松耦合

### 7.4 性能优化
- **Fast-Path**: 绕过标准RDP层级，减少延迟
- **批量发送**: 多个事件打包在一个PDU中
- **零拷贝**: 尽量减少数据复制操作

## 8. 调试与监控

### 8.1 日志设置
```bash
# 启用调试日志
WLOG_LEVEL=DEBUG freerdp /v:server

# 设置日志格式
WLOG_PREFIX="[%fn:%ln] " freerdp /v:server

# 特定组件日志
WLOG_FILTER=core.input:DEBUG freerdp /v:server
```

### 8.2 常用调试点
- **客户端**: `client/*/wf_event.c` - 事件捕捉
- **协议层**: `libfreerdp/core/input.c` - 事件序列化
- **通道层**: `libfreerdp/core/channels.c` - 通道路由
- **服务器端**: `server/Sample/sf_ainput.c` - 事件处理

## 9. 扩展开发

### 9.1 添加新的事件类型
1. 在 `include/freerdp/input.h` 添加事件类型定义
2. 在 `libfreerdp/core/input.c` 实现序列化/反序列化
3. 在客户端添加事件捕捉逻辑
4. 在服务器端添加事件处理逻辑

### 9.2 添加新的虚拟通道
1. 在 `channels/` 创建新目录
2. 实现客户端和服务器端插件
3. 在 `channels/client/addin.c` 注册插件
4. 处理通道生命周期和回调

这份文档详细描述了FreeRDP中从用户操作到系统执行的完整事件处理流程，涵盖了客户端事件捕捉、协议编码、网络传输、服务器接收和事件分发的全过程。