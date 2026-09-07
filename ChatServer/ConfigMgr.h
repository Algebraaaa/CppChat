#pragma once

#include <map>
#include <string>

struct SectionInfo
{
	std::map<std::string, std::string> values;

	std::string GetValue(
		const std::string& key,
		std::string default_value = {}) const
	{
		const auto iter = values.find(key);
		return iter == values.end() ? default_value : iter->second;
	}

	std::string operator[](const std::string& key) const
	{
		return GetValue(key);
	}
};

class ConfigMgr
{
public:
	static ConfigMgr& Inst();

	ConfigMgr(const ConfigMgr&) = delete;
	ConfigMgr& operator=(const ConfigMgr&) = delete;

	SectionInfo GetSection(const std::string& section) const;
	SectionInfo operator[](const std::string& section) const;

private:
	ConfigMgr();
	~ConfigMgr();

	std::map<std::string, SectionInfo> config_map_;
};
