#ifndef PLUGIN_CONFIG_H_INCLUDED
#define PLUGIN_CONFIG_H_INCLUDED

#include "minimal_fixed_string.h"

#include <string>
#include <map>
#include <optional>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#define NOMINMAX
#include <windows.h>
#endif 
#include "wcxhead.h"


struct plugin_config_t {
	minimal_fixed_string_t<MAX_PATH> config_file_path;
	uint32_t plugin_interface_version_lo = 0;
	uint32_t plugin_interface_version_hi = 0;
	bool ignore_boot_signature = false;
	bool use_VFAT = true;
	bool process_DOS1xx_images = true;
	bool process_MBR = true;
	bool process_DOS1xx_exceptions = true; // Highly specialized exceptions for the popular images found on the Internet
	bool search_for_boot_sector = true;	   // WinImage-like behavior -- if boot is not on the beginning of the file,
										   // search it by the pattern 0xEB 0xXX 0x90 .... 0x55 0xAA
	size_t search_for_boot_sector_range = 64 * 1024;

	//------------------------------------------------------------
	bool read_conf (const PackDefaultParamStruct* dps);
	bool write_conf();

private:
	using parse_string_ret_t = std::pair<std::string, std::optional<std::string>>;
	using options_map_t = std::map<std::string, std::string>;

	static parse_string_ret_t parse_string(std::string arg);
	void file_to_options_map(std::istream& cf);

	template<typename T>
	T get_option_from_map(const std::string& option_name) const;

	bool validate_conf();

	options_map_t options_map;

	constexpr static const char* inifilename = "\\fatdiskimg.ini";
};


#endif 