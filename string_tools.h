/*
* Copyright (c) 2017-2022, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
* The code is released under the MIT License.
*/

#ifndef TEST_STRING_TOOLS_HPP
#define TEST_STRING_TOOLS_HPP

#include <string>
#include <sstream>

std::string trim(std::string arg);

//! Slow but universal
template<typename T>
auto from_str(const std::string& arg){
    T res = T{};
    std::stringstream ss{arg};
    ss >> res;
    if( std::string temp; ss >> temp ){
        throw std::runtime_error{"Wrong numerical value: " + arg};
    }
    return res;
}

//! Optimised -- for ull only
//! Specialization of function templates is a suspicious and dangerous thing, although sometimes it is necessary.
//! It seems to me that this is a rare exception when it is useful. (Inline is necessary here).
template<>
inline auto from_str<unsigned long long>(const std::string& arg){
    unsigned long long res = 0;
    size_t last_sym = 0;
    res = std::stoull(arg, &last_sym);
    if( last_sym != arg.size() ){
        throw std::runtime_error{"Wrong numerical value: " + arg};
    }
    return res;
}

#endif //TEST_STRING_TOOLS_HPP
