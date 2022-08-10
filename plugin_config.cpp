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
    fprintf(cf, "# Highly specialized exceptions for the popular images found on the Internet:\n");
    fprintf(cf, "process_DOS1xx_exceptions=%x\n", process_DOS1xx_exceptions);
    fprintf(cf, "# WinImage-like behavior -- if boot is not on the beginning of the file, \n");
    fprintf(cf, "# # search it by the pattern 0xEB 0xXX 0x90 .... 0x55 0xAA\n");
    fprintf(cf, "search_for_boot_sector=%x\n", search_for_boot_sector);
    fprintf(cf, "search_for_boot_sector_range=%zx\n", search_for_boot_sector_range);
    fprintf(cf, "allow_dialogs=%x\n", allow_dialogs);
    fprintf(cf, "allow_txt_log=%x\n", allow_txt_log);
    log_file_path.push_back("D:\\Temp\\fatimg.txt");
    fprintf(cf, "log_file_path=%s\n", log_file_path.data());
    fprintf(cf, "debug_level=%x\n\n", debug_level);

    std::fclose(cf);
    return true;
}

