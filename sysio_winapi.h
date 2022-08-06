#pragma once

#ifndef SYSIO_WINAPI_H_INCLUDED
#define SYSIO_WINAPI_H_INCLUDED

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#define NOMINMAX
#include <windows.h>
#include <cstdint>

#ifndef NDEBUG
#include <format>
#endif 

const auto file_open_error_v = INVALID_HANDLE_VALUE;
using file_handle_t = HANDLE;

// All functions returning bool returns true on success
file_handle_t open_file_shared_read(const char* filename);
file_handle_t open_file_write(const char* filename);
file_handle_t open_file_overwrite(const char* filename);
bool close_file(file_handle_t handle);
bool delete_file(const char* filename);
bool get_temp_filename(char* buff, const char prefix[]);
bool set_file_pointer(file_handle_t handle, size_t offset);
size_t read_file(file_handle_t handle, void* buffer_ptr, size_t size);
size_t write_file(file_handle_t handle, const void* buffer_ptr, size_t size);
bool set_file_datetime(file_handle_t handle, uint32_t file_datetime);
bool set_file_attributes(const char* filename, uint32_t attribute);
size_t get_file_size(const char* filename);
size_t get_file_size(file_handle_t handle);
inline char get_path_separator() { return '\\'; }

uint32_t get_current_datetime();

//! Very basic, simplistic, function
char simple_ucs16_to_local(wchar_t wc);
//! More advenced:
int ucs16_to_local(char* outstr, const wchar_t* instr, size_t maxoutlen);

template<typename... Args>
void debug_print(const char* format, Args&&... args) {
#ifndef NDEBUG
    try {
        std::string strbuf = std::vformat(format, std::make_format_args(std::forward<Args...>(args)...));
        OutputDebugString(strbuf.c_str()); // Sends a string to the debugger
    }
    catch (std::exception& ex) {
        OutputDebugString(ex.what()); 
    }
    // See also http://www.nirsoft.net/utils/simple_program_debugger.html
#endif // !NDEBUG
}

#endif 


