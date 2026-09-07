#include "ConfigMgr.h"

#include <filesystem>
#include <utility>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "Logger.h"

ConfigMgr& ConfigMgr::Inst()
{
	static ConfigMgr config_manager;
	return config_manager;
}

ConfigMgr::ConfigMgr()
{
	const std::filesystem::path config_path =
		std::filesystem::current_path() / "config.ini";
	LOG_INFO("Loading ChatServer configuration: ", config_path.string());

	boost::property_tree::ptree property_tree;
	boost::property_tree::read_ini(config_path.string(), property_tree);

	for (const auto& section_pair : property_tree)
	{
		SectionInfo section;
		for (const auto& key_value_pair : section_pair.second)
		{
			section.values[key_value_pair.first] =
				key_value_pair.second.get_value<std::string>();
		}
		config_map_[section_pair.first] = std::move(section);
	}
}

ConfigMgr::~ConfigMgr()
{
	LOG_DEBUG("ConfigMgr Meyers singleton destroyed.");
}

SectionInfo ConfigMgr::GetSection(const std::string& section) const
{
	const auto iter = config_map_.find(section);
	return iter == config_map_.end() ? SectionInfo{} : iter->second;
}

SectionInfo ConfigMgr::operator[](const std::string& section) const
{
	return GetSection(section);
}
