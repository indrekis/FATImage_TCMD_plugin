// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/*
* Floppy disk images unpack plugin for the Total Commander.
* Copyright (c) 2002, IvGzury ( ivgzury@hotmail.com )
* Copyright (c) 2022, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
* Oleg Farenyuk's code is released under the MIT License.
*
* Original IvGzury copyright message:
* This program is absolutely free software.
* If you have any remarks or problems, please don't
* hesitate to send me an email.
*/
#include "sysio_winapi.h"

file_handle_t open_file_shared_read(const char* filename) {
	file_handle_t handle;
	handle = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	return handle;
}

file_handle_t open_file_write(const char* filename) {
	file_handle_t handle;
	handle = CreateFile(filename, GENERIC_WRITE | FILE_APPEND_DATA, FILE_SHARE_READ, 0, CREATE_NEW, 0, 0);
	return handle;
}

file_handle_t open_file_overwrite(const char* filename) {
	file_handle_t handle;
	handle = CreateFile(filename, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
	return handle;
}

//! Returns true if success
bool close_file(file_handle_t handle) {
	return CloseHandle(handle);
}

bool flush_file(file_handle_t handle) {
	return FlushFileBuffers(handle);
}

bool delete_file(const char* filename) {
	return DeleteFile(filename);
}

bool get_temp_filename(char* buff, const char prefix[]) {
	char dest_path[MAX_PATH];
	auto dwRetVal = GetTempPath(MAX_PATH,  // length of the buffer
		dest_path); // buffer for path 
	if (dwRetVal > MAX_PATH || (dwRetVal == 0))
	{
		return false;
	}
	dwRetVal = GetTempFileName(dest_path, // directory for tmp files
		prefix,     // temp file name prefix -- 3 bytes used only
		0,                // create unique name 
		buff);  // buffer for name 
	if (dwRetVal == 0) {
		return false;
	}
	return true;
}

//! Returns true if success
bool set_file_pointer(file_handle_t handle, size_t offset) {
	LARGE_INTEGER offs;
	offs.QuadPart = offset;
	return SetFilePointerEx(handle, offs, nullptr, FILE_BEGIN);
}

size_t read_file(file_handle_t handle, void* buffer_ptr, size_t size) {
	bool res;
	DWORD result = 0;
	res = ReadFile(handle, buffer_ptr, static_cast<DWORD>(size), &result, nullptr); //-V2001
	if (!res) {
		return static_cast<size_t>(-1);
	}
	else {
		return static_cast<size_t>(result);
	}
}

size_t write_file(file_handle_t handle, const void* buffer_ptr, size_t size) {
	bool res;
	DWORD result = 0;
	res = WriteFile(handle, buffer_ptr, static_cast<DWORD>(size), &result, nullptr); //-V2001
	if (!res) {
		return static_cast<size_t>(-1);
	}
	else {
		return static_cast<size_t>(result);
	}
}

bool set_file_datetime(file_handle_t handle, uint32_t file_datetime)
{
	FILETIME LocTime, GlobTime;
	DosDateTimeToFileTime(static_cast<uint16_t>(file_datetime >> 16),
		static_cast<uint16_t>(file_datetime & static_cast<uint32_t>(0xFFFF)),
		&LocTime);
	LocalFileTimeToFileTime(&LocTime, &GlobTime);
	SetFileTime(handle, nullptr, nullptr, &GlobTime);
	return true; // TODO: Add error handling
}

bool set_file_attributes(const char* filename, uint32_t attribute){
	return SetFileAttributes(filename, attribute);
}

size_t get_file_size(const char* filename)
{
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (!GetFileAttributesEx(filename, GetFileExInfoStandard, &fad))
		return -1; 
	LARGE_INTEGER size;
	size.HighPart = fad.nFileSizeHigh;
	size.LowPart = fad.nFileSizeLow;
	return size.QuadPart;
}

size_t get_file_size(file_handle_t handle)
{
	LARGE_INTEGER size;
	if (!GetFileSizeEx(handle, &size))
		return -1;

	return size.QuadPart;
}

uint32_t get_current_datetime()
{
	SYSTEMTIME t = {0};
	FILETIME   ft = { 0 };
	uint16_t  dft[2] = { 0 };
	GetSystemTime(&t);
	SystemTimeToFileTime(&t, &ft);
	FileTimeToDosDateTime(&ft, dft+1, dft);
	return (static_cast<uint32_t>(dft[1]) << 16) + dft[0];
}

char simple_ucs16_to_local(wchar_t wc) {
	char tc = '\0';
	WideCharToMultiByte(CP_ACP, 0, &wc, -1, &tc, 1, NULL, NULL); 
	return tc;
}

int ucs16_to_local(char* outstr, const wchar_t* instr, size_t maxoutlen) {
	if (instr != nullptr) {
		// CP_ACP The system default Windows ANSI code page.
		// TODO: error analysis
		int res = WideCharToMultiByte(CP_ACP, 0, instr, static_cast<int>(maxoutlen), 
			                          outstr, static_cast<int>(maxoutlen), NULL, NULL);
		if (res != 0)
			return 0;
		else
			return GetLastError();
	}
	else {
		return 1;
	}
}
