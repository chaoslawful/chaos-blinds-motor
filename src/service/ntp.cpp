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

    // 尝试同步时间，如果失败则设置一个默认的起始时间
    if (!sync_time_())
    {
        LoggerService::println("Initial NTP sync failed, setting default time.");
        // 设置一个默认的起始时间（2024-01-01 00:00:00 UTC+8）
        struct timeval tv = {1704067200, 0};
        settimeofday(&tv, nullptr);
        m_last_sync_ts = time(nullptr);
    }
}

void NTPService::update(bool force)
{
    time_t cur_ts = time(nullptr);

    // 检查时间是否有效（大于 2024-01-01）
    if (cur_ts < 1704067200)
    {
        LoggerService::printf("Invalid system time: %lld, forcing NTP sync...\n", (int64_t)cur_ts);
        sync_time_();
        return;
    }

    if (force || cur_ts > m_last_sync_ts + NTP_UPDATE_INTERVAL)
    {
        LoggerService::printf("Now: %lld, Last sync: %lld, Diff: %lld\n", (int64_t)cur_ts, (int64_t)m_last_sync_ts, (int64_t)cur_ts - (int64_t)m_last_sync_ts);
        LoggerService::println("Sync time with NTP server...");
        sync_time_();
    }
}

bool NTPService::sync_time_()
{
    // 同步时间
    if (!m_time_client->update())
    {
        LoggerService::println("NTP sync failed.");
        return false;
    }

    time_t ts = m_time_client->getEpochTime();

    // 验证获取的时间是否有效（大于 2024-01-01）
    if (ts < 1704067200)
    {
        LoggerService::printf("Invalid NTP time received: %lld\n", (int64_t)ts);
        return false;
    }

    // 设置系统时间
    struct timeval tv = {ts, 0};
    settimeofday(&tv, nullptr);

    m_last_sync_ts = time(nullptr);
    LoggerService::printf("Time synchronized: %lld\n", (int64_t)ts);
    return true;
}
