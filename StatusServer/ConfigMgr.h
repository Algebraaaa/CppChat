#pragma once

#include <map>
#include <string>

/*
config.ini 读入内存后的结构：

ConfigMgr
├── "StatusServer" -> SectionInfo -> Host / Port
├── "ChatServer1"  -> SectionInfo -> Host / Port
└── "ChatServer2"  -> SectionInfo -> Host / Port

读取 config["StatusServer"]["Port"] 时：
1. 第一个 [] 找到 StatusServer 分组；
2. 第二个 [] 在该分组中找到 Port；
3. 最终得到字符串 "50052"。
*/

// SectionInfo 表示 config.ini 中的一个 [分组]。
struct SectionInfo {
	// 例如 values_["Host"] == "127.0.0.1"。
	std::map<std::string, std::string> values_;

	// const 表示这个函数只读取配置，不会修改 values_。
	std::string GetValue(const std::string& key,
		std::string default_value = "") const {
		std::map<std::string, std::string>::const_iterator iter = values_.find(key);

		if (iter == values_.end()) {
			return default_value;
		}

		return iter->second;
	}

	// 让调用方可以写 section["Port"]，实际查询仍由 GetValue 完成。
	std::string operator[](const std::string& key) const {
		return GetValue(key);
	}
};

// ConfigMgr 在程序启动时读取一次 config.ini，并保存所有配置。
class ConfigMgr {
public:
	// std::map 会在 ConfigMgr 析构时自动释放，不需要手动 clear()。
	~ConfigMgr() = default;

	SectionInfo GetSection(const std::string& section) const {
		std::map<std::string, SectionInfo>::const_iterator iter = sections_.find(section);

		if (iter == sections_.end()) {
			return SectionInfo();
		}

		return iter->second;
	}

	// 让调用方可以写 config["StatusServer"]。
	SectionInfo operator[](const std::string& section) const {
		return GetSection(section);
	}

	// 函数内的 static 对象只会创建一次，全项目共用这一份配置。
	static ConfigMgr& GetInstance() {
		static ConfigMgr config_manager;
		return config_manager;
	}

	// 禁止复制，避免不小心产生第二份配置。
	ConfigMgr(const ConfigMgr&) = delete;
	ConfigMgr& operator=(const ConfigMgr&) = delete;

private:
	// 构造函数放在 private，外部只能通过 GetInstance() 获取单例。
	ConfigMgr();

	// key 是分组名，value 是该分组中的所有配置项。
	std::map<std::string, SectionInfo> sections_;
};
