#include "service/logger.h"

LoggerService *LoggerService::m_instance = nullptr;

LoggerService::LoggerService() : m_log_buf(""), m_log_server(nullptr)
{
}

LoggerService::~LoggerService()
{
    delete m_log_server;
}

void handle_web_log()
{
    auto logger = LoggerService::get_instance();
    logger->m_log_server->send(200, "text/plain", logger->m_log_buf);
}

void LoggerService::begin()
{
    m_log_server = new ESP8266WebServer(WEB_LOG_PORT);
    m_log_server->on("/", handle_web_log);
    m_log_server->begin();
    LoggerService::printf("HTTP server started on port %d.\n", WEB_LOG_PORT);
}

void LoggerService::update()
{
    m_log_server->handleClient();
}

String LoggerService::get_cur_ts_str_()
{
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);
    return String(buf);
}

void LoggerService::log(const String &msg, bool eol)
{
    String eol_mark = eol ? "\n" : "";
    m_log_buf += "[" + get_cur_ts_str_() + "] " + msg + eol_mark;

    if (m_log_buf.length() > LOG_BUF_SIZE)
    {
        m_log_buf = m_log_buf.substring(m_log_buf.length() - LOG_BUF_SIZE / 2);
    }

    Serial.print(msg + eol_mark);
}
