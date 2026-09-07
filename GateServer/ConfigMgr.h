#pragma once
#include <map>
#include <string>
/*
ConfigMgr
│
├── "GateServer"  → SectionInfo
│                    └── "Port" → "8090"
│
├── "VerifyServer" → SectionInfo
│                     ├── "Host" → "127.0.0.1"
│                     └── "Port" → "50051"
│
├── "Mysql"       → SectionInfo
│                    ├── "Host" → "127.0.0.1"
│                    ├── "Port" → "3306"
│                    └── "Schema" → "cppchat"
│
└── "Redis"       → SectionInfo
├── "Host" → "127.0.0.1"
├── "Port" → "6380"
└── "PoolSize" → "5"
*/
// SectionInfo 表示 config.ini 里面的一个 [分组]
// 并保存这个分组下所有 Key=Value 配置
struct SectionInfo {

	std::map<std::string, std::string> _section_datas;

	// 2026-08-12 对比：
	// 旧写法：业务类直接访问 _section_datas，自己组合 count()、at() 和三目运算符。
	// 新写法：统一调用 GetValue("Port", "6379")，找不到时由这里返回默认值。
	// 好处：隐藏 map 查询细节，调用代码更短，而且默认值的含义一眼可见。
	std::string GetValue(const std::string& key, std::string default_value = {}) const {
		auto iter = _section_datas.find(key);

		if (iter == _section_datas.end()) {
			return default_value;
		}

		return iter->second;
	}

	// 兼容旧调用：section["Port"] 仍然可用，内部统一交给 GetValue()。
	std::string operator[](const std::string& key) const {
		return GetValue(key);
	}
};
class ConfigMgr
{
public:
	// 2026-08-12 对比：
	// 旧写法：~ConfigMgr() { _config_map.clear(); }
	// 新写法：使用 = default，让成员对象按 RAII 自动析构。
	// 好处：std::map 本来就会自动释放元素，避免重复、无意义的手动清理代码。
	~ConfigMgr();
	// 2026-08-12 对比：
	// 旧写法：先 find(section)，找到后又用 _config_map[section] 查一次。
	// 新写法：保存 find() 返回的 iter，直接返回 iter->second；函数同时声明为 const。
	// 好处：只查找一次，不会因 operator[] 意外插入分组，并明确承诺不修改配置。
	SectionInfo GetSection(const std::string& section) const {
		auto iter = _config_map.find(section);

		if (iter == _config_map.end()) {
			return {};
		}

		return iter->second;
	}

	// 兼容旧调用：config["Redis"] 仍然可用，内部统一交给 GetSection()。
	SectionInfo operator[](const std::string& section) const {
		return GetSection(section);
	}

	static ConfigMgr& GetInstance() {
		static ConfigMgr cfg_mgr;
		return cfg_mgr;
	}

	// 2026-08-09 修改：禁止复制，确保所有模块使用同一个 ConfigMgr 实例。
	ConfigMgr(const ConfigMgr&) = delete;
	ConfigMgr& operator=(const ConfigMgr&) = delete;


private:
	ConfigMgr();
	// 存储section和key-value对的map  
	std::map<std::string, SectionInfo> _config_map;
};
