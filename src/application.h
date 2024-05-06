#pragma once

#include "service/ir.h"

#include <ESP8266WiFi.h>
#include <ArduinoHA.h>

class Application
{
public:
    static constexpr const char *BTN_OPEN_NAME = "blinds_open";
    static constexpr const char *BTN_CLOSE_NAME = "blinds_close";
    static constexpr const char *BTN_STOP_NAME = "blinds_stop";
    static constexpr const char *SENSOR_BAT_NAME = "sensor_battery";
    static constexpr const char *SENSOR_MOTOR_NAME = "sensor_motor";
    static constexpr const char *MOTOR_CONF_FILE = "/motor_conf.json";
    static constexpr int BATTERY_UPDATE_INTERVAL_MS = 2000;
    static constexpr int WATCHDOG_INTERVAL_MS = 60000;

    static Application *get_instance()
    {
        if (m_instance == nullptr)
        {
            m_instance = new Application();
        }
        return m_instance;
    }

    ~Application();

    void begin();
    void update();

protected:
    Application();

    static void on_cover_command_(HAButton *sender);
    static void on_ir_key_(IRKey key);
    static void on_motor_stop_(long cur_pos);

    void load_motor_conf_();
    void save_motor_conf_();

    static Application *m_instance;

    long m_cover_full_close_pos; // 窗帘完全关闭时的电机标定位置
    long m_cover_full_open_pos;  // 窗帘完全打开时的电机标定位置
    long m_cover_current_pos;    // 当前电机停止位置

    WiFiClient m_wifi_client;
    HADevice m_device;
    HAMqtt m_mqtt;
    HAButton m_btn_open;
    HAButton m_btn_close;
    HAButton m_btn_stop;
    // HASensor m_sensor_bat;
    HASensor m_sensor_motor;

    // unsigned long m_last_battery_update_ms; // 最后一次上报电池电量的时间
};
