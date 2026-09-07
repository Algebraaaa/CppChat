#include "ConfigMgr.h"

#include <boost/filesystem.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "Logger.h"

ConfigMgr::ConfigMgr() {
	// 程序从“当前工作目录”寻找 config.ini。
	// Visual Studio 构建后会把 config.ini 复制到 exe 所在目录。
	const boost::filesystem::path current_path = boost::filesystem::current_path();
	const boost::filesystem::path config_path = current_path / "config.ini";
	LOG_INFO("Loading configuration file: ", config_path.string());

	// PropertyTree 负责解析 INI 文本。
	// 文件不存在或格式错误时会抛出异常，最后由 main() 统一记录。
	boost::property_tree::ptree property_tree;
	boost::property_tree::read_ini(config_path.string(), property_tree);

	std::size_t key_count = 0;

	// 外层循环读取 [StatusServer]、[ChatServer1] 等分组。
	// 这里保留 auto，是因为 PropertyTree 的元素类型很长；右侧变量名已经说明含义。
	for (const auto& section_pair : property_tree) {
		const std::string& section_name = section_pair.first;
		const boost::property_tree::ptree& section_tree = section_pair.second;

		// 内层循环读取当前分组中的 Host=...、Port=...。
		std::map<std::string, std::string> section_values;
		for (const auto& key_value_pair : section_tree) {
			const std::string& key = key_value_pair.first;
			const std::string value = key_value_pair.second.get_value<std::string>();
			section_values[key] = value;
			++key_count;
		}

		SectionInfo section_info;
		section_info.values_ = section_values;
		sections_[section_name] = section_info;

		LOG_DEBUG("Loaded configuration section [", section_name,
			"] with ", section_values.size(), " keys");
	}

	LOG_INFO("Configuration loaded successfully: sections=", sections_.size(),
		", keys=", key_count);
}
