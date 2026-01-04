// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/*
* Floppy disk images unpack plugin for the Total Commander.
* Copyright (c) 2017-2026, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
* The code is released under the MIT License.
*/

#include "plugin_config.h"
#include "string_tools.h"

#ifdef _WIN32
#include "sysio_winapi.h"
#endif 

#include <algorithm>
#include <memory>
#include <cassert>
#include <optional>
#include <clocale>

namespace {
    using parse_string_ret_t = std::pair<std::string, std::optional<std::string>>;

    parse_string_ret_t parse_string(std::string arg) { // Note: we need copy here -- let compiler create it for us
        constexpr char separator = '='; // Just for the readability
        constexpr char commenter1 = '#';
        constexpr char commenter2 = '['; // Skip sections for now

        auto comment_pos = arg.find(commenter1);
        if (comment_pos != std::string::npos)
            arg.erase(comment_pos); //! Remove comment
        comment_pos = arg.find(commenter2);
        if (comment_pos != std::string::npos)
            arg.erase(comment_pos); //! Remove sections

        auto sep_pos = arg.find(separator);
        if (sep_pos == std::string::npos) {
            return parse_string_ret_t{ trim(arg), std::nullopt };
        }
        auto left_part = arg.substr(0, sep_pos);
        auto right_part = arg.substr(sep_pos + 1, std::string::npos);
        return parse_string_ret_t{ trim(left_part), trim(right_part) };
    }

}



int plugin_config_t::file_to_options_map(std::FILE* cf) {
    using namespace std::string_literals;
    int line_size = 1024; // Limit for the string 
    std::unique_ptr<char[]> line_ptr = std::make_unique<char[]>(line_size);
    while ( std::fgets(line_ptr.get(), line_size, cf) ) {
        auto pr = parse_string(line_ptr.get());
        if (pr.first.empty()) {
            if (!pr.second)
                continue;
            else
                return 1;  // throw std::runtime_error{ "Wrong config line -- no option name: "s + line_ptr.get() }; // "=..."
        }
        else if (!pr.second) {
            return 2;      // throw std::runtime_error{ "Wrong config line -- no '=': "s + line_ptr.get() }; // "abc" -- no '='
        }
        else if (pr.second->empty()) {
            return 3;      // throw std::runtime_error{ "Wrong config option value: "s + line_ptr.get() }; // "key="
        }
        if (options_map.count(pr.first)) {
            return 4;      // throw std::runtime_error{ "Duplicated option name: "s + pr.first + " = " //-V112
                           //             + *pr.second + "; prev. val: " + options_map[pr.first] };
        }
        options_map[pr.first] = *pr.second;
    }
    return 0;
}

template<typename T>
T plugin_config_t::get_option_from_map(const std::string& option_name) const {
    if (options_map.count(option_name) == 0) {
        throw std::runtime_error("Option not found: " + option_name); 
    }
    auto elem_itr = options_map.find(option_name);
    if (elem_itr != options_map.end()) {
        return from_str<T>(elem_itr->second);
    }
    else {
        throw std::runtime_error{ "Option " + option_name + " not found" };
    }
}


bool plugin_config_t::read_conf(const PackDefaultParamStruct* dps, bool reread)
{
    std::setlocale(LC_ALL, "");
    using namespace std::literals::string_literals; 
    if (!reread) {
        config_file_path.push_back(dps->DefaultIniName);
        auto slash_idx = config_file_path.find_last(get_path_separator());
        if (slash_idx == config_file_path.npos)
            slash_idx = 0;
        config_file_path.shrink_to(slash_idx);
        config_file_path.push_back(inifilename);

        plugin_interface_version_hi = dps->PluginInterfaceVersionHi;
        plugin_interface_version_lo = dps->PluginInterfaceVersionLow;
    }

    std::FILE* cf = std::fopen( config_file_path.data(), "r");
    if (!cf) {
        return false; // Use default configuration
    }

    auto res = file_to_options_map(cf);
    if (res) {
        std::fclose(cf);
        return false; // Wrong configuration would be overwritten by default configuration.
    }
    try {
        ignore_boot_signature = get_option_from_map<decltype(ignore_boot_signature)>("ignore_boot_signature"s);
        use_VFAT = get_option_from_map<decltype(use_VFAT)>("use_VFAT"s);
        process_DOS1xx_images = get_option_from_map<decltype(process_DOS1xx_images)>("process_DOS1xx_images"s);
        process_MBR = get_option_from_map<decltype(process_MBR)>("process_MBR"s);
        process_DOS1xx_exceptions = get_option_from_map<decltype(process_DOS1xx_exceptions)>("process_DOS1xx_exceptions"s);
        search_for_boot_sector = get_option_from_map<decltype(search_for_boot_sector)>("search_for_boot_sector"s);
        search_for_boot_sector_range = get_option_from_map<decltype(search_for_boot_sector_range)>("search_for_boot_sector_range"s);
        allow_dialogs = get_option_from_map<decltype(allow_dialogs)>("allow_dialogs"s);
        allow_txt_log = get_option_from_map<decltype(allow_txt_log)>("allow_txt_log"s);
        debug_level = get_option_from_map<decltype(debug_level)>("debug_level"s);

		max_depth = get_option_from_map<decltype(max_depth)>("max_depth"s);
		max_invalid_chars_in_dir = get_option_from_map<decltype(max_invalid_chars_in_dir)>("max_invalid_chars_in_dir"s);

        //=========new_arc============================================
        new_arc.single_part = get_option_from_map<decltype(new_arc.single_part)>("new_arc_single_part"s);
        new_arc.custom_unit = get_option_from_map<decltype(new_arc.custom_unit)>("new_arc_custom_unit"s);
        new_arc.custom_value = get_option_from_map<decltype(new_arc.custom_value)>("new_arc_custom_value"s);
        new_arc.single_fs = get_option_from_map<decltype(new_arc.single_fs)>("new_arc_single_fs"s);
        for (int i = 0; i < 4; ++i) {
            new_arc.multi_values[i] = get_option_from_map<size_t>("new_arc_multi_values_"s + std::to_string(i));
            new_arc.multi_units[i] = get_option_from_map<int>("new_arc_multi_units_"s + std::to_string(i));
            new_arc.multi_fs[i] = get_option_from_map<int>("new_arc_multi_fs_"s + std::to_string(i));
        }
        new_arc.total_unit = get_option_from_map<decltype(new_arc.total_unit)>("new_arc_total_unit"s);
        new_arc.total_value = get_option_from_map<decltype(new_arc.total_value)>("new_arc_total_value"s);
        new_arc.save_config = get_option_from_map<decltype(new_arc.save_config)>("new_arc_save_config"s);
        //============================================================

        if (allow_txt_log && log_file_path.is_empty()) {
            auto tstr = get_option_from_map<std::string>("log_file_path"s);
            log_file_path.clear();
            log_file_path.push_back(tstr.data());

            log_file = open_file_overwrite(log_file_path.data());
            if (log_file == file_open_error_v) {
                allow_txt_log = false;
            }
        }
    }
    catch (std::exception& ex) {
        (void)ex;
        std::fclose(cf);
        return false; 
    }
       
    std::fclose(cf);
    options_map = options_map_t{}; // Optimization -- options_map.clear() can leave allocated internal structures

    return validate_conf();
}

bool plugin_config_t::validate_conf() {
    if (debug_level > 2)
        return false;
    return true;
}

bool plugin_config_t::write_conf()
{
    assert(validate_conf() && "Inconsisten fatdiskimg plugin configuration");

    std::FILE* cf = std::fopen(config_file_path.data(), "w");

    //std::ofstream cf{ config_file_path.data() };
    if ( !cf ) {
        return false;
    }
    fprintf(cf, "[FAT_disk_img_plugin]\n");
    fprintf(cf, "ignore_boot_signature=%x\n", ignore_boot_signature);
    fprintf(cf, "use_VFAT=%x\n", use_VFAT);
    fprintf(cf, "process_DOS1xx_images=%x\n", process_DOS1xx_images);
    fprintf(cf, "process_MBR=%x\n", process_MBR);
    fprintf(cf, "# Highly specialized exceptions for the popular ancient images found on the Internet:\n");
    fprintf(cf, "process_DOS1xx_exceptions=%x\n", process_DOS1xx_exceptions);
    fprintf(cf, "# WinImage-like behavior -- if boot is not on the beginning of the file, \n");
    fprintf(cf, "# search it by the pattern 0xEB 0xXX 0x90 .... 0x55 0xAA\n");
    fprintf(cf, "search_for_boot_sector=%x\n", search_for_boot_sector);
    fprintf(cf, "search_for_boot_sector_range=%zu\n", search_for_boot_sector_range);
    fprintf(cf, "allow_dialogs=%x\n", allow_dialogs);
    fprintf(cf, "allow_txt_log=%x\n", allow_txt_log);
	// log_file_path.clear();
    // log_file_path.push_back("D:\\Temp\\fatimg.txt");
    fprintf(cf, "log_file_path=%s\n", log_file_path.data());
    fprintf(cf, "debug_level=%d\n", debug_level);
    fprintf(cf, "max_depth=%zu\n", max_depth);
    fprintf(cf, "# Values above the 11 efficiently disables the check. Beware of special value LLDE_OS2_EA = 0xFFFF\n");
    fprintf(cf, "max_invalid_chars_in_dir=%zu\n", max_invalid_chars_in_dir);

    //=========new_arc============================================
    fprintf(cf, "\nnew_arc_single_part=%x\n", new_arc.single_part);
    fprintf(cf, "new_arc_custom_unit=%d\n", new_arc.custom_unit);
    fprintf(cf, "new_arc_custom_value=%zu\n", new_arc.custom_value);
    fprintf(cf, "new_arc_single_fs=%d\n", new_arc.single_fs);
    for (int i = 0; i < 4; ++i) {
        fprintf(cf, "new_arc_multi_values_%d=%zu\n", i, new_arc.multi_values[i]);
        fprintf(cf, "new_arc_multi_units_%d=%d\n", i, new_arc.multi_units[i]);
        fprintf(cf, "new_arc_multi_fs_%d=%d\n", i, new_arc.multi_fs[i]);
    }
    fprintf(cf, "new_arc_total_unit=%d\n", new_arc.total_unit);
    fprintf(cf, "new_arc_total_value=%zu\n", new_arc.total_value);
    fprintf(cf, "new_arc_save_config=%x\n", new_arc.save_config);

    std::fclose(cf);
    return true;
}

const char*  plugin_config_t::new_arc_t::unit_labels[unit_labels_n] = { "Bytes", "512b Sectors", "Kb",    "4Kb Blocks", "Mb" };
const size_t plugin_config_t::new_arc_t::unit_sizes_b[unit_labels_n] = { 1,       512,            1024,    4 * 1024,       1024 * 1024 };
const char*  plugin_config_t::new_arc_t::fdd_sizes_str[fdd_sizes_n] = { "160", "180", "320", "360", "720", "1200", "1440", "2880", "Custom" };
const size_t plugin_config_t::new_arc_t::fdd_sizes_b[fdd_sizes_n] = { 160 * 1024, 180 * 1024, 320 * 1024, 360 * 1024, 720 * 1024, 1200 * 1024, 1440 * 1024, 2880 * 1024, 0 };
const char*  plugin_config_t::new_arc_t::FS_types[FS_types_n] = { "None", "FAT12", "FAT16", "FAT32", "Auto"};
