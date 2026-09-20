#pragma once

#include <Arduino.h>

#define REL_ERR_TOL 0.01 // 判定到达目标位置的相对误差
#define ABS_ERR_TOL 80.0  // 判定到达目标位置的绝对误差(编码脉冲数)

inline bool is_close_enough(float val, float dst, float rel_tol = REL_ERR_TOL, float abs_tol = ABS_ERR_TOL)
{
    float err = abs(val - dst);
    float denom = max(abs(dst), 1.0f);
    if (err <= abs_tol || (err / denom) <= rel_tol)
    {
        return true;
    }
    return false;
}
