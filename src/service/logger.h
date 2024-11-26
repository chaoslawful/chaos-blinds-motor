#pragma once

#include <Arduino.h>
#include <StreamString.h>

class LoggerService
{
public:
    static constexpr int LOG_BUF_SIZE = 2048;

    static LoggerService *get_instance()
    {
        if (m_instance == nullptr)
        {
            m_instance = new LoggerService();
        }
        return m_instance;
    }

    ~LoggerService();

    static void println(const String &msg)
    {
        LoggerService::get_instance()->log(msg);
    }

    static void println(const Printable &x)
    {
        StreamString ss;
        x.printTo(ss);
        LoggerService::get_instance()->log(ss);
    }

    static void println()
    {
        LoggerService::get_instance()->log("");
    }

    static void print(const String &msg)
    {
        LoggerService::get_instance()->log(msg, false);
    }

    static void print(const Printable &x)
    {
        StreamString ss;
        x.printTo(ss);
        LoggerService::get_instance()->log(ss, false);
    }

    static void printf(const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        char buf[2048];
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
    }
        LoggerService::get_instance()->log(buf, false);

    void log(const String &msg, bool eol = true);
    String get_log_buf() const { return m_log_buf; }

protected:
    LoggerService();

    static LoggerService *m_instance;

    String m_log_buf;
};
