#ifndef XHQM_EVENT
#define XHQM_EVENT

namespace xhqm
{
	enum class event_type
	{
		no = 0,		//无
		key,		//按键
		mouse,		//鼠标
		timer,		//时间
	};

	enum class key_type
	{
		key = 0,//键盘

		key_0 = 48,
		key_1 = 49,
		key_2 = 50,
		key_3 = 51,
		key_4 = 52,
		key_5 = 53,
		key_6 = 54,
		key_7 = 55,
		key_8 = 56,
		key_9 = 57,
		key_A = 65,
		key_B = 66,
		key_C = 67,
		key_D = 68,
		key_E = 69,
		key_F = 70,
		key_G = 71,
		key_H = 72,
		key_I = 73,
		key_J = 74,
		key_K = 75,
		key_L = 76,
		key_M = 77,
		key_N = 78,
		key_O = 79,
		key_P = 80,
		key_Q = 81,
		key_R = 82,
		key_S = 83,
		key_T = 84,
		key_U = 85,
		key_V = 86,
		key_W = 87,
		key_X = 88,
		key_Y = 89,
		key_Z = 90,
		key_N0 = 96,
		key_N1 = 97,
		key_N2 = 98,
		key_N3 = 99,
		key_N4 = 100,
		key_N5 = 101,
		key_N6 = 102,
		key_N7 = 103,
		key_N8 = 104,
		key_N9 = 105,
		key_Nmul = 106,
		key_Nadd = 107,
		key_NEnter = 108,
		key_Nsub = 109,
		key_Ndot = 110,
		key_Nexc = 111,
		key_F1 = 112,
		key_F2 = 113,
		key_F3 = 114,
		key_F4 = 115,
		key_F5 = 116,
		key_F6 = 117,
		key_F7 = 118,
		key_F8 = 119,
		key_F9 = 120,
		key_F10 = 121,
		key_F11 = 122,
		key_F12 = 123,

		key_BackSpace = 8,
		key_Tab = 9,
		key_Clear = 12,
		key_Enter = 13,
		key_Shift = 16,
		key_Control = 17,
		key_Alt = 18,
		key_CapeLock = 20,
		key_Esc = 27,
		key_Spacebar = 32,
		key_PageUp = 33,
		key_PageDown = 34,
		key_End = 35,
		key_Home = 36,
		key_LeftArrow = 37,
		key_UpArrow = 38,
		key_RightArrow = 39,
		key_DwArrow = 40,
		key_Insert = 45,
		key_Delete = 46,
		key_NumLock = 144,

		key_170 = 170,//搜索
		key_171 = 171,//收藏
		key_172 = 172,//浏览器
		key_173 = 173,//静音
		key_174 = 174,//音量减
		key_175 = 175,//音量加
		key_179 = 179,//停止
		key_180 = 180,//邮件
		key_186 = 186,// ;:
		key_187 = 187,// =+
		key_188 = 188,// ,<
		key_189 = 189,// -_
		key_190 = 190,// .>
		key_191 = 191,// /?
		key_192 = 192,// `~
		key_219 = 219,// [{
		key_220 = 220,// \|
		key_221 = 221,// ]}
		key_222 = 222,// '"
	};

	enum class mouse_type
	{
		mouse = 0,//
		key_l = 1,//
		key_c = 2,//
		key_r = 3,//

		key_f = 4,
		key_b = 5,

		key_m0 = 60,
		key_m1 = 61,
		key_m2 = 62,
		key_m3 = 63,
		key_m4 = 64,
		key_m5 = 65,
	};

	enum class signal_type
	{
		signal_auto = 0,//信号
		signal_direct = 1,//立即执行 本线程
		signal_queued = 2,//队列执行 异线程
		signal_waited = 3,//等待执行 异线程
	};

	enum class timer_type
	{
		signal_pot = 0,//时间
		signal_now = 1,//开始
		signal_end = 2,//结束
	};

	struct xhqm_io event
	{
		event_type type = event_type::no;
		explicit event() {};
		virtual ~event() {};
	};
}
#endif

