// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/*
* Copyright (c) 2017-2022, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
* The code is released under the MIT License.
*/

#include "plugin_config.h"
#include "string_tools.h"

#ifdef _WIN32
#include "sysio_winapi.h"
#endif 

#include <algorithm>
#include <iostream>
#include <fstream>
#include <cassert>

plugin_config_t::parse_string_ret_t
plugin_config_t::parse_string(std::string arg) { // Note: we need copy here -- let compiler create it for us
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

void plugin_config_t::file_to_options_map(std::istream& cf) {
    std::string line;
    while (std::getline(cf, line)) {
        auto pr = parse_string(line);
        if (pr.first.empty()) {
            if (!pr.second)
                continue;
            else
                throw std::runtime_error{ "Wrong config line -- no option name: " + line }; // "=..."
        }
        else if (!pr.second) {
            throw std::runtime_error{ "Wrong config line -- no '=': " + line }; // "abc" -- no '='
        }
        else if (pr.second->empty()) {
            throw std::runtime_error{ "Wrong config option value: " + line }; // "key="
        }
        if (options_map.count(pr.first)) {
            throw std::runtime_error{ "Duplicated option name: " + pr.first + " = "
                                         + *pr.second + "; prev. val: " + options_map[pr.first] };
        }
        options_map[pr.first] = *pr.second;
    }
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


bool plugin_config_t::read_conf(const PackDefaultParamStruct* dps)
{
    using namespace std::literals::string_literals; 

    config_file_path.push_back(dps->DefaultIniName);
    auto slash_idx = config_file_path.find_last(get_path_separator());
    if (slash_idx == config_file_path.npos)
        slash_idx = 0;
    config_file_path.shrink_to(slash_idx);
    config_file_path.push_back(inifilename);

    plugin_interface_version_hi = dps->PluginInterfaceVersionHi;
    plugin_interface_version_lo = dps->PluginInterfaceVersionLow;

    std::ifstream cf{ config_file_path.data() };
    if (!cf.is_open()) {
        return false; // Use default configuration
    }

    try {
        file_to_options_map(cf);
        ignore_boot_signature = get_option_from_map<decltype(ignore_boot_signature)>("ignore_boot_signature"s);
        use_VFAT = get_option_from_map<decltype(use_VFAT)>("use_VFAT"s);
        process_DOS1xx_images = get_option_from_map<decltype(process_DOS1xx_images)>("process_DOS1xx_images"s);
        process_MBR = get_option_from_map<decltype(process_MBR)>("process_MBR"s);
        process_DOS1xx_exceptions = get_option_from_map<decltype(process_DOS1xx_exceptions)>("process_DOS1xx_exceptions"s);
        search_for_boot_sector = get_option_from_map<decltype(search_for_boot_sector)>("search_for_boot_sector"s);
        search_for_boot_sector_range = get_option_from_map<decltype(search_for_boot_sector_range)>("search_for_boot_sector_range"s);
        allow_dialogs = get_option_from_map<decltype(allow_dialogs)>("allow_dialogs"s);
        allow_GUI_log = get_option_from_map<decltype(allow_GUI_log)>("allow_GUI_log"s);
        debug_level = get_option_from_map<decltype(debug_level)>("debug_level"s);
    }
    catch (std::exception&) {
        return false; // Wrong configuration would be overwritten by default configuration.
    }
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

    std::ofstream cf{ config_file_path.data() };
    if (!cf.is_open()) {
        return false;
    }

    cf << "[FAT_disk_img_plugin]\n";
    cf << "ignore_boot_signature=" << ignore_boot_signature << '\n';
    cf << "use_VFAT=" << use_VFAT << '\n';
    cf << "process_DOS1xx_images=" << process_DOS1xx_images << '\n';
    cf << "process_MBR=" << process_MBR << '\n';
    cf << "# Highly specialized exceptions for the popular images found on the Internet:\n";
    cf << "process_DOS1xx_exceptions=" << process_DOS1xx_exceptions << '\n';
    cf << "# WinImage-like behavior -- if boot is not on the beginning of the file, \n";
    cf << "# search it by the pattern 0xEB 0xXX 0x90 .... 0x55 0xAA\n";
    cf << "search_for_boot_sector=" << search_for_boot_sector << '\n';
    cf << "search_for_boot_sector_range=" << search_for_boot_sector_range << '\n'; //-V128
    cf << "allow_dialogs=" << allow_dialogs << '\n'; 
    cf << "allow_GUI_log=" << allow_GUI_log << '\n'; 
    cf << "debug_level=" << debug_level << '\n'; 
    cf << '\n';
    return true;
}

