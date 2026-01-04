// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/*
* Floppy disk images unpack plugin for the Total Commander.
* Copyright (c) 2002, IvGzury ( ivgzury@hotmail.com )
* Copyright (c) 2022-2026, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
* Oleg Farenyuk's code is released under the MIT License.
*
* Original IvGzury copyright message:
* This program is absolutely free software.
* If you have any remarks or problems, please don't
* hesitate to send me an email.
*/
#include "sysio_winapi.h"


bool file_exists(const char* path) {
	DWORD attr = GetFileAttributesA(path);
	return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

bool create_sized_file(const char* path, size_t size, uint8_t fill_byte) {
	HANDLE hFile = CreateFileA(
		path,
		GENERIC_WRITE,
		0,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	const size_t chunk_size = 1024*1024;
	uint8_t* buffer = new uint8_t[chunk_size];
	memset(buffer, fill_byte, chunk_size);

	DWORD written = 0;
	size_t total_written = 0;

	while (total_written < size) {
		DWORD to_write = static_cast<DWORD>(std::min(chunk_size, size - total_written));
		if (!WriteFile(hFile, buffer, to_write, &written, nullptr)) {
			CloseHandle(hFile);
			return false;
		}
		total_written += written;
	}

	CloseHandle(hFile);
	return true;
}

// INVALID_HANDLE_VALUE on error
file_handle_t open_file_shared_read(const char* filename) {
	file_handle_t handle;
	handle = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	return handle;
}

//! Opens for reading but allows writing by others
file_handle_t open_file_read_shared_write(const char* filename) {
	file_handle_t handle;
	handle = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
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
	// TODO: костиль, поф≥ксити, коли виправлю TODO в DeleteFiles
	BY_HANDLE_FILE_INFORMATION info;
	if (GetFileInformationByHandle(handle, &info)) {
		return CloseHandle(handle);
	}
	else return true;
}

bool flush_file(file_handle_t handle) {
	return FlushFileBuffers(handle);
}

bool delete_file(const char* filename) {
	DWORD attrs = GetFileAttributes(filename);
	if (attrs == INVALID_FILE_ATTRIBUTES) 
		return false;

	if (attrs & FILE_ATTRIBUTE_READONLY) {
		// TODO: Add asking user for confirmation?
		SetFileAttributes(filename, attrs & ~FILE_ATTRIBUTE_READONLY);
	}
	return DeleteFile(filename);
}

bool delete_dir(const char* filename)
{
	DWORD attrs = GetFileAttributes(filename);
	if (attrs == INVALID_FILE_ATTRIBUTES) return false;

	if (attrs & FILE_ATTRIBUTE_READONLY) {
		SetFileAttributes(filename, attrs & ~FILE_ATTRIBUTE_READONLY);
	}
	return RemoveDirectory(filename);
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

void SystemTimeToFatFs(const SYSTEMTIME& st, uint16_t* pdate, uint16_t* ptime) {
	// FatFS time encoding: 
	// Bits 15-11: hour (0Ц23)
	// Bits 10-5 : minute (0Ц59)
	// Bits 4-0  : second/2 (0Ц29)
	*ptime = (WORD)((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2));

	// FatFS date encoding:
	// Bits 15-9: year from 1980 (0Ц127 for 1980Ц2107)
	// Bits 8-5 : month (1Ц12)
	// Bits 4-0 : day (1Ц31)
	*pdate = (WORD)(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);
}

std::pair<uint16_t, uint16_t> get_file_datatime_for_FatFS(const char* filename) {
	bool is_dir_v = is_dir(filename);
	HANDLE handle = CreateFileA( filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 
		is_dir_v ? FILE_FLAG_BACKUP_SEMANTICS : 0, NULL);

	if (handle == INVALID_HANDLE_VALUE) {
		return { -1, -1 };
	}
	FILETIME ftWrite;
	if (!GetFileTime(handle, NULL, NULL, &ftWrite)) {
		CloseHandle(handle);
		return { -1, -1 };
	}
	SYSTEMTIME stUTC, stLocal;
	FileTimeToSystemTime(&ftWrite, &stUTC);
	SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal); // Is it OK?

	std::pair<uint16_t, uint16_t> res; 
	SystemTimeToFatFs(stLocal, &res.first, &res.second);
	CloseHandle(handle);
	return res;
}

uint32_t get_current_datatime_for_FatFS() {
	SYSTEMTIME tm, stLocal;

	/* Get local time */
	GetLocalTime(&tm); 
	SystemTimeToTzSpecificLocalTime(NULL, &tm, &stLocal); // Is it OK?

	/* Pack date and time into a DWORD variable */
	return   (tm.wYear - 1980) << 25 | tm.wMonth << 21 | tm.wDay << 16 | tm.wHour << 11 | tm.wMinute << 5 | tm.wSecond >> 1;
}

bool set_file_attributes(const char* filename, uint32_t attribute){
	return SetFileAttributes(filename, attribute);
}

uint32_t get_file_attributes(const char* filename)
{
	return GetFileAttributes(filename);
}

bool is_dir(const char* filename)
{
	auto res = get_file_attributes(filename);
	if (res == INVALID_FILE_ATTRIBUTES)
		return false; // Is this enough?
	return  res & FILE_ATTRIBUTE_DIRECTORY;
}

bool check_is_RO(uint32_t attr) {
	return attr & FILE_ATTRIBUTE_READONLY;
}

bool check_is_Hidden(uint32_t attr) {
	return attr & FILE_ATTRIBUTE_HIDDEN;
}

bool check_is_System(uint32_t attr) {
	return attr & FILE_ATTRIBUTE_SYSTEM;
}

bool check_is_Archive(uint32_t attr) {
	return attr & FILE_ATTRIBUTE_ARCHIVE;
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
