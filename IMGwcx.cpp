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

#pragma pvs(disable: 2005)
// 2005 -- C casts

#include "stdafx.h"
#include "wcxhead.h"
#include <new>
#include <memory>
#include <cstddef>
#include <vector>
using std::nothrow, std::uint8_t;

// The DLL entry point
BOOL APIENTRY DllMain(HANDLE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
) {
	return TRUE;
}

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
	uint16_t BPB_RsvdSecCnt;  // TODO: use this value too, 1 or more -- boot is included
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
struct tFAT12Table
{
	uint8_t data[12 * sector_size];
} ;
#pragma pack(pop)

#pragma pack(push, 1)
struct FATxx_dir_entry_t
{
	char  DIR_Name[11];
	uint8_t  DIR_Attr;
	uint8_t  DIR_NTRes; // "Reserved for use by Windows NT"
	uint8_t  DIR_CrtTimeTenth;
	uint16_t DIR_CrtTime;	// TODO: Use it too
	uint16_t DIR_CrtDate;	// TODO: Use it too
	uint16_t DIR_LstAccDate;
	uint16_t DIR_FstClusHI;
	uint16_t DIR_WrtTime;
	uint16_t DIR_WrtDate;
	uint16_t DIR_FstClusLO;
	uint32_t DIR_FileSize;
};
#pragma pack(pop)

static_assert(sizeof(FATxx_dir_entry_t) == 32, "Wrong size of FATxx_dir_entry_t"); //-V112

enum file_attr_t{ 
	ATTR_READONLY  = 0x01,
	ATTR_HIDDEN    = 0x02,
	ATTR_SYSTEM    = 0x04,
	ATTR_VOLUME_ID = 0x08,
	ATTR_DIRECTORY = 0x10,
	ATTR_ARCHIVE   = 0x20,
	ATTR_LONG_NAME = 0x0F,
};

//--------End of  FAT12 Definitions-------------

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

//! Returns true if success
static bool close_file(file_handle_t handle) {
	return CloseHandle(handle);
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

static uint32_t combine(uint16_t hi, uint16_t lo) {
	return (static_cast<uint32_t>(hi) << sizeof(hi) * CHAR_BIT) + hi; //   CHAR_BIT == 8
}
//----------------IMG Definitions-------------

struct tDirEntry
{
	char FileName[260];
	char PathName[260];
	unsigned FileSize;
	unsigned FileTime;
	unsigned FileAttr;
	uint32_t FirstClus;
	tDirEntry* next;
	tDirEntry* prev;
};

struct tArchive
{
	char archname[MAX_PATH]{ '\0' }; // All set to 0
	file_handle_t hArchFile = file_handle_t();    //opened file handle

	std::vector<uint8_t> fattable;
	tDirEntry* entrylist = nullptr;
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

#define AT_OK      0
#define AT_VOL     1
#define AT_DIR     2
#define AT_LONG    3
#define AT_INVALID 4

DWORD DIR_AttrToFileAttr(uint8_t DIR_Attr, unsigned* FileAttr)
{
	(*FileAttr) = (unsigned)DIR_Attr;
	if (DIR_Attr == ATTR_VOLUME_ID) return AT_VOL;
	if (DIR_Attr == ATTR_LONG_NAME) return AT_LONG;
	if ((DIR_Attr & 0xC8) != 0) return AT_INVALID;
	if (DIR_Attr & ATTR_DIRECTORY) return AT_DIR;
	return AT_OK;
}

int ValidChar(char mychar)
{
	static constexpr char nonValid[] = R"("*+,./:;<=>?[\]|)";

	return !((mychar >= '\x00') && (mychar <= '\x20')) &&  
		strchr(nonValid, mychar) == nullptr;
}

#define DN_OK      0
#define DN_DELETED 1
#define DN_FREE    2 // indicates that all the others are free too
#define DN_UNKNOWN 3

DWORD DIR_NameToFileName(const char* DIR_Name, char* FileName)
{
	int i = 0, j = 0;

	if (DIR_Name[0] == char(0x00)) return DN_FREE;
	if (DIR_Name[0] == char(0xE5)) return DN_DELETED;
	if (DIR_Name[0] == char(0x05)) // because 0xE5 used in Japan
	{
		FileName[i++] = char(0xE5);
	}
	while ((i < 8) && ValidChar(DIR_Name[i]))
	{
		FileName[i] = DIR_Name[i];
		i++;
	}
	if (ValidChar(DIR_Name[8]))
	{
		FileName[i++] = '.';
		while ((j < 3) && ValidChar(DIR_Name[j + 8]))
		{
			FileName[i++] = DIR_Name[j++ + 8];
		}
	}
	FileName[i] = 0;
	if (i == 0) return DN_UNKNOWN;
	return DN_OK;
}

size_t next_cluster_FAT12(size_t firstclus, const tArchive* arch)
{
	const auto FAT_byte_pre = arch->fattable.data() + ((firstclus * 3) >> 1); // firstclus + firstclus/2
	//! Extract word, containing next cluster:
	const uint16_t* word_ptr = reinterpret_cast<const uint16_t*>(FAT_byte_pre);
	// Extract correct 12 bits -- lower for odd, upper for even: 
	return ( (*word_ptr) >> (( firstclus % 2) ? 4 : 0) ) & 0x0FFF; //-V112
}

int CreateFileList(const char* root, size_t firstclus, tArchive* arch, DWORD depth)
{
	tDirEntry* newentry;
	FATxx_dir_entry_t* sector = nullptr;
	DWORD i, j;
	size_t result;
	size_t portion_size = 0;

	if (firstclus == 0)
	{
		set_file_pointer(arch->hArchFile, arch->rootarea_off);
		portion_size = 512;
	}
	else {
		set_file_pointer(arch->hArchFile, arch->dataarea_off + (firstclus - 2) * sector_size); //-V104
		portion_size = arch->cluster_size; //-V101
	}	
	size_t records_number = portion_size / sizeof(FATxx_dir_entry_t);
	sector = new(nothrow) FATxx_dir_entry_t[records_number];  //-V121
	if (sector == nullptr) goto error;
	if ((firstclus == 1) || (firstclus >= 0xFF0)) goto error;
	result = read_file(arch->hArchFile, sector, portion_size);
	if (result != portion_size) goto error;

	i = 1;
	do {
		j = 0;
		while ((j < records_number) && (sector[j].DIR_Name[0] != char(0x00))) //-V112
		{
			if ((newentry = new(nothrow) tDirEntry) == NULL) goto error;
			switch (DIR_AttrToFileAttr(sector[j].DIR_Attr, &newentry->FileAttr))
			{
			case AT_LONG:
			case AT_VOL:
			case AT_INVALID: {delete newentry; j++; continue; };
			}
			switch (DIR_NameToFileName(sector[j].DIR_Name, newentry->FileName))
			{
			case DN_UNKNOWN:
			case DN_DELETED: {delete newentry; j++; continue; };
			}
			strcpy(newentry->PathName, "");
			if (strcmp(root, "") != 0)
			{
				strcpy(newentry->PathName, root);
				strcat(newentry->PathName, "\\");
			}
			strcat(newentry->PathName, newentry->FileName);
			newentry->FileTime = combine(sector[j].DIR_WrtDate, sector[j].DIR_WrtTime);
			newentry->FileSize = sector[j].DIR_FileSize;
			newentry->FirstClus = combine(sector[j].DIR_FstClusHI, sector[j].DIR_FstClusLO);
			newentry->next = nullptr;
			newentry->prev = arch->entrylist;
			if (arch->entrylist != NULL) arch->entrylist->next = newentry;
			arch->entrylist = newentry;

			if ((newentry->FileAttr & ATTR_DIRECTORY) &&
				(newentry->FirstClus < 0xFF0) && (newentry->FirstClus > 0x1)
				&& (depth <= 100))
			{
				CreateFileList(newentry->PathName, newentry->FirstClus, arch, depth + 1);
			}
			j++;
		}
		if (j < records_number) goto error; 
		if ((firstclus == 0) && ((i * records_number) >= arch->rootentcnt)) goto error; //-V104
		if (firstclus == 0)
		{
			set_file_pointer(arch->hArchFile, arch->rootarea_off + i * sector_size); //-V104
		}
		else {
			firstclus = next_cluster_FAT12(firstclus, arch);
			if ((firstclus <= 1) || (firstclus >= 0xFF0)) goto error;
			set_file_pointer(arch->hArchFile, arch->dataarea_off + static_cast<size_t>(firstclus - 2) * portion_size); 
		}
		result = read_file(arch->hArchFile, sector, portion_size);
		if (result != portion_size) goto error;
		i++;
	} while (true);
error:
	delete[] sector;
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
	//224 is the maximum in a 1.44 floppy
	// But 2.88 disk exists
	//! TODO: Warn for too large or too small numbers
	/*
	if (arch->rootentcnt > 0xE0) 
	{
		ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
		goto error;
	}*/
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
	// trying to read fat table
	result = read_file(arch->hArchFile, arch->fattable.data(), arch->sectors_in_FAT * sector_size);
	if (result != arch->sectors_in_FAT * sector_size)
	{
		ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
		return nullptr;
	}

	// trying to read root directory
	arch->counter = 0;
	arch->entrylist = nullptr;
	CreateFileList("", 0, arch.get(), 0);
	if (arch->entrylist != nullptr)
	{
		while (arch->entrylist->prev != nullptr)
		{
			arch->entrylist = arch->entrylist->prev;
		}
	}

	ArchiveData->OpenResult = 0;// ok
	return arch.release(); // Returns raw ptr and releases ownership 
};

int IMG_NextItem(myHANDLE hArcData, tHeaderData* HeaderData)
{
	tArchive* arch = (tArchive*)(hArcData);
	tDirEntry* newentry;
	DWORD i = 0;

	newentry = arch->entrylist;
	while ((i < arch->counter) && (newentry != nullptr))
	{
		newentry = newentry->next;
		i++;
	}
	if (newentry == nullptr)
	{
		arch->counter = 0;
		return E_END_ARCHIVE;
	}

	strcpy(HeaderData->ArcName, arch->archname);
	strcpy(HeaderData->FileName, newentry->PathName);
	HeaderData->FileAttr = newentry->FileAttr;
	HeaderData->FileTime = newentry->FileTime;
	HeaderData->PackSize = newentry->FileSize;
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
	tDirEntry* newentry;
	DWORD i = 0;
	char dest[260] = "";
	HANDLE hUnpFile;
	DWORD nextclus;
	DWORD remaining;
	size_t towrite;
	DWORD attributes;
	FILETIME LocTime, GlobTime;

	if (Operation == PK_SKIP || Operation == PK_TEST) return 0;

	newentry = arch->entrylist;
	while ((i < arch->counter - 1) && (newentry != nullptr))
	{
		newentry = newentry->next;
		i++;
	}
	if (newentry == nullptr) return E_END_ARCHIVE;

	if (newentry->FileAttr & ATTR_DIRECTORY) return 0;

	if (DestPath) strcpy(dest, DestPath);
	if (DestName) strcat(dest, DestName);

	std::unique_ptr<char[]> buff;
	try {
		buff = std::make_unique<char[]>( static_cast<size_t>(arch->cluster_size));
	}
	catch (std::bad_alloc& ) {
		return E_NO_MEMORY;
	}

	hUnpFile = open_file_write(dest);
	if (hUnpFile == file_open_error_v) 
		return E_ECREATE;

	i = 0;
	nextclus = newentry->FirstClus;
	remaining = newentry->FileSize;
	while (remaining > 0)
	{
		if ((nextclus <= 1) || (nextclus >= 0xFF0))
		{
			close_file(hUnpFile);
			return E_UNKNOWN_FORMAT;
		}
		set_file_pointer(arch->hArchFile, arch->dataarea_off + static_cast<size_t>(nextclus - 2) * arch->cluster_size); //-V104
		towrite = (remaining > arch->cluster_size) ? (arch->cluster_size) : (remaining); //-V101
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
		if (remaining > arch->cluster_size) { remaining -= arch->cluster_size; }
		else { remaining = 0; }

		nextclus = next_cluster_FAT12(nextclus, arch);
		i++;
	}

	// set file time
	DosDateTimeToFileTime(WORD((DWORD((newentry->FileTime))) >> 16),
		WORD((DWORD((newentry->FileTime))) & 0xFFFF),
		&LocTime);
	LocalFileTimeToFileTime(&LocTime, &GlobTime);
	SetFileTime(hUnpFile, nullptr, nullptr, &GlobTime);

	close_file(hUnpFile);

	// set file attributes
	attributes = FILE_ATTRIBUTE_NORMAL;
	if (newentry->FileAttr & ATTR_READONLY) attributes |= FILE_ATTRIBUTE_READONLY;
	if (newentry->FileAttr & ATTR_ARCHIVE) attributes |= FILE_ATTRIBUTE_ARCHIVE;
	if (newentry->FileAttr & ATTR_HIDDEN) attributes |= FILE_ATTRIBUTE_HIDDEN;
	if (newentry->FileAttr & ATTR_SYSTEM) attributes |= FILE_ATTRIBUTE_SYSTEM;
	SetFileAttributes(dest, attributes);

	return 0;//ok
};

int IMG_Close(myHANDLE hArcData)
{
	tArchive* arch = hArcData;
	tDirEntry* newentry;

	while (arch->entrylist != nullptr)
	{
		newentry = arch->entrylist->next;
		delete arch->entrylist;
		arch->entrylist = newentry;
	}
	delete arch;

	return 0;// ok
};

void IMG_SetCallBackVol(myHANDLE hArcData, tChangeVolProc pChangeVolProc)
{
	tArchive* arch = (tArchive*)(hArcData);

	arch->pLocChangeVol = pChangeVolProc;
};

void IMG_SetCallBackProc(myHANDLE hArcData, tProcessDataProc pProcessDataProc)
{
	tArchive* arch = (tArchive*)(hArcData);

	arch->pLocProcessData = pProcessDataProc;
};


//-----------------------=[ DLL exports ]=--------------------

// OpenArchive should perform all necessary operations when an archive is to be opened
myHANDLE __stdcall OpenArchive(tOpenArchiveData* ArchiveData)
{
	return IMG_Open(ArchiveData);
}

// WinCmd calls ReadHeader to find out what files are in the archive
int __stdcall ReadHeader(myHANDLE hArcData, tHeaderData* HeaderData)
{
	return IMG_NextItem(hArcData, HeaderData);
}

// ProcessFile should unpack the specified file or test the integrity of the archive
int __stdcall ProcessFile(myHANDLE hArcData, int Operation, char* DestPath, char* DestName) //-V2009
{
	return IMG_Process(hArcData, Operation, DestPath, DestName);
}

// CloseArchive should perform all necessary operations when an archive is about to be closed
int __stdcall CloseArchive(myHANDLE hArcData)
{
	return IMG_Close(hArcData);
}

// This function allows you to notify user about changing a volume when packing files
void __stdcall SetChangeVolProc(myHANDLE hArcData, tChangeVolProc pChangeVolProc)
{
	IMG_SetCallBackVol(hArcData, pChangeVolProc);
}

// This function allows you to notify user about the progress when you un/pack files
void __stdcall SetProcessDataProc(myHANDLE hArcData, tProcessDataProc pProcessDataProc)
{
	IMG_SetCallBackProc(hArcData, pProcessDataProc);
}
