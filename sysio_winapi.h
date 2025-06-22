/*
* Floppy disk images unpack plugin for the Total Commander.
* Copyright (c) 2022-2025, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
* The code is released under the MIT License.
*/

#pragma once

#ifndef SYSIO_WINAPI_H_INCLUDED
#define SYSIO_WINAPI_H_INCLUDED

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>

#include <exception>
#include <cstdio>

#ifndef NDEBUG
#include <format>
#endif 

const auto file_open_error_v = INVALID_HANDLE_VALUE;
using file_handle_t = HANDLE;

// All functions returning bool returns true on success
file_handle_t open_file_shared_read(const char* filename);
file_handle_t open_file_read_shared_write(const char* filename);
file_handle_t open_file_write(const char* filename);
file_handle_t open_file_overwrite(const char* filename);
bool close_file(file_handle_t handle);
bool flush_file(file_handle_t handle);
bool delete_file(const char* filename);
bool delete_dir(const char* filename);
bool get_temp_filename(char* buff, const char prefix[]);
bool set_file_pointer(file_handle_t handle, size_t offset);
size_t read_file(file_handle_t handle, void* buffer_ptr, size_t size);
size_t write_file(file_handle_t handle, const void* buffer_ptr, size_t size);
bool set_file_datetime(file_handle_t handle, uint32_t file_datetime);
bool set_file_attributes(const char* filename, uint32_t attribute);
uint32_t get_file_attributes(const char* filename);
bool is_dir(const char* filename);
// Those functions are not exceptionally necessary on Windows, but would be useful on other platforms
bool check_is_RO(uint32_t attr);
bool check_is_Hidden(uint32_t attr);
bool check_is_System(uint32_t attr);
bool check_is_Archive(uint32_t attr);
size_t get_file_size(const char* filename);
size_t get_file_size(file_handle_t handle);
inline char get_path_separator() { return '\\'; }

uint32_t get_current_datetime();
std::pair<uint16_t, uint16_t> get_file_datatime_for_FatFS(const char* filename);
uint32_t get_current_datatime_for_FatFS();

//! Very basic, simplistic, function
char simple_ucs16_to_local(wchar_t wc);
//! More advenced:
int ucs16_to_local(char* outstr, const wchar_t* instr, size_t maxoutlen);

// Defect report: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2905r2.html disables forwarding and Args&&
template<typename... Args>
void debug_print(const char* format, const Args&... args) {
#ifndef NDEBUG
    try {
        std::string strbuf = std::vformat(format, std::make_format_args(args...));
        OutputDebugString(strbuf.c_str()); // Sends a string to the debugger
    }
    catch (std::exception& ex) {
        OutputDebugString(ex.what()); 
    }
    // See also http://www.nirsoft.net/utils/simple_program_debugger.html
#endif // !NDEBUG
}

template<typename... Args>
void log_print_f(file_handle_t hnd, const char* format, const Args&... args) {
    try {
        char strbuf[1024 * 4]; //-V112
        auto res = snprintf(strbuf, sizeof(strbuf) - 1, format, args...);
        strcat(strbuf, "\n");
        write_file(hnd, strbuf, strlen(strbuf));
        flush_file(hnd);
    }
    catch (std::exception& ex) {
        OutputDebugString(ex.what());
    }
}


#endif 


