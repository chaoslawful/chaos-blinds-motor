#pragma once

#include <Arduino.h>

class WirelessService
{
public:
    static constexpr int CLEAR_BTN_PIN = D3;       // 配置清除按钮引脚 (NodeMCU 上的 FLASH 按钮)
    static constexpr int CLEAR_CHECK_DELAY = 3000; // 重启后允许清除按钮生效的时间 (ms)

    static constexpr int DEF_FAIL_REBOOT_DELAY = 5000;
    static constexpr const char *DEF_OTA_PASSWORD = "chaos123456";
    static constexpr const char *DEF_MQTT_SERVER = "chaosgateway";
    static constexpr const char *DEF_MQTT_PORT = "1883";
    static constexpr const char *MQTT_CONF_FILE = "/mqtt_conf.json";

    static WirelessService *get_instance()
    {
        if (m_instance == nullptr)
        {
            m_instance = new WirelessService();
        }
        return m_instance;
    }

    ~WirelessService();

    /** 初始化无线网络 */
    void begin();

    /** 更新无线网络服务状态 */
    void update();

    /** 清空保存的 WiFi 设置*/
    void clear_settings_and_restart();

    /** 获取 MQTT 服务器地址 */
    const char *mqtt_server() const { return m_mqtt_server; }
    /** 获取 MQTT 服务器端口 */
    uint16_t mqtt_port() const { return atoi(m_mqtt_port); }
    /** 获取 MQTT 用户名 */
    const char *mqtt_user() const { return m_mqtt_user; }
    /** 获取 MQTT 密码 */
    const char *mqtt_pass() const { return m_mqtt_pass; }

protected:
    WirelessService();

    void load_conf_();
    void save_conf_();
    void wait_check_clear_btn_();
    void setup_wifi_();
    void setup_ota_();

    static WirelessService *m_instance;

    char m_mqtt_server[40];
    char m_mqtt_port[6];
    char m_mqtt_user[40];
    char m_mqtt_pass[40];
    bool m_should_save_config;
};
