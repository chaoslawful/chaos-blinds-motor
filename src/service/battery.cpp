#include "config/pins.h"
#include "service/battery.h"

#include <Arduino.h>

constexpr float ADC_DIV_FACTOR = 3.005;  // NodeMCU A0 引脚板上分压倍数
constexpr float PWR_DIV_FACTOR = 13.694; // 电源侧分压倍数

BatteryService *BatteryService::m_instance = nullptr;

BatteryService::BatteryService() : m_last_poll_ms(0),
                                   m_voltage(0.0),
                                   m_adc(0),
                                   m_status(BATTERY_NORMAL),
                                   m_norm_thres(DEF_BAT_NORM_THRES),
                                   m_crit_thres(DEF_BAT_CRIT_THRES)
{
}

BatteryService::~BatteryService()
{
}

void BatteryService::begin()
{
    // 启动电池电压检测引脚
    pinMode(BATTERY_PIN, INPUT);
}

void BatteryService::update()
{
    unsigned long cur_ms = millis();
    if (cur_ms - m_last_poll_ms > POLL_INTERVAL_MS)
    {
        m_last_poll_ms = cur_ms;
        this->_poll_voltage();
    }
}

void BatteryService::_poll_voltage()
{
    // 读取电池电压
    m_adc = analogRead(BATTERY_PIN);
    m_voltage = m_adc / 1024.0 * ADC_DIV_FACTOR * PWR_DIV_FACTOR;

    // 判断电池状态
    if (m_voltage < m_crit_thres)
    {
        m_status = BATTERY_CRITICAL;
    }
    else if (m_voltage < m_norm_thres)
    {
        m_status = BATTERY_MEDIUM;
    }
    else
    {
        m_status = BATTERY_NORMAL;
    }
}
