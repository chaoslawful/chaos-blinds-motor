#include "ir.h"

#include <Arduino.h>
// 不使用 LED_BUILTIN 反馈接收数据
// 因为 NodeMCU 上 LED_BUILTIN 连接到 D4 脚，与电机驱动模块的 PWM 输出引脚冲突
#define NO_LED_FEEDBACK_CODE
#include <TinyIRReceiver.hpp>

IRService *IRService::m_instance = nullptr;

IRService::IRService()
    : m_last_key(KEY_UNKNOWN), m_last_key_ms(0), m_debounce_ms(DEFAULT_DEBOUNCE_TIME_MS), m_last_poll_ms(0), m_anykey_handler(nullptr)
{
    pinMode(IR_RECEIVE_PWR, OUTPUT);
    digitalWrite(IR_RECEIVE_PWR, LOW);
}

IRService::~IRService()
{
    digitalWrite(IR_RECEIVE_PWR, LOW); // 关闭红外接收模块

    delete m_instance;
}

void IRService::begin()
{
    digitalWrite(IR_RECEIVE_PWR, HIGH); // 使能红外接收模块

    // 初始化红外接收模块
    if (!initPCIInterruptForTinyReceiver())
    {
        Serial.printf("No interrupt available for pin %d\n", IR_RECEIVE_PIN);
    }
    Serial.printf("Ready to receive NEC IR signals at pin %d\n", IR_RECEIVE_PIN);
}

void IRService::update()
{
    unsigned long cur_ms = millis();
    if (cur_ms - m_last_poll_ms > POLL_INTERVAL_MS)
    {
        m_last_poll_ms = cur_ms;
        this->_poll();
    }
}

void IRService::_poll()
{
    if (TinyIRReceiverData.justWritten)
    {
        TinyIRReceiverData.justWritten = false;

        if (TinyIRReceiverData.Flags != IRDATA_FLAGS_PARITY_FAILED)
        {
            // 收到完整解码的红外遥控数据
            // 消除按键抖动
            if (millis() > m_last_key_ms + m_debounce_ms)
            {
                m_last_key_ms = millis();

                IRKey key = (IRKey)TinyIRReceiverData.Command;
                if (TinyIRReceiverData.Flags == IRDATA_FLAGS_IS_REPEAT)
                {
                    key = m_last_key;
                }
                else
                {
                    m_last_key = key;
                }

                // 根据遥控器按键执行对应动作
                if (key_handlers.find(key) != key_handlers.end())
                {
                    key_handlers[key]();
                }
                else if (m_anykey_handler)
                {
                    m_anykey_handler(key);
                }
            }
        }
    }
}