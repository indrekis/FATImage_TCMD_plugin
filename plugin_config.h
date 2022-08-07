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
#include "sysio_winapi.h"
#endif 
#include "wcxhead.h"


struct plugin_config_t {
	minimal_fixed_string_t<MAX_PATH> config_file_path;
	uint32_t plugin_interface_version_lo = 0;
	uint32_t plugin_interface_version_hi = 0;
	bool ignore_boot_signature = true;	   // Some historical floppy images contain correct BPB but do not have 0x55AA signature
										   // Examples are Norton Utilities 2.00 and 2.01; CheckIt Pro v1.11 (3.5-1.44mb)	
#if defined FLTK_ENABLED_EXPERIMENTAL && !defined NDEBUG
	bool allow_dialogs = true;
	bool allow_txt_log = true;
#else 
	bool allow_dialogs = false;
	bool allow_txt_log = false;
#endif
	minimal_fixed_string_t<MAX_PATH> log_file_path;
	file_handle_t log_file = file_handle_t();

	bool use_VFAT = true;
	bool process_DOS1xx_images = true;
	bool process_MBR = true;
	bool process_DOS1xx_exceptions = true; // Highly specialized exceptions for the popular images found on the Internet
	bool search_for_boot_sector = true;	   // WinImage-like behavior -- if boot is not on the beginning of the file,
										   // search it by the pattern 0xEB 0xXX 0x90 .... 0x55 0xAA
	size_t search_for_boot_sector_range = 64 * 1024;

	//! Enum is not convenient here because of I/O
	static constexpr int NO_DEBUG     = 0;
	static constexpr int DEBUGGER_MSG = 1;
	static constexpr int GUI_MSG      = 2;
	int debug_level = NO_DEBUG;
	//------------------------------------------------------------
	bool read_conf (const PackDefaultParamStruct* dps, bool reread);
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

public:
	template<typename... Args>
	void log_print(const char* format, Args&&... args) {
		if (allow_txt_log) {
			log_print_f(log_file, format, std::forward<Args>(args)...);
		}
	}

	template<typename... Args>
	void log_print_dbg(const char* format, Args&&... args) {
		debug_print(format, std::forward<Args>(args)...);
		log_print(format, std::forward<Args>(args)...);
	}

};

#endif 