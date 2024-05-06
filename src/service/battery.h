#pragma once

typedef enum
{
    BATTERY_NORMAL = 0,
    BATTERY_MEDIUM = 1,
    BATTERY_CRITICAL = 2,
} BatteryStatus;

class BatteryService
{
public:
    static constexpr float DEF_BAT_NORM_THRES = 7.2;
    static constexpr float DEF_BAT_CRIT_THRES = 6.8;
    static constexpr int POLL_INTERVAL_MS = 10000;

    static BatteryService *get_instance()
    {
        if (m_instance == nullptr)
        {
            m_instance = new BatteryService();
        }
        return m_instance;
    }

    ~BatteryService();

    /** 启动电池状态感知服务 */
    void begin();

    /** 更新电池状态 */
    void update();

    /** 获取电池状态判定阈值 */
    float get_norm_thres() const { return m_norm_thres; }
    float get_crit_thres() const { return m_crit_thres; }

    /** 设置电池状态判定阈值 */
    void set_norm_thres(float norm_thres) { m_norm_thres = norm_thres; }
    void set_crit_thres(float crit_thres) { m_crit_thres = crit_thres; }

    int get_adc() const { return m_adc; }
    float get_voltage() const { return m_voltage; }
    BatteryStatus get_status() const { return m_status; }
    const char *get_status_str() const
    {
        return m_status == BATTERY_NORMAL ? "Normal" : (m_status == BATTERY_MEDIUM ? "Medium" : "Critical");
    }

protected:
    BatteryService();

    void _poll_voltage();

    unsigned long m_last_poll_ms;

    float m_norm_thres; // 电池正常电压阈值
    float m_crit_thres; // 电池临界电压阈值
    int m_adc;          // ADC 读取值(0~1023)
    float m_voltage;
    BatteryStatus m_status;

    static BatteryService *m_instance;
};
