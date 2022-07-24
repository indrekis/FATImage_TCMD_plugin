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

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#define NOMINMAX
#include <windows.h>

#include "wcxhead.h"
#include <new>
#include <memory>
#include <cstddef>
#include <vector>
#include <array>
#include <algorithm>
#include <cassert>
using std::nothrow, std::uint8_t;

#ifdef _WIN32
#define WCX_PLUGIN_EXPORTS
#endif 

#ifdef WCX_PLUGIN_EXPORTS
#define DLLEXPORT __declspec(dllexport)
#define STDCALL __stdcall
#else
#define WCX_API
#define STDCALL
#endif 

// The DLL entry point
BOOL APIENTRY DllMain(HANDLE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
) {
	return TRUE;
}

//----------------I/O functions ---------------
static const auto file_open_error_v = INVALID_HANDLE_VALUE;
using file_handle_t = HANDLE;

static file_handle_t open_file_shared_read(const char* filename) {
	file_handle_t handle;
	handle = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	return handle;
}

static file_handle_t open_file_write(const char* filename) {
	file_handle_t handle;
	handle = CreateFile(filename, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_NEW, 0, 0);
	return handle;
}

static file_handle_t open_file_overwrite(const char* filename) {
	file_handle_t handle;
	handle = CreateFile(filename, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
	return handle;
}

//! Returns true if success
static bool close_file(file_handle_t handle) {
	return CloseHandle(handle);
}

static bool delete_file(const char* filename) {
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
static bool set_file_pointer(file_handle_t handle, size_t offset) {
	LARGE_INTEGER offs;
	offs.QuadPart = offset;
	return SetFilePointerEx(handle, offs, nullptr, FILE_BEGIN);
}

static size_t read_file(file_handle_t handle, void* buffer_ptr, size_t size) {
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

static size_t write_file(file_handle_t handle, const void* buffer_ptr, size_t size) {
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

static bool set_file_datetime(file_handle_t handle, uint32_t file_datetime)
{
	FILETIME LocTime, GlobTime;
	DosDateTimeToFileTime(static_cast<uint16_t>(file_datetime >> 16),
		static_cast<uint16_t>(file_datetime & static_cast<uint32_t>(0xFFFF)),
		&LocTime);
	LocalFileTimeToFileTime(&LocTime, &GlobTime);
	SetFileTime(handle, nullptr, nullptr, &GlobTime);
	return true; // TODO: Add error handling
}

//----------------Tools-------------------------
static uint32_t combine(uint16_t hi, uint16_t lo) {
	return (static_cast<uint32_t>(hi) << (sizeof(hi) * CHAR_BIT)) + lo;
}

template<size_t N>
class minimal_fixed_string_t {
	std::array<char, N> data_m = { '\0' };
	size_t size_m = 0;
public:
	minimal_fixed_string_t() = default;
	minimal_fixed_string_t(const minimal_fixed_string_t&) = default;
	minimal_fixed_string_t(const char* str) {
		push_back(str);
	}
	constexpr size_t size() const { return size_m; }
	constexpr size_t capacity() const { return data_m.size() - 1; }
	constexpr bool is_empty() const { return size_m == 0; }
	constexpr       char& operator[](size_t idx) { return data_m[idx]; }
	constexpr const char& operator[](size_t idx) const { return data_m[idx]; }
	constexpr const char* data() const { return data_m.data(); }
	constexpr       char* data() { return data_m.data(); }
	bool push_back(char c) {
		if (size() == capacity()) return false;
		data_m[size_m++] = c;
		data_m[size_m] = '\0';
		return true;
	}
	bool push_back(const char* str) {
		data_m[size_m] = '\0';
		auto res = !strcpy_s(data() + size(), capacity() - size(), str);
		size_m += strnlen_s(data() + size(), capacity() - size());
		return res;
	}
	template<size_t M>
	bool push_back(const minimal_fixed_string_t<M>& fixed_str) {
		data_m[size_m] = '\0';
		auto res = !strcpy_s(data() + size(), capacity() - size(), fixed_str.data());
		size_m += strnlen_s(data() + size(), capacity() - size());
		return res;
	}
};

//----------------FAT12 Definitions-------------

//! https://wiki.osdev.org/FAT#BPB_.28BIOS_Parameter_Block.29
//! FAT is little endian.
// BPB = BIOS Parameter Block
// BS = (Extended) Boot Record. Extended BR is also called EBPB 
#pragma pack(push, 1) // See also __attribute__((packed)) 
struct tFAT12BootSec
{
	uint8_t  BS_jmpBoot[3];
	uint8_t  BS_OEMName[8];
	uint16_t BPB_bytesPerSec;
	uint8_t  BPB_SecPerClus;
	uint16_t BPB_RsvdSecCnt;
	uint8_t  BPB_NumFATs;	  // Often 1 or 2
	uint16_t BPB_RootEntCnt;
	uint16_t BPB_TotSec16;    // 0 if more than 65535 -- then val in. BPB_TotSec32
	uint8_t  BPB_MediaDescr;
	uint16_t BPB_SectorsPerFAT; // FAT12/16 only
	uint16_t BPB_SecPerTrk;
	uint16_t BPB_NumHeads;
	uint32_t BPB_HiddSec;     // "Number of hidden sectors. (i.e. the LBA of the beginning of the partition.)"
	uint32_t BPB_TotSec32;
	// Extended Boot Record, FAT12/16 only 
	uint8_t  BS_DrvNum;	      // Ignore it
	uint8_t  BS_ReservedOrNT; // Ignore it
	uint8_t  BS_BootSig;	  // Signature (must be 0x28 or 0x29). TODO: check
	uint32_t BS_VolID;		  // Volume serial number
	uint8_t  BS_VolLab[11];   // Should be padded by spaces
	uint8_t  BS_FilSysType[8];// "The spec says never to trust the contents of this string for any use."
	uint8_t  remaining_part[448];
	uint16_t signature;       // 0xAA55 (Little endian: signature[0] == 0x55, signature[1] == 0xAA)
};
#pragma pack(pop)

static_assert(sizeof(tFAT12BootSec) == 512, "Wrong boot sector structure size");
static_assert(std::endian::native == std::endian::little, "Wrong endiannes");

#pragma pack(push, 1)
struct FAT_attrib_t {
	enum file_attr_t {
		ATTR_READONLY = 0x01,
		ATTR_HIDDEN = 0x02,
		ATTR_SYSTEM = 0x04,
		ATTR_VOLUME_ID = 0x08,
		ATTR_DIRECTORY = 0x10,
		ATTR_ARCHIVE = 0x20,
		ATTR_LONG_NAME = 0x0F,
	};
	uint8_t  attribute;

	bool is_volumeID() const { // TODO: rename
		return attribute == ATTR_VOLUME_ID;
	}

	bool is_longname_part() const {
		return attribute == ATTR_LONG_NAME;
	}

	bool is_dir() const {
		return attribute & ATTR_DIRECTORY;
	}

	bool is_invalid() const {
		bool res = ((attribute & 0xC8) != 0);
		res |= ((!is_volumeID()) && ((attribute & ATTR_VOLUME_ID) != 0));
		// Other checks here
		return res;
	}

	bool is_readonly() const {
		return attribute & ATTR_READONLY;
	}

	bool is_archive() const {
		return attribute & ATTR_ARCHIVE;
	}

	bool is_hidden() const {
		return attribute & ATTR_HIDDEN;
	}
	
	bool is_system() const {
		return attribute & ATTR_SYSTEM;
	}

	uint32_t get_user_attr() const {
		return attribute & (ATTR_READONLY | ATTR_ARCHIVE | ATTR_HIDDEN | ATTR_SYSTEM);
	}

	template<std::integral T>
	operator T() const {
		return attribute; 
	}
};
#pragma pack(pop)
static_assert(sizeof(FAT_attrib_t) == 1, "Wrong FAT_attrib_t size");


//! Platform-dependent IO function. 
//! Returns true if successful
bool set_file_attributes(const char* filename, FAT_attrib_t attribute) {
	/*
	DWORD winattr = FILE_ATTRIBUTE_NORMAL;
	if (attribute.is_readonly()) winattr |= FILE_ATTRIBUTE_READONLY;
	if (attribute.is_archive() ) winattr |= FILE_ATTRIBUTE_ARCHIVE;
	if (attribute.is_hidden()  ) winattr |= FILE_ATTRIBUTE_HIDDEN;
	if (attribute.is_system()  ) winattr |= FILE_ATTRIBUTE_SYSTEM;
	*/ 
	return SetFileAttributes(filename, attribute.get_user_attr()); // Codes are equal
}

#pragma pack(push, 1)
struct FATxx_dir_entry_t
{
	char  DIR_Name[11];
	FAT_attrib_t  DIR_Attr;
	uint8_t  DIR_NTRes; // "Reserved for use by Windows NT"
	uint8_t  DIR_CrtTimeTenth;
	uint16_t DIR_CrtTime;	// TODO: Use it too
	uint16_t DIR_CrtDate;	// TODO: Use it too
	uint16_t DIR_LstAccDate;
	uint16_t DIR_FstClusHI; // High bytes of first cluster on FAT32, obscure attribute of some OSes for FAT12/16 -- ignore
	uint16_t DIR_WrtTime;
	uint16_t DIR_WrtDate;
	uint16_t DIR_FstClusLO;
	uint32_t DIR_FileSize;

	bool is_dir_record_free() const {
		return (DIR_Name[0] == '\x00');
	}

	bool is_dir_record_deleted() const {
		return (DIR_Name[0] == '\xE5') || (DIR_Name[0] == '\x05');
	}

	bool is_dir_record_unknown() const {
		return !(is_dir_record_free()) &&
			!(is_dir_record_deleted()) &&
			!ValidChar(DIR_Name[0]);
	}

	bool is_dir_record_used() const {
		return !is_dir_record_unknown();
	}

	//! Convenience functions
	bool is_dir_record_volumeID() const {
		return DIR_Attr.is_volumeID();
	}

	bool is_dir_record_longname_part() const {
		return DIR_Attr.is_longname_part();
	}

	bool is_dir_record_dir() const {
		return DIR_Attr.is_dir();
	}

	bool is_dir_record_invalid_attr() const {
		return DIR_Attr.is_invalid();
	}

	bool is_dir_record_regular_file() const {
		return !is_dir_record_volumeID() &&
			!is_dir_record_longname_part() &&
			!is_dir_record_dir() &&
			!is_dir_record_invalid_attr();
	}

	//! Returns number of invalid chars
	template<typename T>
	uint32_t dir_entry_name_to_str(T& name) {
		// if (DIR_Name[0] == char(0x05)) FileName[i++] = char(0xE5); // because 0xE5 used in Japan
		uint32_t invalid = 0;

		for (int i = 0; i < 8; ++i) {
			if (!ValidChar(DIR_Name[i])) {
				if (DIR_Name[i] != ' ') ++invalid;
				break;
			}
			name.push_back(DIR_Name[i]);
		}
		if (ValidChar(DIR_Name[8]))
		{
			name.push_back('.');
		}
		for (int i = 8; i < 8 + 3; ++i) {
			if (!ValidChar(DIR_Name[i])) {
				if (DIR_Name[i] != ' ') ++invalid;
				break;
			}
			name.push_back(DIR_Name[i]);
		}
		return invalid;
	}

	uint32_t get_first_cluster_FAT12() const {
		return DIR_FstClusLO;
	}

	// For the future
	uint32_t get_first_cluster_FAT16() const {
		return DIR_FstClusLO;
	}
	uint32_t get_first_cluster_FAT32() const {
		return combine(DIR_FstClusHI, DIR_FstClusLO); // FAT32;
	}

	uint32_t get_file_datetime() const {
		return combine(DIR_WrtDate, DIR_WrtTime);
	}

	static constexpr char nonValidChars[] = R"("*+,./:;<=>?[\]|)";
	static int ValidChar(char mychar)
	{		
		return !((mychar >= '\x00') && (mychar <= '\x20')) &&
			strchr(nonValidChars, mychar) == nullptr;
	}
};
#pragma pack(pop)
static_assert(offsetof(FATxx_dir_entry_t, DIR_Attr) == 11, "Wrong FAT_attrib_t offset");
static_assert(sizeof(FATxx_dir_entry_t) == 32, "Wrong size of FATxx_dir_entry_t"); //-V112


//--------End of  FAT12 Definitions-------------

//----------------IMG Definitions-------------

struct arc_dir_entry_t
{
	minimal_fixed_string_t<MAX_PATH> PathName;
	size_t FileSize;
	uint32_t FileTime;
	FAT_attrib_t FileAttr;
	uint32_t FirstClus;
};

struct tArchive
{
	static constexpr size_t sector_size = 512;
	minimal_fixed_string_t<MAX_PATH> archname; // Should be saved for the TCmd API
	using on_bad_BPB_callback_t = int(*)(tArchive*, int openmode);
	on_bad_BPB_callback_t on_bad_BPB_callback = nullptr;
	int openmode_m = PK_OM_LIST;
	file_handle_t hArchFile = file_handle_t();    //opened file handle

	std::vector<uint8_t> fattable;
	std::vector<arc_dir_entry_t> arc_dir_entries;
	tFAT12BootSec bootsec{};

	size_t rootarea_off_m = 0; //number of uint8_t before root area 
	size_t dataarea_off_m = 0; //number of uint8_t before data area
	uint32_t cluster_size_m = 0;
	uint32_t counter = 0;

	tChangeVolProc   pLocChangeVol = nullptr;
	tProcessDataProc pLocProcessData = nullptr;

	tArchive(const tArchive&) = delete;
	tArchive& operator=(const tArchive&) = delete;

	tArchive(on_bad_BPB_callback_t clb, const char* arcnm, file_handle_t fh, int openmode) :
		archname(arcnm), on_bad_BPB_callback(clb), openmode_m(openmode), hArchFile(fh)
	{
	}

	~tArchive() {
		if (hArchFile)
			close_file(hArchFile);
	}

	size_t cluster_to_file_off(uint32_t cluster) {
		return get_data_area_offset() + static_cast<size_t>(cluster - 2) * get_cluster_size(); //-V104
	}

	uint32_t get_sectors_per_FAT() const {
		return bootsec.BPB_SectorsPerFAT;
	}

	uint32_t get_bytes_per_FAT() const {
		return get_sectors_per_FAT() * sector_size;
	}

	uint32_t get_root_dir_entry_count() const {
		return bootsec.BPB_RootEntCnt;
	}

	size_t get_root_dir_size() const {
		return get_data_area_offset() - get_root_area_offset(); //-V110
	}

	size_t get_root_area_offset() const {
		return rootarea_off_m;
	}

	size_t get_data_area_offset() const {
		return dataarea_off_m;
	}

	uint32_t get_cluster_size() const {
		return cluster_size_m;
	}

	int process_bootsector() {
		auto result = read_file(hArchFile, &bootsec, sector_size);
		if (result != sector_size) {
			return E_EREAD;
		}

		if (bootsec.BPB_bytesPerSec != sector_size) {
			return E_UNKNOWN_FORMAT;
		}

		if (bootsec.signature != 0xAA55) {
			int res = on_bad_BPB_callback(this, openmode_m);
			if (res != 0) {
				return res;
			}
		}

		if ((get_sectors_per_FAT() < 1) || (get_sectors_per_FAT() > 12)) // FAT12 Only
		{
			return E_UNKNOWN_FORMAT;
		}
		cluster_size_m = sector_size * bootsec.BPB_SecPerClus;
		rootarea_off_m = sector_size * (bootsec.BPB_RsvdSecCnt +
			get_sectors_per_FAT() * static_cast<size_t>(bootsec.BPB_NumFATs));
		dataarea_off_m = get_root_area_offset() + get_root_dir_entry_count() * sizeof(FATxx_dir_entry_t);
		return 0;
	}

	int load_FAT() {
		const size_t fat_size_bytes = get_bytes_per_FAT();
		try {
			fattable.reserve(fat_size_bytes); // To minimize overcommit
			fattable.resize(fat_size_bytes);
		}
		catch (std::exception&) { // std::length_error, std::bad_alloc, other can be used by custom allocators
			return E_NO_MEMORY;
		}
		// Read FAT table
		auto result = read_file(hArchFile, fattable.data(), fat_size_bytes);
		if (result != fat_size_bytes)
		{
			return E_EREAD;
		}
		return 0;
	}

	int extract_to_file(file_handle_t hUnpFile, uint32_t idx) {	
		try { // For bad allocation
			const auto& cur_entry = arc_dir_entries[idx];
			uint32_t nextclus = cur_entry.FirstClus;
			size_t remaining = cur_entry.FileSize;
			std::vector<char> buff(get_cluster_size());
			while (remaining > 0)
			{
				if ((nextclus <= 1) || (nextclus >= 0xFF0))
				{
					close_file(hUnpFile);
					return E_UNKNOWN_FORMAT;
				}
				set_file_pointer(hArchFile, cluster_to_file_off(nextclus));
				size_t towrite = std::min<size_t>(get_cluster_size(), remaining);
				size_t result = read_file(hArchFile, buff.data(), towrite);
				if (result != towrite)
				{
					close_file(hUnpFile);
					return E_EREAD;
				}
				result = write_file(hUnpFile, buff.data(), towrite);
				if (result != towrite)
				{
					close_file(hUnpFile);
					return E_EWRITE;
				}
				if (remaining > get_cluster_size()) { remaining -= get_cluster_size(); } //-V104 //-V101
				else { remaining = 0; }

				nextclus = next_cluster_FAT12(nextclus);
			}
			return 0;
		}
		catch (std::bad_alloc&) {
			return E_NO_MEMORY;
		}
	}

	// root passed by copy to avoid problems while relocating vector
	int load_file_list_recursively(minimal_fixed_string_t<MAX_PATH> root, uint32_t firstclus, uint32_t depth) //-V813
	{
		if (root.is_empty()) { // Initial reading
			counter = 0;
			arc_dir_entries.clear();
		}
		size_t portion_size = 0;
		if (firstclus == 0)
		{   // Read whole FAT12/16 dir at once
			set_file_pointer(hArchFile, get_root_area_offset());
			portion_size = get_root_dir_size();
		}
		else {
			set_file_pointer(hArchFile, cluster_to_file_off(firstclus));
			portion_size = static_cast<size_t>(get_cluster_size());
		}
		size_t records_number = portion_size / sizeof(FATxx_dir_entry_t);
		std::unique_ptr<FATxx_dir_entry_t[]> sector;
		try {
			sector = std::make_unique<FATxx_dir_entry_t[]>(records_number);
		}
		catch (std::bad_alloc&) {
			return E_NO_MEMORY;
		}

		if ((firstclus == 1) || (firstclus >= 0xFF0)) { 
			return E_UNKNOWN_FORMAT; 
		}
		size_t result = read_file(hArchFile, sector.get(), portion_size);
		if (result != portion_size) { 
			return E_EREAD; 
		}

		do {
			size_t entry_in_cluster = 0;
			while ((entry_in_cluster < records_number) && (!sector[entry_in_cluster].is_dir_record_free()))
			{
				if (sector[entry_in_cluster].is_dir_record_deleted() ||
					sector[entry_in_cluster].is_dir_record_unknown() ||
					sector[entry_in_cluster].is_dir_record_volumeID() ||
					sector[entry_in_cluster].is_dir_record_longname_part() ||
					sector[entry_in_cluster].is_dir_record_invalid_attr()
					)
				{
					entry_in_cluster++;
					continue;
				}
				arc_dir_entries.emplace_back();
				auto& newentryref = arc_dir_entries.back();
				newentryref.FileAttr = sector[entry_in_cluster].DIR_Attr;
				// TODO: errors handling
				newentryref.PathName.push_back(root); // OK for empty root
				newentryref.PathName.push_back('\\');
				auto invalid_chars = sector[entry_in_cluster].dir_entry_name_to_str(newentryref.PathName);
				newentryref.FileTime = sector[entry_in_cluster].get_file_datetime();
				newentryref.FileSize = sector[entry_in_cluster].DIR_FileSize;
				newentryref.FirstClus = sector[entry_in_cluster].get_first_cluster_FAT12();

				if (sector[entry_in_cluster].is_dir_record_dir() &&
					(newentryref.FirstClus < 0xFF0) && (newentryref.FirstClus > 0x1)
					&& (depth <= 100))
				{
					load_file_list_recursively(newentryref.PathName, newentryref.FirstClus, depth + 1);
				}
				++entry_in_cluster;
			}
			if (entry_in_cluster < records_number) { return 0; }
			if (firstclus == 0)
			{	
				break; // We already processed FAT12/16 root dir
			}
			else {
				firstclus = next_cluster_FAT12(firstclus);
				if ((firstclus <= 1) || (firstclus >= 0xFF0)) { return 0; }
				set_file_pointer(hArchFile, cluster_to_file_off(firstclus)); //-V104
			}
			result = read_file(hArchFile, sector.get(), portion_size);
			if (result != portion_size) { 
				return E_EREAD; 
			}
		} while (true);

		if (root.is_empty()) { // Initial finished
			arc_dir_entries.shrink_to_fit();
		}
		return 0;
	}

	uint32_t next_cluster_FAT12(uint32_t firstclus)
	{
		const auto FAT_byte_pre = fattable.data() + ((firstclus * 3) >> 1); // firstclus + firstclus/2 //-V104
		//! Extract word, containing next cluster:
		const uint16_t* word_ptr = reinterpret_cast<const uint16_t*>(FAT_byte_pre);
		// Extract correct 12 bits -- lower for odd, upper for even: 
		return ((*word_ptr) >> ((firstclus % 2) ? 4 : 0)) & 0x0FFF; //-V112
	}	
};

int winAPI_msgbox_on_bad_BPB(tArchive*, int openmode) {
	if (openmode == PK_OM_LIST) {
		//! Is it correct to create own dialogs in plugin?
		int msgboxID = MessageBoxEx(
			NULL,
			TEXT("Wrong BPB signature\nContinue?"),
			TEXT("BPB Signature error"),
			MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1,
			0
		);
		if (msgboxID == IDCANCEL) {
			return E_UNKNOWN_FORMAT;
		}
		else {
			return 0;
		}
	}
	else {
		return 0;
	}
}
using archive_HANDLE = tArchive*;

//--------End of  IMG Definitions-------------

//-----------------------=[ DLL exports ]=--------------------

extern "C" {
	// OpenArchive should perform all necessary operations when an archive is to be opened
	DLLEXPORT archive_HANDLE STDCALL OpenArchive(tOpenArchiveData* ArchiveData)
	{
		std::unique_ptr<tArchive> arch; // TCmd API expects HANDLE/raw pointer,
										// so smart pointer is used to manage cleanup on errors 
										// only inside this function
		//! Not used by TCmd yet.
		ArchiveData->CmtBuf = 0;
		ArchiveData->CmtBufSize = 0;
		ArchiveData->CmtSize = 0;
		ArchiveData->CmtState = 0;

		auto hArchFile = open_file_shared_read(ArchiveData->ArcName);
		if (hArchFile == file_open_error_v)
		{
			ArchiveData->OpenResult = E_EOPEN;
			return nullptr;
		}
		try {
			arch = std::make_unique<tArchive>(winAPI_msgbox_on_bad_BPB, ArchiveData->ArcName,
				hArchFile, ArchiveData->OpenMode);
		}
		catch (std::bad_alloc&) {
			ArchiveData->OpenResult = E_NO_MEMORY;
			return nullptr;
		}

		auto err_code = arch->process_bootsector();
		if (err_code != 0) {
			ArchiveData->OpenResult = err_code;
			return nullptr;
		}

		err_code = arch->load_FAT();
		if (err_code != 0) {
			ArchiveData->OpenResult = err_code;
			return nullptr;
		}

		err_code = arch->load_file_list_recursively(minimal_fixed_string_t<MAX_PATH>{}, 0, 0);
		if (err_code != 0) {
			ArchiveData->OpenResult = err_code;
			return nullptr;
		}

		ArchiveData->OpenResult = 0; // OK
		return arch.release(); // Returns raw ptr and releases ownership 
	}

	// TCmd calls ReadHeader to find out what files are in the archive
	DLLEXPORT int STDCALL ReadHeader(archive_HANDLE hArcData, tHeaderData* HeaderData)
	{
		if (hArcData->counter == hArcData->arc_dir_entries.size()) { //-V104
			hArcData->counter = 0;
			return E_END_ARCHIVE;
		}

		strcpy(HeaderData->ArcName, hArcData->archname.data());
		strcpy(HeaderData->FileName, hArcData->arc_dir_entries[hArcData->counter].PathName.data());
		HeaderData->FileAttr = hArcData->arc_dir_entries[hArcData->counter].FileAttr;
		HeaderData->FileTime = hArcData->arc_dir_entries[hArcData->counter].FileTime;
		// For files larger than 2Gb -- implement tHeaderDataEx
		HeaderData->PackSize = static_cast<int>(hArcData->arc_dir_entries[hArcData->counter].FileSize);
		HeaderData->UnpSize = HeaderData->PackSize;
		HeaderData->CmtBuf = 0;
		HeaderData->CmtBufSize = 0;
		HeaderData->CmtSize = 0;
		HeaderData->CmtState = 0;
		HeaderData->UnpVer = 0;
		HeaderData->Method = 0;
		HeaderData->FileCRC = 0;
		hArcData->counter++;
		return 0; // OK
	}

	// ProcessFile should unpack the specified file or test the integrity of the archive
	DLLEXPORT int STDCALL ProcessFile(archive_HANDLE hArcData, int Operation, char* DestPath, char* DestName) //-V2009
	{
		tArchive* arch = hArcData;
		char dest[MAX_PATH] = "";
		file_handle_t hUnpFile;

		if (Operation == PK_SKIP) return 0;

		if (arch->counter == 0)
			return E_END_ARCHIVE;
		// if (newentry->FileAttr & ATTR_DIRECTORY) return 0;

		if (Operation == PK_TEST) {
			auto res = get_temp_filename(dest, "FIM");
			if (!res) {
				return E_ECREATE;
			}
		}
		else {
			if (DestPath) strcpy(dest, DestPath);
			if (DestName) strcat(dest, DestName);
		}

		if (Operation == PK_TEST) {
			hUnpFile = open_file_overwrite(dest);
		}
		else {
			hUnpFile = open_file_write(dest);
		}
		if (hUnpFile == file_open_error_v)
			return E_ECREATE;

		auto res = arch->extract_to_file(hUnpFile, arch->counter - 1);
		if (res != 0) {
			return res;
		}
		const auto& cur_entry = arch->arc_dir_entries[arch->counter - 1];
		set_file_datetime(hUnpFile, cur_entry.FileTime);
		close_file(hUnpFile);
		set_file_attributes(dest, cur_entry.FileAttr);

		if (Operation == PK_TEST) {
			delete_file(dest);
		}

		return 0;
	}

	// CloseArchive should perform all necessary operations when an archive is about to be closed
	DLLEXPORT int STDCALL CloseArchive(archive_HANDLE hArcData)
	{
		delete hArcData;
		return 0; // OK
	}

	// This function allows you to notify user about changing a volume when packing files
	DLLEXPORT void STDCALL SetChangeVolProc(archive_HANDLE hArcData, tChangeVolProc pChangeVolProc)
	{
		hArcData->pLocChangeVol = pChangeVolProc;
	}

	// This function allows you to notify user about the progress when you un/pack files
	DLLEXPORT void STDCALL SetProcessDataProc(archive_HANDLE hArcData, tProcessDataProc pProcessDataProc)
	{
		hArcData->pLocProcessData = pProcessDataProc;
	}
}
