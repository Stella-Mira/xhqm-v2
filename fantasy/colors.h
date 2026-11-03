#ifndef XHQM_FANTASY_COLORS
#define XHQM_FANTASY_COLORS

#include "fantasy.h"

namespace xhqm
{
    //颜色体系
    using rgba = unsigned int;
    using hlsa = std::array<xhqm::real, 4>;
    using hvsa = std::array<xhqm::real, 4>;
    using cmyk = std::array<xhqm::real, 4>;
#define xhqm_rgb(_r,_g,_b)		((xhqm::rgba)(((xhqm::byte)(_r)|((xhqm::rgba)((xhqm::byte)(_g))<<8))|(((xhqm::rgba)(xhqm::byte)(_b))<<16)|(((xhqm::rgba)(xhqm::byte)(0xff))<<24)))
#define xhqm_rgba(_r,_g,_b,_a)	((xhqm::rgba)(((xhqm::byte)(_r)|((xhqm::rgba)((xhqm::byte)(_g))<<8))|(((xhqm::rgba)(xhqm::byte)(_b))<<16)|(((xhqm::rgba)(xhqm::byte)(_a))<<24)))
#define xhqm_rgba_r(_data)		((xhqm::byte*)(&_data))[0]
#define xhqm_rgba_g(_data)		((xhqm::byte*)(&_data))[1]
#define xhqm_rgba_b(_data)		((xhqm::byte*)(&_data))[2]
#define xhqm_rgba_a(_data)		((xhqm::byte*)(&_data))[3]

    enum class rgba_color : unsigned int
    {
        transparent = 0x00000000,
        black = 0xff000000,
        white = 0xffffffff,

        blue = 0xffff0000,
        cyan = 0xffffff00,
        green = 0xff00ff00,
        yello = 0xff00ffff,
        red = 0xff0000ff,

        magenta = 0xffff00ff,
        fuchsia = 0xffff00ff,

        grey = 0xff808080,
    };

    class xhqm_io color
    {
    public:
        enum type
        {
            color_rgba,
            color_hlsa,
            color_hvsa,
            color_cmyk,
        };
        //RGBA 0.0-1.0
        static bool is_good(const std::array<xhqm::real, 4>& input);
        static std::array<xhqm::real, 4> rgba2array(const xhqm::rgba& input);
        static xhqm::rgba array2rgba(const std::array<xhqm::real, 4>& input);

        //cmyk
        //青色（C）0.0-1.0
        //洋红（M）0.0-1.0
        //黄色（Y）0.0-1.0
        //黑色（K）0.0-1.0
        static xhqm::cmyk rgba2cmyk(const xhqm::rgba& input);
        static xhqm::rgba cmyk2rgba(const xhqm::cmyk& input);


        //hlsa(hue, saturation, lightness)
        //色相    色轮上从 0 到 360 的度数。0 是红色，120 是绿色，240 是蓝色。
        //亮度    百分比，0％是黑色，100％是白色。
        //饱和度  百分比，0％表示灰色阴影，100％表示全色。
        //透明度  0.f-1.f
        static xhqm::hlsa rgba2hlsa(const xhqm::rgba& input);
        static xhqm::rgba hlsa2rgba(const xhqm::hlsa& input);


        //hvsa(hue, value, saturation)
        //色相    色轮上从 0 到 360 的度数。0 是红色，120 是绿色，240 是蓝色。
        //明度    百分比，0％是黑色，表示光谱色
        //饱和度  百分比，0％表示灰色阴影，100％表示全色。
        //透明度  0.f-1.f
        static xhqm::hvsa rgba2hvsa(const xhqm::rgba& input);
        static xhqm::rgba hvsa2rgba(const xhqm::hvsa& input);
    public:
        static bool assignment(xhqm::real value, const std::vector<xhqm::color>& colors, xhqm::color& color);
        static std::vector<xhqm::color> assignment(xhqm::size size, std::vector<xhqm::color> colors);
    protected:
        color::type m_color_type = color::color_rgba;
        std::array<float, 4> m_color_value;
    public:
        color()
            : m_color_type(color::color_rgba)
        {
            from_rgba(0);
        }
        color(const xhqm::rgba& value)
            : m_color_type(color::color_rgba)
        {
            from_rgba(value);
        }
        color(const xhqm::rgba_color& value)
            : m_color_type(color::color_rgba)
        {
            from_rgba(xhqm::rgba(value));
        }

        void operator =(const xhqm::rgba& value)
        {
            from_rgba(value);
        }

        void operator =(const xhqm::rgba_color& value)
        {
            from_rgba(xhqm::rgba(value));
        }

        inline operator xhqm::rgba() const
        {
            return to_rgba();
        }

        xhqm::rgba to_rgba() const;

        void from_rgba(const xhqm::rgba& value);

        void invert_type(const xhqm::color::type& type);
    public:
        void from_hlsa(const xhqm::hlsa& value)
        {
            from_rgba(xhqm::color::hlsa2rgba(value));
        }
        void from_hvsa(const xhqm::hvsa& value)
        {
            from_rgba(xhqm::color::hvsa2rgba(value));
        }
        void from_cmyk(const xhqm::cmyk& value)
        {
            from_rgba(xhqm::color::cmyk2rgba(value));
        }
        xhqm::hlsa to_hlsa() const
        {
            return xhqm::color::rgba2hlsa(to_rgba());
        }
        xhqm::hvsa to_hvsa() const
        {
            return xhqm::color::rgba2hvsa(to_rgba());
        }
        xhqm::cmyk to_cmyk() const
        {
            return xhqm::color::rgba2cmyk(to_rgba());
        }
    };

    namespace fantasy
    {
        using color = xhqm::rgba;
        using colors = std::vector<xhqm::rgba>;
    }
}
#endif