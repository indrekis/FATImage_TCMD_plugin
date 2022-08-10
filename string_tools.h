/*
* Copyright (c) 2017-2022, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
* The code is released under the MIT License.
*/

#ifndef TEST_STRING_TOOLS_HPP
#define TEST_STRING_TOOLS_HPP

#include <string>
#include <cassert>
#include <stdexcept>
#include <type_traits>

// #define  STRING_TOOLS_USE_STRINGSTREAM 
#ifdef STRING_TOOLS_USE_STRINGSTREAM
#include <sstream>
#endif 

std::string trim(std::string arg);

template<typename T>
auto from_str(const std::string& arg){
    T res = {}; 
    size_t last_sym = 0;
    if constexpr (std::is_same_v<T, unsigned long long>) {
        res = std::stoull(arg, &last_sym);
    } 
    else if constexpr (std::is_same_v<T, bool>) {
        res = static_cast<bool>(std::stoi(arg, &last_sym));
    }
    else if constexpr (std::is_same_v<T, int>) {
        res = std::stoi(arg, &last_sym);
    }
    else if constexpr (std::is_same_v<T, std::remove_cvref_t<decltype(arg)>>) {
        return arg; 
    }
    else {
#if defined STRING_TOOLS_USE_STRINGSTREAM 
        //! Slow but universal
        T res = T{};
        std::stringstream ss{ arg };
        ss >> res;
        if (std::string temp; ss >> temp) {
            throw std::runtime_error{ "Wrong numerical value: " + arg };
        }
        return res;
#else
        assert(false && "Not implemented"); // Get rid of stringstream
#endif 
    }

    if (last_sym != arg.size()) {
        throw std::runtime_error{ "Wrong numerical value: " + arg };
    }
    
    return res;    
}

#endif //TEST_STRING_TOOLS_HPP
