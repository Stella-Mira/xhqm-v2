#ifndef XHQM_NEBULA_DATA
#define XHQM_NEBULA_DATA

#include "../nebula/nebula.h"
#include "../astral/kernel.h"

namespace xhqm
{
	namespace nebula
	{
		class xhqm_io data : public node
		{
		public:
			// 通道类型（三级树：节点通道 ← 设备通道 ← 数据源通道）
			enum class channel_kind { none = 0, node_c, device, source };

			struct window : public nebula::node
			{
				nebula::uuid parent = {};                 // 上级通道（设备→节点、源→设备；节点通道无父）
				channel_kind kind = channel_kind::none;   // 通道类型（默认、节点、设备、数据源）
				std::deque<std::pair<xhqm::u64, astral::json>> view;  // 实时数据窗口 {纳秒时间, 帧}
				window(const nebula::uuid& id = {}) : nebula::node(id) {}
			};
		protected:
			xhqm::text m_path;					// 持久化文件路径（空 = 纯内存运行）
			xhqm::size m_capacity = 0;			// 条数上限：0 = 不限条数，单位 条数
			xhqm::size m_duration = 0;			// 时间上限：0 = 不限时间，单位 毫秒
			xhqm::astral::kernel m_kernel;		// 记忆内核：数据库
			std::map<nebula::uuid, data::window, uuid_less> m_windows;
			mutable std::mutex m_data_mutex;  // 通道表 + 窗口帧的锁（create/remove/update/select 共用）
			// frame_record：内核条目 {time, form, json:{type, data}} → 帧记录（{time, data}）。
			// 时间取**外层**记录戳（内层没有 time），载荷取内层 json.data；
			// 不带 data 的条目（ensure / update_info 建档桩）返回 false。
			// select_history 与 select_kernal 共用它，保证两者形状一致。
			static bool frame_record(const astral::json& rec, xhqm::u64& time, astral::json& frame);
		public:
			data(const data&) = delete;
			data& operator=(const data&) = delete;
			// 构造：path 非空则从记忆内核文件读取历史（文件不存在时 load 安全返回）；
			explicit data(const text& path = "") : m_path{ path }
			{
				if (!m_path.empty()) m_kernel.load_json(m_path);
			}
			// 析构：path 非空则把记忆内核落盘（每帧写入即同步入内核，历史无丢失）
			virtual ~data() override
			{
				if (!m_path.empty()) m_kernel.save_json(m_path);
			}

			inline void duration(xhqm::size value) { m_duration = value; }
			inline void capacity(xhqm::size value) { m_capacity = value; }
			inline xhqm::size duration(void) const { return m_duration; }
			inline xhqm::size capacity(void) const { return m_capacity; }

			// ---- 数据管理（通道/窗口/存储/查询）----
			// channels：某通道的子通道 uuid 列表（节点→设备、设备→源的枚举入口）
			std::vector<nebula::uuid> channels(const nebula::uuid& parent) const;
			// create：创建通道。kind=node_c 时通道 uuid 强制 = 本节点 uuid（节点通道即节点身份）
			nebula::uuid create(channel_kind kind, const text& name = "", const astral::json& info = astral::json::object());
			// remove：移除通道，级联移除其全部子通道（设备删→其数据源一并删；节点删→整树清空）
			void remove(const nebula::uuid& id);
			// connect：关联通道（设备挂节点、数据源挂设备），同时在记忆内核建立双向关联
			bool connect(const nebula::uuid& child, const nebula::uuid& parent);

			// updata_info：更新通道信息——写入实时窗口并落盘
			bool updata_info(const nebula::uuid& id, const text& name, const astral::json& info);
			// updata_data：更新通道数据——写入实时窗口并落盘
			bool insert_data(const nebula::uuid& id, const astral::json& frame);
			// select_message：窗口最新一帧（{time, data}，最新数据）
			astral::json select_message(const nebula::uuid& id);
			// select_windows：滑动窗口快照（[{time, data}, ...]，时间升序）
			astral::json select_windows(const nebula::uuid& id);
			// select_history：通道历史数据（[{time, data}, ...]，时间升序）
			astral::json select_history(const nebula::uuid& id);

			// select_kernal：查询通道历史（记忆内核落库部分）
			// 条目由 frame_record 统一解包：时间取**外层**记录戳、载荷取内层 json.data，
			// 与 select_history / select_windows 同形；ensure / update_info 建档的桩条目跳过。
			template<typename _func, typename ..._argv> //bool (const json&, &&argv...) //json 处理后的节点数据
			astral::json select_kernal(const nebula::uuid& id, _func&& func_, _argv&& ...argv_)
			{
				std::lock_guard<std::mutex> lk(m_data_mutex);
				astral::json arr = astral::json::array();
				if (auto n = m_kernel.get(id))
				{
					xhqm::u64 time = 0;
					astral::json frame;
					for (const auto& rec : n->datas_json())
					{
						if (func_(rec, std::forward<_argv>(argv_)...) && frame_record(rec, time, frame))
						{
							arr.push_back(astral::json{
								{"time", time},
								{"data", frame}
								});
						}
					}
				}
				return arr;
			}
		};
	}
}

#endif
