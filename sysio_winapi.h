#pragma once

#ifndef SYSIO_WINAPI_H_INCLUDED
#define SYSIO_WINAPI_H_INCLUDED

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#define NOMINMAX
#include <windows.h>
#include <cstdint>

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

#endif 

