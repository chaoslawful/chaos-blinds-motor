#include "service/ntp.h"
#include "service/logger.h"

NTPService *NTPService::m_instance = nullptr;

NTPService::NTPService() : m_udp()
{
    m_time_client = new NTPClient(m_udp, NTP_SERVER, NTP_GMT_OFFSET);
    m_last_sync_ts = 0;
}

NTPService::~NTPService()
{
    delete m_time_client;
}

void NTPService::begin()
{
    m_time_client->begin();
    LoggerService::println("NTP service started.");
}

void NTPService::update()
{
    time_t now = time(nullptr);
    if (now - m_last_sync_ts > NTP_UPDATE_INTERVAL)
    {
        LoggerService::printf("Now: %ld, Last sync: %ld, Sync interval: %d\n", now, m_last_sync_ts, NTP_UPDATE_INTERVAL);
        LoggerService::println("Sync time with NTP server...");
        sync_time_();
        m_last_sync_ts = now;
    }
}

void NTPService::sync_time_()
{
    // 同步时间
    m_time_client->update();
    time_t ts = m_time_client->getEpochTime();

    // 设置系统时间
    struct timeval tv = {ts, 0};
    settimeofday(&tv, nullptr);

    m_last_sync_ts = time(nullptr);
    LoggerService::println("Time synchronized.");
}
