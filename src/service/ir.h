#pragma once

#include "config/pins.h"

#include <map>
#include <functional>

// 通用 17 键红外遥控器按键编码
typedef enum
{
    KEY_UNKNOWN = -1,
    KEY_1 = 0x45,
    KEY_2 = 0x46,
    KEY_3 = 0x47,
    KEY_4 = 0x44,
    KEY_5 = 0x40,
    KEY_6 = 0x43,
    KEY_7 = 0x07,
    KEY_8 = 0x15,
    KEY_9 = 0x09,
    KEY_0 = 0x19,
    KEY_STAR = 0x16,
    KEY_POUND = 0x0D,
    KEY_UP = 0x18,
    KEY_DOWN = 0x52,
    KEY_LEFT = 0x08,
    KEY_RIGHT = 0x5A,
    KEY_OK = 0x1C
} IRKey;

/** 红外遥控数据接收服务
 *
 */
class IRService
{
public:
    static constexpr int DEFAULT_DEBOUNCE_TIME_MS = 100;
    static constexpr int POLL_INTERVAL_MS = 10;

    using key_handler_t = std::function<void()>;
    using anykey_handler_t = std::function<void(IRKey)>;

    static IRService *get_instance()
    {
        if (m_instance == nullptr)
        {
            m_instance = new IRService();
        }
        return m_instance;
    }

    ~IRService();

    /** 启动红外遥控数据接收服务 */
    void begin();

    /** 更新红外遥控数据接收服务状态 */
    void update();

    /** 注册按键事件对应的处理函数 */
    void on_anykey(const anykey_handler_t callback) { m_anykey_handler = callback; }
    void on_key(IRKey key, const key_handler_t callback) { key_handlers[key] = callback; }

    /** 清除所有注册的按键事件处理函数 */
    void clear_key_handlers()
    {
        key_handlers.clear();
    }
    void clear_anykey_handler()
    {
        m_anykey_handler = nullptr;
    }

    /** 设置按键事件去抖时间 */
    void set_debounce_time(unsigned int time_ms) { m_debounce_ms = time_ms; }

protected:
    IRService();
    void _poll();

    IRKey m_last_key;             // 上次按键码
    unsigned long m_last_key_ms;  // 上次按键事件时间
    unsigned int m_debounce_ms;   // 按键去抖时间
    unsigned long m_last_poll_ms; // 上次轮询数据时间

    std::map<IRKey, key_handler_t> key_handlers; // 按键事件处理函数
    anykey_handler_t m_anykey_handler;           // 任意按键事件处理函数

    static IRService *m_instance;
};
