/*
* Floppy disk images unpack plugin for the Total Commander.
* Copyright (c) 2022-2025, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
* The code is released under the MIT License.
*/

#ifndef PLUGIN_CONFIG_H_INCLUDED
#define PLUGIN_CONFIG_H_INCLUDED

#include "minimal_fixed_string.h"

#include <string>
#include <map>
#include <cstdio>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#ifndef NOMINMAX
#define NOMINMAX
#endif
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

	size_t max_depth = 100; // Maximum recursion depth for the FAT directory tree

	size_t max_invalid_chars_in_dir = 0; // Values above 11 efficiently disable the check for invalid characters in directory names

	//! Enum is not convenient here because of I/O
	static constexpr int NO_DEBUG     = 0;
	static constexpr int DEBUGGER_MSG = 1;
	static constexpr int GUI_MSG      = 2;
	int debug_level = NO_DEBUG;
	//------------------------------------------------------------
	bool read_conf (const PackDefaultParamStruct* dps, bool reread);
	bool write_conf();

	struct new_arc_t {
		static const int unit_labels_n = 5;
		static const char* unit_labels[unit_labels_n];
		static size_t      unit_sizes_b[unit_labels_n];
		enum unit_ids { unit_b, unit_sec, unit_kb, unit_4kb, unit_mb };
		static const int fdd_sizes_n = 9;
		static const char* fdd_sizes_str[fdd_sizes_n];
		static size_t      fdd_sizes_b[fdd_sizes_n];
		static const int FS_types_n = 5;
		static const char* FS_types[FS_types_n];

		bool single_part = true;
		int custom_unit = unit_kb;
		size_t custom_value = 1440; // fdd_sizes_b[unit_kb] / unit_sizes_b[unit_kb];
		int single_fs = 2; // 0 - do not create, 1 - FAT12, 2 - FAT16, 3 - FAT32, 4 - Auto-detect

		std::array<size_t, 4> multi_values;
		std::array<int, 4> multi_units;
		std::array<int, 4> multi_fs;
		int    total_unit = -1;
		size_t total_value = -1;
		bool save_config = false;
		
		static size_t unit_factor(int unit) {
			return unit_sizes_b[unit];
		}
		new_arc_t() = default;
	} new_arc{};

private:
	using options_map_t = std::map<std::string, std::string>;

	int file_to_options_map(std::FILE* cf);

	template<typename T>
	T get_option_from_map(const std::string& option_name) const;

	bool validate_conf();

	options_map_t options_map;

	constexpr static const char* inifilename = "\\fatdiskimg.ini";

public:
	template<typename... Args>
	void log_print(const char* format, const Args&... args) {
		if (allow_txt_log) {
			log_print_f(log_file, format, args...);
		}
	}

	template<typename... Args>
	void log_print_dbg(const char* format, const Args&... args) {
		debug_print(format, args...);
		log_print(format, args...);
	}

};

extern plugin_config_t plugin_config;

#endif 