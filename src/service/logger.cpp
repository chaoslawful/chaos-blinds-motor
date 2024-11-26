#include "service/logger.h"

LoggerService *LoggerService::m_instance = nullptr;

LoggerService::LoggerService() : m_log_buf("")
{
}

LoggerService::~LoggerService()
{
}

void LoggerService::log(const String &msg, bool eol)
{
    if (eol)
    {
        m_log_buf += msg + "\n";
    }
    else
    {
        m_log_buf += msg;
    }

    if (m_log_buf.length() > LOG_BUF_SIZE)
    {
        m_log_buf = m_log_buf.substring(m_log_buf.length() - LOG_BUF_SIZE / 2);
    }

    Serial.print(msg);
}
