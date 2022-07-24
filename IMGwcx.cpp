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
#include <windows.h>

#include "wcxhead.h"
#include <new>
#include <memory>
#include <cstddef>
#include <vector>
#include <array>
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
	constexpr bool empty() const { return size_m == 0; }
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

constexpr size_t sector_size = 512;

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
	char archname[MAX_PATH]{ '\0' }; // All set to 0
	file_handle_t hArchFile = file_handle_t();    //opened file handle

	std::vector<uint8_t> fattable;
	std::vector<arc_dir_entry_t> arc_dir_entries;
	tFAT12BootSec bootsec{};

	size_t rootarea_off = 0; //number of uint8_t before root area 
	size_t dataarea_off = 0; //number of uint8_t before data area
	uint32_t rootentcnt = 0;
	uint32_t sectors_in_FAT = 0; 
	uint32_t cluster_size = 0; 
	uint32_t counter = 0;

	tChangeVolProc   pLocChangeVol   = nullptr;
	tProcessDataProc pLocProcessData = nullptr;

	~tArchive() {
		if (hArchFile)
			close_file(hArchFile);
	}
};

using myHANDLE = tArchive*;

//--------End of  IMG Definitions-------------

//------------------=[ "Kernel" ]=-------------

size_t next_cluster_FAT12(size_t firstclus, const tArchive* arch)
{
	const auto FAT_byte_pre = arch->fattable.data() + ((firstclus * 3) >> 1); // firstclus + firstclus/2
	//! Extract word, containing next cluster:
	const uint16_t* word_ptr = reinterpret_cast<const uint16_t*>(FAT_byte_pre);
	// Extract correct 12 bits -- lower for odd, upper for even: 
	return ( (*word_ptr) >> (( firstclus % 2) ? 4 : 0) ) & 0x0FFF; //-V112
}

// root -- by copy to avoid problems while relocating vector
int CreateFileList(minimal_fixed_string_t<MAX_PATH> root, size_t firstclus, tArchive* arch, DWORD depth) //-V813
{		
	size_t portion_size = 0;
	if (firstclus == 0)
	{   // Read whole FAT12/16 dir at once
		set_file_pointer(arch->hArchFile, arch->rootarea_off);
		portion_size = arch->dataarea_off - arch->rootarea_off; // Size of root dir
	}
	else {
		set_file_pointer(arch->hArchFile, arch->dataarea_off + (firstclus - 2) * arch->cluster_size); //-V104
		portion_size = arch->cluster_size;  //-V101
	}	
	size_t records_number = portion_size / sizeof(FATxx_dir_entry_t);
	std::unique_ptr<FATxx_dir_entry_t[]> sector = std::make_unique<FATxx_dir_entry_t[]>(records_number);

	if (sector == nullptr) { return 0; }
	if ((firstclus == 1) || (firstclus >= 0xFF0)) { return 0; }
	size_t result = read_file(arch->hArchFile, sector.get(), portion_size);
	if (result != portion_size) { return 0; }

	do {
		size_t entry_in_cluster = 0;
		while ((entry_in_cluster < records_number) && (!sector[entry_in_cluster].is_dir_record_free()))
		{
			if( sector[entry_in_cluster].is_dir_record_deleted() ||
				sector[entry_in_cluster].is_dir_record_unknown() ||
				sector[entry_in_cluster].is_dir_record_volumeID() ||
				sector[entry_in_cluster].is_dir_record_longname_part() ||
				sector[entry_in_cluster].is_dir_record_invalid_attr()
				)
			{
				entry_in_cluster++; 
				continue;
			}
			arch->arc_dir_entries.emplace_back();
			auto& newentryref = arch->arc_dir_entries.back();
			newentryref.FileAttr = sector[entry_in_cluster].DIR_Attr;
			// TODO: errors handling
			newentryref.PathName.push_back(root); // OK for empty root
			newentryref.PathName.push_back('\\');
			auto invalid_chars = sector[entry_in_cluster].dir_entry_name_to_str(newentryref.PathName);
			newentryref.FileTime = combine(sector[entry_in_cluster].DIR_WrtDate, sector[entry_in_cluster].DIR_WrtTime);
			newentryref.FileSize = sector[entry_in_cluster].DIR_FileSize;
			newentryref.FirstClus = sector[entry_in_cluster].DIR_FstClusLO;
			//newentryref.FirstClus = combine(sector[entry_in_cluster].DIR_FstClusHI, sector[entry_in_cluster].DIR_FstClusLO); // FAT32

			if ( sector[entry_in_cluster].is_dir_record_dir() &&
				(newentryref.FirstClus < 0xFF0) && (newentryref.FirstClus > 0x1)
				&& (depth <= 100))
			{
				CreateFileList(newentryref.PathName, newentryref.FirstClus, arch, depth + 1);
			}
			entry_in_cluster++;
		}
		if (entry_in_cluster < records_number) { return 0; }
		if (firstclus == 0)
		{
			break;
		}
		else {
			firstclus = next_cluster_FAT12(firstclus, arch);
			if ((firstclus <= 1) || (firstclus >= 0xFF0)) { return 0; }
			set_file_pointer(arch->hArchFile, arch->dataarea_off + static_cast<size_t>(firstclus - 2) * arch->cluster_size); //-V104
		}
		result = read_file(arch->hArchFile, sector.get(), portion_size);
		if (result != portion_size) { return 0; }
	} while (true);

	return 0;
}

myHANDLE IMG_Open(tOpenArchiveData* ArchiveData)
{
	std::unique_ptr<tArchive> arch; // Looks like TCmd API expects HANDLE/raw pointer,
									// so smart pointer is used to manage cleanup on errors 
									// only inside this function
	size_t result;

	//! Not used by TCmd yet.
	ArchiveData->CmtBuf = 0;
	ArchiveData->CmtBufSize = 0;
	ArchiveData->CmtSize = 0;
	ArchiveData->CmtState = 0;

	ArchiveData->OpenResult = E_NO_MEMORY;// default error type
	try {
		arch = std::make_unique<tArchive>();
	}
	catch (std::bad_alloc&) {
		return nullptr;
	}

	// trying to open
	auto errcode = strcpy_s(arch->archname, sizeof(arch->archname), ArchiveData->ArcName);
	if (errcode != 0) {
		return nullptr;
	}
	
	arch->hArchFile = open_file_shared_read(arch->archname);
	if (arch->hArchFile == file_open_error_v)
	{
		return nullptr;
	}

	//----------begin of bootsec in use
	result = read_file(arch->hArchFile, &(arch->bootsec), sector_size);
	if (result != sector_size)
	{
		ArchiveData->OpenResult = E_EREAD;
		return nullptr;
	}

	if (arch->bootsec.BPB_bytesPerSec != sector_size)
	{
		ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
		return nullptr;
	}

	if ( arch->bootsec.signature != 0xAA55 && ArchiveData->OpenMode == PK_OM_LIST)
	{
		//! Dialog only while listing
		//! Is it correct to create own dialogs in plugin?
		int msgboxID = MessageBoxEx(
			NULL,
			TEXT("Wrong BPB signature\nContinue?"),
			TEXT("BPB Signature error"),
			MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1, 
			0
		);
		if (msgboxID == IDCANCEL) {
			ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
			return nullptr;
		}
	}

	arch->sectors_in_FAT = arch->bootsec.BPB_SectorsPerFAT;
	if ((arch->sectors_in_FAT < 1) || (arch->sectors_in_FAT > 12))
	{
		ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
		return nullptr;
	}
	arch->rootentcnt = arch->bootsec.BPB_RootEntCnt;
	arch->cluster_size = 512 * arch->bootsec.BPB_SecPerClus;
	arch->rootarea_off = sector_size * (arch->bootsec.BPB_RsvdSecCnt +
		        arch->sectors_in_FAT * static_cast<size_t>(arch->bootsec.BPB_NumFATs));
	arch->dataarea_off = arch->rootarea_off + arch->rootentcnt * sizeof(FATxx_dir_entry_t); 
	const size_t fat_size_bytes = arch->sectors_in_FAT * sector_size;
	try {
		arch->fattable.reserve(fat_size_bytes); // To minimize overcommit
		arch->fattable.resize(fat_size_bytes);
	}
	catch (std::exception&) { // std::length_error, std::bad_alloc, other can be used by custom allocators
		ArchiveData->OpenResult = E_NO_MEMORY;
		return nullptr;
	}
	// Read FAT table
	result = read_file(arch->hArchFile, arch->fattable.data(), arch->sectors_in_FAT * sector_size);
	if (result != arch->sectors_in_FAT * sector_size)
	{
		ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
		return nullptr;
	}

	// Read root directory
	arch->counter = 0;
	arch->arc_dir_entries.clear();
	CreateFileList(minimal_fixed_string_t<MAX_PATH>{}, 0, arch.get(), 0);
	arch->arc_dir_entries.shrink_to_fit();

	ArchiveData->OpenResult = 0;// ok
	return arch.release(); // Returns raw ptr and releases ownership 
};

int IMG_NextItem(myHANDLE arch, tHeaderData* HeaderData)
{
	if (arch->counter == arch->arc_dir_entries.size()) { //-V104
		arch->counter = 0;
		return E_END_ARCHIVE;
	}

	strcpy(HeaderData->ArcName, arch->archname);
	strcpy(HeaderData->FileName, arch->arc_dir_entries[arch->counter].PathName.data());
	HeaderData->FileAttr = arch->arc_dir_entries[arch->counter].FileAttr;
	HeaderData->FileTime = arch->arc_dir_entries[arch->counter].FileTime;
	// For files larger than 2Gb -- implement tHeaderDataEx
	HeaderData->PackSize = static_cast<int>(arch->arc_dir_entries[arch->counter].FileSize); 
	HeaderData->UnpSize = HeaderData->PackSize;
	HeaderData->CmtBuf = 0;
	HeaderData->CmtBufSize = 0;
	HeaderData->CmtSize = 0;
	HeaderData->CmtState = 0;
	HeaderData->UnpVer = 0;
	HeaderData->Method = 0;
	HeaderData->FileCRC = 0;
	arch->counter++;
	return 0;//ok
};

int IMG_Process(myHANDLE hArcData, int Operation, const char* DestPath, const char* DestName)
{
	tArchive* arch = hArcData;
	char dest[MAX_PATH] = "";
	HANDLE hUnpFile;
	size_t nextclus;
	size_t remaining;
	size_t towrite;
	FILETIME LocTime, GlobTime;

	if (Operation == PK_SKIP ) return 0;

	if( arch->counter == 0 ) 
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

	std::unique_ptr<char[]> buff;
	try {
		buff = std::make_unique<char[]>( static_cast<size_t>(arch->cluster_size));
	}
	catch (std::bad_alloc& ) {
		return E_NO_MEMORY;
	}

	if (Operation == PK_TEST) {
		hUnpFile = open_file_overwrite(dest);
	}
	else {
		hUnpFile = open_file_write(dest);
	}
	if (hUnpFile == file_open_error_v) 
		return E_ECREATE;

	const auto& cur_entry = arch->arc_dir_entries[arch->counter-1];
	nextclus = cur_entry.FirstClus; //-V101
	remaining = cur_entry.FileSize;
	while (remaining > 0)
	{
		if ((nextclus <= 1) || (nextclus >= 0xFF0))
		{
			close_file(hUnpFile);
			return E_UNKNOWN_FORMAT;
		}
		set_file_pointer(arch->hArchFile, arch->dataarea_off + static_cast<size_t>(nextclus - 2) * arch->cluster_size); //-V104
		towrite = (remaining > arch->cluster_size) ? (arch->cluster_size) : (remaining); //-V101 //-V105 //-V104
		size_t result = read_file(arch->hArchFile, buff.get(), towrite);
		if (result != towrite)
		{
			close_file(hUnpFile);
			return E_EREAD;
		}
		result = write_file(hUnpFile, buff.get(), towrite);
		if (result != towrite)
		{
			close_file(hUnpFile);
			return E_EWRITE;
		}
		if (remaining > arch->cluster_size) { remaining -= arch->cluster_size; } //-V104 //-V101
		else { remaining = 0; }

		nextclus = next_cluster_FAT12(nextclus, arch);
	}

	// set file time
	DosDateTimeToFileTime(static_cast<uint16_t>(static_cast<uint32_t>(cur_entry.FileTime) >> 16),
		static_cast<uint16_t>(static_cast<uint32_t>(cur_entry.FileTime) & static_cast<uint32_t>(0xFFFF)),
		&LocTime);
	LocalFileTimeToFileTime(&LocTime, &GlobTime);
	SetFileTime(hUnpFile, nullptr, nullptr, &GlobTime);

	close_file(hUnpFile);

	if (Operation == PK_TEST) {
		delete_file(dest);
	}

	set_file_attributes(dest, cur_entry.FileAttr);
	return 0; 
};

int IMG_Close(myHANDLE hArcData)
{
	delete hArcData;
	return 0;// ok
};

void IMG_SetCallBackVol(myHANDLE hArcData, tChangeVolProc pChangeVolProc)
{
	hArcData->pLocChangeVol = pChangeVolProc;
};

void IMG_SetCallBackProc(myHANDLE hArcData, tProcessDataProc pProcessDataProc)
{
	hArcData->pLocProcessData = pProcessDataProc;
};


//-----------------------=[ DLL exports ]=--------------------

extern "C" {
	// OpenArchive should perform all necessary operations when an archive is to be opened
	DLLEXPORT myHANDLE STDCALL OpenArchive(tOpenArchiveData* ArchiveData)
	{
		return IMG_Open(ArchiveData);
	}

	// TCmd calls ReadHeader to find out what files are in the archive
	DLLEXPORT int STDCALL ReadHeader(myHANDLE hArcData, tHeaderData* HeaderData)
	{
		return IMG_NextItem(hArcData, HeaderData);
	}

	// ProcessFile should unpack the specified file or test the integrity of the archive
	DLLEXPORT int STDCALL ProcessFile(myHANDLE hArcData, int Operation, char* DestPath, char* DestName) //-V2009
	{
		return IMG_Process(hArcData, Operation, DestPath, DestName);
	}

	// CloseArchive should perform all necessary operations when an archive is about to be closed
	DLLEXPORT int STDCALL CloseArchive(myHANDLE hArcData)
	{
		return IMG_Close(hArcData);
	}

	// This function allows you to notify user about changing a volume when packing files
	DLLEXPORT void STDCALL SetChangeVolProc(myHANDLE hArcData, tChangeVolProc pChangeVolProc)
	{
		IMG_SetCallBackVol(hArcData, pChangeVolProc);
	}

	// This function allows you to notify user about the progress when you un/pack files
	DLLEXPORT void STDCALL SetProcessDataProc(myHANDLE hArcData, tProcessDataProc pProcessDataProc)
	{
		IMG_SetCallBackProc(hArcData, pProcessDataProc);
	}
}