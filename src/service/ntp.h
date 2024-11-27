#pragma once

#include <Arduino.h>
#include <NTPClient.h>
#include <WifiUdp.h>
#include <time.h>

class NTPService
{
public:
    static constexpr const long NTP_UPDATE_INTERVAL = 3600; // 每小时同步一次
    static constexpr const char *NTP_SERVER = "cn.pool.ntp.org";
    static constexpr const long NTP_GMT_OFFSET = 8 * 3600; // 时区: UTC+8

    static NTPService *get_instance()
    {
        if (m_instance == nullptr)
        {
            m_instance = new NTPService();
        }
        return m_instance;
    }

    ~NTPService();

    void begin();
    void update();

protected:
    NTPService();

    static NTPService *m_instance;

    void sync_time_();

    WiFiUDP m_udp;
    NTPClient *m_time_client;
    time_t m_last_sync_ts; // 最后一次同 NTP 服务同步的时刻
};
