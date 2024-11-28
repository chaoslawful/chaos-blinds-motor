#include <stdint.h>
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
    LoggerService::printf("NTP server: %s, GMT offset: %ld, Sync interval: %ld\n", NTP_SERVER, NTP_GMT_OFFSET, NTP_UPDATE_INTERVAL);

    sync_time_();
}

void NTPService::update(bool force)
{
    time_t cur_ts = time(nullptr);
    if (force || cur_ts > m_last_sync_ts + NTP_UPDATE_INTERVAL)
    {
        LoggerService::printf("Now: %lld, Last sync: %lld, Diff: %lld\n", (int64_t)cur_ts, (int64_t)m_last_sync_ts, (int64_t)cur_ts - (int64_t)m_last_sync_ts);
        LoggerService::println("Sync time with NTP server...");
        sync_time_();
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
