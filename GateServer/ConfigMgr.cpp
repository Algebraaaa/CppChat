#include "ConfigMgr.h"

#include <boost/filesystem.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "Logger.h"

ConfigMgr::ConfigMgr() {
	// 获取当前工作目录  
	boost::filesystem::path current_path = boost::filesystem::current_path();
	// 构建config.ini文件的完整路径  
	boost::filesystem::path config_path = current_path / "config.ini";
	LOG_INFO("Loading configuration file: ", config_path.string());

	// 使用Boost.PropertyTree来读取INI文件  
	boost::property_tree::ptree pt;
	boost::property_tree::read_ini(config_path.string(), pt);


	// 遍历INI文件中的所有section  
	for (const auto& section_pair : pt) {
		const std::string& section_name = section_pair.first;
		const boost::property_tree::ptree& section_tree = section_pair.second;

		// 对于每个section，遍历其所有的key-value对  
		std::map<std::string, std::string> section_config;
		for (const auto& key_value_pair : section_tree) {
			const std::string& key = key_value_pair.first;
			const std::string& value = key_value_pair.second.get_value<std::string>();
			section_config[key] = value;
		}
		SectionInfo sectionInfo;
		sectionInfo._section_datas = section_config;
		// 将section的key-value对保存到config_map中  
		_config_map[section_name] = sectionInfo;
	}

	// ConfigMgr 是迈耶斯单例，没有继承 Singleton<T>，因此在自身构造函数中记日志。
	LOG_INFO(
		"ConfigMgr singleton initialized successfully: sections=",
		_config_map.size());
}

ConfigMgr::~ConfigMgr()
{
	// 析构函数体执行后，_config_map 会按照 RAII 自动释放。
	LOG_INFO("ConfigMgr singleton destroyed successfully.");
}
