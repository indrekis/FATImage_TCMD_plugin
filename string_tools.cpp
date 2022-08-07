// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/*
* Copyright (c) 2017-2022, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
* The code is released under the MIT License.
*/

#include <algorithm>
#include <cctype>

#include "string_tools.h"

std::string trim(std::string arg) { // Note: we need copy here -- let compiler create it for us
    constexpr auto is_space_priv = [](auto a) { return std::isspace(a); };
    auto last_nonspace = std::find_if_not(arg.rbegin(), arg.rend(), is_space_priv ).base(); 
    if(last_nonspace != arg.end())
        arg.erase(last_nonspace, arg.end());   
    auto first_nonspace = std::find_if_not(arg.begin(), arg.end(), is_space_priv );
    arg.erase(arg.begin(), first_nonspace);
    return arg;
}

