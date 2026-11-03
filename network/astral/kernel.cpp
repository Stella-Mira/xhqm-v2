#include "kernel.h"
#include "mmap_index.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <filesystem>

namespace xhqm
{
	namespace astral
	{
		xhqm::u64 now_stamp()
		{
			return static_cast<xhqm::u64>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
		}

		// ---- 流式 CBOR 辅助（save 时逐条写索引，避免全量 json 聚合的内存峰值）----
		static void write_cbor_text(std::ofstream& out, const char* s)
		{
			size_t n = std::strlen(s);
			out.put((char)(0x60 + n));
			out.write(s, (std::streamsize)n);
		}
		static void write_cbor_array_header(std::ofstream& out, size_t count)
		{
			out.put((char)0x9A);
			for (int k = 3; k >= 0; --k) out.put((char)(count >> (8 * k)));
		}

		// ---- info 索引维护（已加锁上下文调用）----
		void kernel::index_add(const node& n)
		{
			if (!n.info.empty())
				m_info_index[n.info].push_back(n.uuid);
		}

		void kernel::index_erase(const uuid& id, const text& info)
		{
			if (info.empty()) return;
			auto nit = m_info_index.find(info);
			if (nit == m_info_index.end()) return;
			auto& vec = nit->second;
			vec.erase(std::remove_if(vec.begin(), vec.end(),
				[&id](const uuid& x) { return uuid_equal(x, id); }), vec.end());
			if (vec.empty()) m_info_index.erase(nit);
		}

		// ---- 内部实现（不加锁，供已加锁的公共方法重入调用）----
		uuid kernel::put_impl(node& n)
		{
			if (uuid_equal(n.uuid, uuid_none()))
			{
				do {
					n.uuid = astral::uuid::creat_uuid_v4();
				} while (m_node_data.count(n.uuid) != 0);   // 冲突重试
			}
			m_node_data[n.uuid] = n;
			index_add(n);
			return n.uuid;
		}

		bool kernel::del_impl(const uuid& id)
		{
			auto iter = m_node_data.find(id);
			if (iter == m_node_data.end()) return false;
			index_erase(id, iter->second.info);
			for (const auto& [nid, ninfo] : iter->second.links)
			{
				auto lit = m_node_data.find(nid);
				if (lit != m_node_data.end())
				{
					lit->second.links.erase(id);
				}
			}
			m_node_data.erase(iter);
			return true;
		}

		bool kernel::set_connect_impl(const uuid& a, const uuid& b)
		{
			auto ait = m_node_data.find(a);
			auto bit = m_node_data.find(b);
			if (ait == m_node_data.end() || bit == m_node_data.end()) return false;
			ait->second.links[b] = bit->second.info;
			bit->second.links[a] = ait->second.info;
			return true;
		}

		bool kernel::dis_connect_impl(const uuid& a, const uuid& b)
		{
			bool ok = false;
			auto ait = m_node_data.find(a);
			auto bit = m_node_data.find(b);
			if (ait != m_node_data.end())
			{
				ok |= ait->second.links.erase(b) > 0;
			}
			if (bit != m_node_data.end())
			{
				ok |= bit->second.links.erase(a) > 0;
			}
			return ok;
		}

		bool kernel::load_index_impl(const xhqm::text& path)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in.is_open()) return false;

			char magic[6];
			in.read(magic, 6);
			if (std::memcmp(magic, "ASTRAL", 6) != 0) return false;

			std::uint8_t ver = 0, flags = 0;
			in.read(reinterpret_cast<char*>(&ver), 1);
			in.read(reinterpret_cast<char*>(&flags), 1);
			if (ver != 1) return false;

			xhqm::u64 isz = 0;
			in.read(reinterpret_cast<char*>(&isz), 8);
			std::vector<std::uint8_t> ib(static_cast<std::size_t>(isz));
			if (isz > 0) in.read(reinterpret_cast<char*>(ib.data()), static_cast<std::streamsize>(isz));

			astral::json root;
			try { root = astral::json::from_cbor(ib); }
			catch (...) { return false; }

			m_offset.clear();
			m_node_data.clear();
			m_info_index.clear();
			m_path_fetch = path;

			for (const auto& j : root.at("nodes"))
			{
				node n;
				n.info = j.value("info", xhqm::text());
				n.uuid = astral::uuid::to_uuid(j.at("uuid").get<xhqm::text>());
				if (j.contains("links") && j.at("links").is_array())
				{
					for (const auto& l : j.at("links"))
					{
						astral::uuid uuid = astral::uuid::to_uuid(l.at("uuid").get<xhqm::text>());
						astral::text info = l.value("info", xhqm::text());
						n.links[uuid] = info;
					}
				}
				m_node_data[n.uuid] = n;
				index_add(n);
				m_offset[n.uuid] =
				{
					j.at("off").get<xhqm::u64>(),
					j.at("size").get<xhqm::u64>()
				};
			}
			return true;
		}


		xhqm::size kernel::count() const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			return m_node_data.size();
		}
		bool kernel::has(const uuid& id) const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			return (m_node_data.find(id) != m_node_data.end());
		}
		const node* kernel::get(const uuid& id) const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			auto iter = m_node_data.find(id);
			return (iter != m_node_data.end()) ? &(iter->second) : nullptr;
		}
		json kernel::link_of(const uuid& id, bool wath_info) const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			astral::json array = astral::json::array();
			auto iter = m_node_data.find(id);
			if (iter != m_node_data.end())
			{
				array = std::move(wath_info ? iter->second.links_info() : iter->second.links_uuid());
			}
			return array;
		}
		json kernel::data_of(const uuid& id, bool full_data) const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			astral::json object = (astral::node{}).to_json();
			auto iter = m_node_data.find(id);
			if (iter != m_node_data.end())
			{
				object = std::move(iter->second.to_json(full_data));
			}
			return object;
		}

		// ---- 连 ----
		bool kernel::set_connect(const uuid& a, const uuid& b)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			return set_connect_impl(a, b);
		}
		bool kernel::dis_connect(const uuid& a, const uuid& b)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			return dis_connect_impl(a, b);
		}
		// ---- 增 ----
		uuid kernel::ensure(const astral::text& form, const astral::uuid& uuid, const astral::text& info, const astral::json& data)
		{
			if (uuid_equal(uuid, uuid_none()))
			{
				return kernel::create(form, info, data);
			}
			else if (!kernel::update_data(form, uuid, data))
			{
				astral::json rec;
				rec["time"] = now_stamp();
				rec["form"] = form;
				rec["json"] = data;
				{
					std::unique_lock<std::shared_mutex> lock(m_mutex);
					{
						node n;
						n.uuid = uuid;
						n.info = info;
						n.datas = astral::json::array({ rec });
						m_node_data[n.uuid] = n;
						index_add(n);
					}
				}
			}
			return uuid;
		}
		uuid kernel::create(const astral::text& form, const astral::text& info, const astral::json& data)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			node n;
			n.info = info;
			astral::json rec;
			rec["time"] = now_stamp();
			rec["form"] = form;
			rec["json"] = data;
			n.datas = astral::json::array({ rec });
			return put_impl(n);
		}

		bool kernel::update_info(const text& form, const uuid& id, const text& info)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			auto iter = m_node_data.find(id);
			if (iter == m_node_data.end()) return false;
			index_erase(id, iter->second.info);   // 旧 info 出索引
			iter->second.info = info;
			index_add(iter->second);              // 新 info 入索引
			return true;
		}
		bool kernel::update_data(const text& form, const uuid& id, const json& data)
		{
			return update(form, id, [](const text& form, astral::node& node, const json& data) -> bool {
				if (!node.datas.is_array()) node.datas = astral::json::array();   // 损坏数据段防护
				astral::json rec;
				rec["time"] = now_stamp();
				rec["form"] = form;
				rec["json"] = data;
				node.datas.push_back(rec);
				return true;
			}, data);
		}
		bool kernel::remove_time(const text& form, const uuid& id, xhqm::u64 time)
		{
			return update(form, id, [](const text& form, astral::node& node, xhqm::u64 time)->bool
			{
				if (!node.datas.is_array()) { node.datas = astral::json::array(); return true; }   // 损坏数据段防护
				astral::json kept = astral::json::array();
				for (const auto& rec : node.datas)
					if (rec.is_object() && rec.value("time", xhqm::u64(0)) >= time)
						kept.push_back(rec);
				node.datas = std::move(kept);
				return true;
			}, time);
		}
		bool kernel::remove_node(const text& form, const uuid& id)
		{
			//return remove(form, id, [id](const text& form, const astral::node& node)->bool
			//{
			//	return (node.uuid == id);
			//});
			return remove(form, id, [](const text& form, const astral::node& node)->bool
			{
				return true;
			});
		}

		kernel::node_list kernel::select_uuid(const std::vector<astral::uuid>& uuids)
		{
			if (uuids.empty()) return {};
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			std::vector<astral::node> out;
			out.reserve(uuids.size());
			for (const uuid& id : uuids)
			{
				auto it = m_node_data.find(id);
				if (it == m_node_data.end()) continue;
				fetch_impl(it->second, id);   // 冷节点按需加热
				if (!it->second.datas.is_array() || it->second.datas.empty()) continue;
				astral::node n = it->second;
				n.datas = astral::json::array({ it->second.datas.back() });
				out.push_back(std::move(n));
			}
			return out;
		}
		kernel::node_list kernel::select_info(const std::vector<astral::text>& words)
		{
			if (words.empty()) return {};
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			std::vector<astral::node> out;
			// 走 info 索引：只对去重后的 info 键做子串匹配，不扫全表
			for (const auto& [key, ids] : m_info_index)
			{
				bool match = false;
				for (const auto& word : words)
					if (!word.empty() && key.find(word) != text::npos) { match = true; break; }
				if (!match) continue;
				for (const uuid& id : ids)
				{
					auto it = m_node_data.find(id);
					if (it == m_node_data.end()) continue;
					fetch_impl(it->second, id);   // 冷节点按需加热
					if (!it->second.datas.is_array() || it->second.datas.empty()) continue;
					astral::node n = it->second;
					n.datas = astral::json::array({ it->second.datas.back() });
					out.push_back(std::move(n));
				}
			}
			// 最新时间降序（与 select_time/select_size 一致，序列化时最新在前）
			std::sort(out.begin(), out.end(), [](const astral::node& a, const astral::node& b) {
				const xhqm::u64 ta = (a.datas.is_array() && !a.datas.empty() && a.datas.back().is_object())
					? a.datas.back().value("time", xhqm::u64(0)) : 0;
				const xhqm::u64 tb = (b.datas.is_array() && !b.datas.empty() && b.datas.back().is_object())
					? b.datas.back().value("time", xhqm::u64(0)) : 0;
				return ta > tb;
			});
			return out;
		}
		kernel::node_list kernel::select_data(const std::vector<astral::text>& words)
		{
			if (words.empty()) return {};
			return select("", [](const text& form, const astral::node& node, const std::vector<astral::text>& words) -> json {
				astral::json array = astral::json::array();
				if (!node.datas.is_array()) return array;   // 损坏数据段防护
				for (const auto& word : words)
				{
					if (!word.empty() && !node.datas.empty() && node.datas.back().dump().find(word) != text::npos)
					{
						array.push_back(node.datas.back()); break;
					}
				}
				return array;
			}, words);
		}
		kernel::node_list kernel::select_time(xhqm::u64 time)
		{
			return select("", [](const text& form, const astral::node& node, xhqm::u64 time) -> json {

				astral::json array = astral::json::array();
				if (!node.datas.is_array()) return array;   // 损坏数据段防护
				for (auto packet = node.datas.rbegin(); packet != node.datas.rend(); ++packet)
				{
					if (packet->value("time", xhqm::u64(0)) >= time)
					{
						array.push_back(*packet);
					}
					else
					{
						break;
					}
				}
				return array;
			}, time);
		}
		kernel::node_list kernel::select_size(xhqm::u64 size)
		{
			return select("", [](const text& form, const astral::node& node, xhqm::u64 size) -> json {

				astral::json array = astral::json::array();
				if (!node.datas.is_array()) return array;   // 损坏数据段防护
				if (node.datas.size() <= size)
				{
					array = node.datas;
				}
				else
				{
					for (auto packet = node.datas.rbegin(); packet != node.datas.rend(); ++packet)
					{
						if (array.size() < size)
						{
							array.push_back(*packet);
						}
						else
						{
							break;
						}
					}
				}
				return array;
			}, size);
		}

		// ---- 持久化（json/binary）----
		bool kernel::load_json(const xhqm::text& path)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			std::ifstream in(path, std::ios::binary);
			if (!in.is_open()) return false;

			astral::json root;
			try { in >> root; }
			catch (...) { in.close(); return false; }

			m_offset.clear();
			m_node_data.clear();
			m_info_index.clear();
			if (root.contains("nodes") && root.at("nodes").is_array())
			{
				for (const auto& j : root.at("nodes"))
				{
					node n;
					n.uuid = astral::uuid::to_uuid(j.at("uuid").get<xhqm::text>());
					n.info = j.value("info", xhqm::text());
					n.datas = j.value("datas", astral::json::array());
					if (!n.datas.is_array()) n.datas = astral::json::array();   // 损坏数据段防护
					if (j.contains("links") && j.at("links").is_array())
					{
						for (const auto& l : j.at("links"))
						{
							uuid lid = astral::uuid::to_uuid(l.at("uuid").get<xhqm::text>());
							xhqm::text linfo = l.value("info", xhqm::text());
							n.links[lid] = linfo;
						}
					}
					m_node_data[n.uuid] = n;
					index_add(n);
				}
			}
			in.close();
			return true;
		}

		bool kernel::save_json(const xhqm::text& path)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);

			// 冷节点数据源：原文件（冷节点 datas 为空，嵌入前须回读）
			std::ifstream src;
			xhqm::u64 src_blob_off = 0;
			if (!m_path_fetch.empty() && !m_offset.empty())
			{
				src.open(m_path_fetch, std::ios::binary);
				if (!src.is_open())
					return false;   // 源文件不可读：拒绝保存，避免把冷节点写成空数据
				src.seekg(8, std::ios::beg);
				xhqm::u64 isz = 0;
				src.read(reinterpret_cast<char*>(&isz), 8);
				src_blob_off = 16 + isz;
			}

			// 流式写（逐节点 dump，避免全量 nodes 数组聚合的内存峰值）
			const xhqm::text tmp = path + ".tmp";
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out.is_open()) return false;
			out << "{\"nodes\":[";
			bool first = true;
			for (const auto& [id, n] : m_node_data)
			{
				if (!first) out << ",";
				first = false;
				astral::json nj = n.to_json();
				auto oit = m_offset.find(id);
				if (oit != m_offset.end() && src.is_open())
				{
					// 冷节点：从原文件回读数据段嵌入
					src.seekg(static_cast<std::streamoff>(src_blob_off + oit->second.first), std::ios::beg);
					std::vector<std::uint8_t> cb(static_cast<std::size_t>(oit->second.second));
					src.read(reinterpret_cast<char*>(cb.data()), static_cast<std::streamsize>(oit->second.second));
					try { nj["datas"] = astral::json::from_cbor(cb); }
					catch (...) {}
				}
				out << nj.dump();
			}
			out << "],\"version\":" << astral::version << "}";
			out.close();
			if (src.is_open()) src.close();   // 先关原文件，Windows 删除打开文件会失败

			// 原子替换：优先 rename 直接覆盖（POSIX 原子），失败再 remove+rename（Windows 兼容）
			std::error_code ec;
			std::filesystem::rename(tmp, path, ec);
			if (ec)
			{
				ec.clear();
				std::filesystem::remove(path, ec);
				ec.clear();
				std::filesystem::rename(tmp, path, ec);
			}
			if (ec)
			{
				std::filesystem::remove(tmp, ec);
				return false;
			}
			return true;
		}

		bool kernel::load_binary(const xhqm::text& path)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			if (!load_index_impl(path)) return false;

			std::ifstream in(path, std::ios::binary);
			if (!in.is_open()) return false;
			in.seekg(8, std::ios::beg);
			xhqm::u64 isz = 0;
			in.read(reinterpret_cast<char*>(&isz), 8);
			const xhqm::u64 blob_off = 16 + isz;

			std::vector<std::pair<uuid, std::pair<xhqm::u64, xhqm::u64>>> todo;
			todo.reserve(m_offset.size());
			for (const auto& [id, off] : m_offset) todo.push_back({ id, off });

			for (const auto& [id, offsize] : todo)
			{
				in.seekg(static_cast<std::streamoff>(blob_off + offsize.first), std::ios::beg);
				std::vector<std::uint8_t> cb(static_cast<std::size_t>(offsize.second));
				in.read(reinterpret_cast<char*>(cb.data()), static_cast<std::streamsize>(offsize.second));
				auto dit = m_node_data.find(id);
				if (dit != m_node_data.end())
				{
					try
					{
						astral::json parsed = astral::json::from_cbor(cb);
						if (parsed.is_array())
						{
							dit->second.datas = std::move(parsed);
							m_offset.erase(id);   // 数据入内存，转热
						}
						// 非数组：保持冷状态（offset 保留），单节点容错不中断整体加载
					}
					catch (...) { /* 单节点容错：保持冷状态 */ }
				}
				else
				{
					m_offset.erase(id);
				}
			}
			in.close();
			return true;
		}

		bool kernel::save_binary(const xhqm::text& path)
		{
			// 非 const：保存成功后 m_offset 必须刷新为新文件布局，否则下次保存时
			// 冷节点按旧 offset 从「布局已变」的文件拷贝 → 错位数据（历史数据损坏根因）
			std::unique_lock<std::shared_mutex> lock(m_mutex);

			// 冷节点数据源：load_index 的原文件（冷节点 datas 为空，数据段须从原文件原样拷贝）
			std::ifstream src;
			xhqm::u64 src_blob_off = 0;
			const bool have_cold = !m_offset.empty();
			if (have_cold)
			{
				src.open(m_path_fetch, std::ios::binary);
				if (!src.is_open())
					return false;   // 源文件不可读：拒绝保存，避免把冷节点写成空数据
				src.seekg(8, std::ios::beg);
				xhqm::u64 isz = 0;
				src.read(reinterpret_cast<char*>(&isz), 8);
				src_blob_off = 16 + isz;
			}

			// 流式两遍写（临时文件 + 原子 rename）：
			//   遍A 写头 + 流式写索引 CBOR（逐条，off 由 size 累加）
			//   遍B 流式写数据段（冷节点原文件分块拷贝 / 热节点 to_cbor）
			// 全程无全量 blob / 全量索引 json 聚合，内存峰值 = 单条目 + 1MB 拷贝缓冲
			const xhqm::text tmp = path + ".tmp";
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out.is_open()) return false;

			out.write("ASTRAL", 6);
			const std::uint8_t ver = 1, flags = 0;
			out.write(reinterpret_cast<const char*>(&ver), 1);
			out.write(reinterpret_cast<const char*>(&flags), 1);
			const std::streampos isz_pos = out.tellp();
			xhqm::u64 isz = 0;
			out.write(reinterpret_cast<const char*>(&isz), 8);

			// 遍A：索引（同时记录新布局，供保存成功后刷新 m_offset）
			out.put((char)0xA2);   // map(2)
			write_cbor_text(out, "nodes");
			write_cbor_array_header(out, m_node_data.size());
			xhqm::u64 off_acc = 0;
			offset new_offsets;
			for (const auto& [id, n] : m_node_data)
			{
				auto oit = m_offset.find(id);
				xhqm::u64 size = (oit != m_offset.end())
					? oit->second.second                                     // 冷：原 size
					: static_cast<xhqm::u64>(astral::json::to_cbor(n.datas).size());  // 热：算 size

				astral::json entry;
				entry["uuid"] = xhqm::cipher::uuid::to_text(n.uuid);
				entry["info"] = n.info;
				entry["links"] = n.links_info();
				entry["off"] = off_acc;
				entry["size"] = size;
				std::vector<std::uint8_t> cb = astral::json::to_cbor(entry);
				out.write(reinterpret_cast<const char*>(cb.data()), static_cast<std::streamsize>(cb.size()));
				new_offsets[id] = { off_acc, size };
				off_acc += size;
			}
			write_cbor_text(out, "version");
			out.put((char)0x01);

			// 回填 isz
			isz = static_cast<xhqm::u64>(out.tellp()) - 16;
			out.seekp(isz_pos);
			out.write(reinterpret_cast<const char*>(&isz), 8);
			out.seekp(0, std::ios::end);

			// 遍B：数据段
			std::vector<char> buf(1 << 20);   // 1MB 拷贝缓冲
			for (const auto& [id, n] : m_node_data)
			{
				auto oit = m_offset.find(id);
				if (oit != m_offset.end() && src.is_open())
				{
					// 冷节点：原文件分块拷贝
					src.seekg(static_cast<std::streamoff>(src_blob_off + oit->second.first), std::ios::beg);
					xhqm::u64 remain = oit->second.second;
					while (remain > 0)
					{
						std::size_t chunk = (remain > buf.size()) ? buf.size() : static_cast<std::size_t>(remain);
						src.read(buf.data(), static_cast<std::streamsize>(chunk));
						if (src.gcount() != static_cast<std::streamsize>(chunk))
						{
							out.close();
							if (src.is_open()) src.close();
							std::error_code ec;
							std::filesystem::remove(tmp, ec);
							return false;   // 源读失败：放弃本次保存
						}
						out.write(buf.data(), static_cast<std::streamsize>(chunk));
						remain -= chunk;
					}
				}
				else
				{
					// 热节点：内存数据
					std::vector<std::uint8_t> cb = astral::json::to_cbor(n.datas);
					out.write(reinterpret_cast<const char*>(cb.data()), static_cast<std::streamsize>(cb.size()));
				}
			}
			out.close();
			if (src.is_open()) src.close();   // 先关原文件，Windows 删除打开文件会失败

			// 原子替换：优先 rename 直接覆盖（POSIX 原子），失败再 remove+rename（Windows 兼容）
			std::error_code ec;
			std::filesystem::rename(tmp, path, ec);
			if (ec)
			{
				ec.clear();
				std::filesystem::remove(path, ec);
				ec.clear();
				std::filesystem::rename(tmp, path, ec);
			}
			if (ec)
			{
				std::filesystem::remove(tmp, ec);
				return false;
			}

			// 成功：m_offset 刷新为新布局（仅保留仍冷的节点——datas 为空的；
			// 热节点保持无 offset，下次按内存 datas 直写，避免新数据被旧拷贝覆盖）
			m_offset.clear();
			for (const auto& [id, n] : m_node_data)
				if (n.datas.empty())
					m_offset[id] = new_offsets.at(id);
			m_path_fetch = path;
			return true;
		}

		// ---- 索引/数据 ----
		bool kernel::load_index(const xhqm::text& path)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			return load_index_impl(path);
		}

		// 冷节点加热（不加锁，供已加锁的公共方法重入调用）
		bool kernel::fetch_impl(node& n, const uuid& id)
		{
			auto oit = m_offset.find(id);
			if (oit == m_offset.end()) return true;   // 已热
			if (m_path_fetch.empty()) return false;

			std::ifstream in(m_path_fetch, std::ios::binary);
			if (!in.is_open()) return false;
			in.seekg(8, std::ios::beg);
			xhqm::u64 isz = 0;
			in.read(reinterpret_cast<char*>(&isz), 8);
			const xhqm::u64 blob_off = 16 + isz;

			in.seekg(static_cast<std::streamoff>(blob_off + oit->second.first), std::ios::beg);
			std::vector<std::uint8_t> cb(static_cast<std::size_t>(oit->second.second));
			in.read(reinterpret_cast<char*>(cb.data()), static_cast<std::streamsize>(oit->second.second));

			try { n.datas = astral::json::from_cbor(cb); }
			catch (...) { return false; }   // 解码失败：维持冷状态（offset 保留），不吞数据
			if (!n.datas.is_array())        // 数据段损坏（非数组）：维持冷状态，交由修复工具处理
			{
				n.datas = astral::json::array();
				return false;
			}
			m_offset.erase(oit);
			return true;
		}

		bool kernel::data_fetch(const astral::uuid& id)
		{
			std::unique_lock<std::shared_mutex> lock(m_mutex);
			auto dit = m_node_data.find(id);
			if (dit == m_node_data.end()) return false;
			return fetch_impl(dit->second, id);
		}

		xhqm::size kernel::loaded_size() const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			return m_node_data.size() - m_offset.size();
		}

		bool kernel::loaded_data(const astral::uuid& id) const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			return m_offset.find(id) == m_offset.end();
		}


		// ---- mmap 只读索引（定长 v8，统一 node）----
		bool kernel::save_mmap(const xhqm::text& path) const
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);

			// 1. node 按 uuid 排序
			std::vector<astral::node> nodes;
			nodes.reserve(m_node_data.size());
			for (const auto& [id, n] : m_node_data) nodes.push_back(n);
			std::sort(nodes.begin(), nodes.end(),
				[](const node& a, const node& b) { return uuid_less{}(a.uuid, b.uuid); });

			// 2. info 二级索引（按 (info, uuid) 排序，info 不唯一 → 一对多）
			std::vector<std::pair<xhqm::text, uuid>> infos;
			infos.reserve(nodes.size());
			for (const node& n : nodes) infos.push_back({ n.info, n.uuid });
			auto str_cmp = [](const auto& a, const auto& b) {
				if (a.first != b.first) return a.first < b.first;
				return uuid_less{}(a.second, b.second);
			};
			std::sort(infos.begin(), infos.end(), str_cmp);

			// 3. pool / blob / 索引记录
			std::vector<std::uint8_t> pool, blob;
			std::vector<index_record> data_recs;
			std::vector<str_index_record> info_recs;
			data_recs.reserve(nodes.size());
			info_recs.reserve(infos.size());

			auto put_str = [&pool](const xhqm::text& s) -> std::pair<xhqm::u64, xhqm::u64> {
				xhqm::u64 off = pool.size();
				xhqm::u64 size = s.size();
				pool.insert(pool.end(), s.begin(), s.end());
				return { off, size };
			};
			auto put_uuid = [&pool](const uuid& u) {
				for (int i = 0; i < 16; ++i) pool.push_back(u(i));
			};

			for (const node& n : nodes)
			{
				index_record rec;
				rec.id = n.uuid;
				auto [io, is] = put_str(n.info);
				rec.info_off = io; rec.info_size = is;

				std::vector<std::uint8_t> cb = astral::json::to_cbor(n.datas);
				rec.data_off = static_cast<xhqm::u64>(blob.size());
				rec.data_size = static_cast<xhqm::u64>(cb.size());
				blob.insert(blob.end(), cb.begin(), cb.end());

				rec.links_off = static_cast<xhqm::u64>(pool.size());
				rec.link_count = static_cast<xhqm::u64>(n.links.size());
				for (const auto& [lid, linfo] : n.links) put_uuid(lid);
				data_recs.push_back(rec);
			}

			for (const auto& [s, id] : infos)
			{
				str_index_record rec;
				auto [off, size] = put_str(s);
				rec.str_off = off; rec.str_size = size;
				rec.id = id;
				info_recs.push_back(rec);
			}

			// 4. 计算偏移 + 写文件
			const xhqm::u64 header = 96;
			const xhqm::u64 data_index_off = header;
			const xhqm::u64 info_index_off = data_index_off + data_recs.size() * 64;
			const xhqm::u64 pool_off = info_index_off + info_recs.size() * 32;
			const xhqm::u64 blob_off = pool_off + pool.size();

			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out.is_open()) return false;
			out.write("ASTRAL", 6);
			const std::uint8_t ver = 8, flags = 0;
			out.write(reinterpret_cast<const char*>(&ver), 1);
			out.write(reinterpret_cast<const char*>(&flags), 1);
			const xhqm::u64 count = data_recs.size();
			out.write(reinterpret_cast<const char*>(&count), 8);
			const xhqm::u64 pad1 = 0;
			out.write(reinterpret_cast<const char*>(&pad1), 8);       // 16..24 保留
			out.write(reinterpret_cast<const char*>(&info_index_off), 8);
			out.write(reinterpret_cast<const char*>(&pool_off), 8);
			out.write(reinterpret_cast<const char*>(&blob_off), 8);
			const char pad[48] = { 0 };
			out.write(pad, 48);                                        // 48..96 保留
			for (const auto& rec : data_recs) out.write(reinterpret_cast<const char*>(&rec), 64);
			for (const auto& rec : info_recs) out.write(reinterpret_cast<const char*>(&rec), 32);
			out.write(reinterpret_cast<const char*>(pool.data()), static_cast<std::streamsize>(pool.size()));
			out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
			return out.good();
		}
	}
}
