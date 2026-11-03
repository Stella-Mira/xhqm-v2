#include "ftype.h"

namespace xhqm
{
    struct _float 
    {
        xhqm::u32 fraction : 23;
        xhqm::u32 exponent : 8;
        xhqm::u32 sign : 1;

        inline _float(const float& value = 0.f) { (*this) = (_float)value; }
        inline operator float() const { return *(float*)this; }
    };

    xhqm::float_16::float_16(const float& fValue)
    {
        _float* pf32Value = (_float*)&fValue;

        xhqm::u32 sign = pf32Value->sign, fraction = pf32Value->fraction >> 13, value;
        xhqm::i32 exponent = (xhqm::i32)pf32Value->exponent - 127;

        if (pf32Value->exponent == 0xFF)
        {
            // inf / nan 直接映射（inf fraction=0，nan fraction!=0）
            exponent = 0x1F;
        }
        else if (exponent > 16)
        {
            // 上溢 → inf
            exponent = 0x1F;
            fraction = 0;
        }
        else if (exponent < -14)
        {
            // 下溢 → 0（忽略 subnormal）
            exponent = 0;
            fraction = 0;
        }
        else
        {
            exponent += 15;
        }

        value = (sign << 15) | (exponent << 10) | fraction;

        memcpy(this, &value, sizeof(xhqm::float_16));
    }

    xhqm::float_16::operator float() const
    {
        xhqm::u32 sign = this->sign, fraction = (xhqm::u32)(this->fraction) << 13, value;
        xhqm::i32 exponent = this->exponent;

        exponent = (exponent == 0x1F) ? 0xFF : ((exponent == 0) ? 0 : exponent + (128 - 16));

        value = (sign << 31) | (exponent << 23) | fraction;

        return *(float*)&value;
    }
}
