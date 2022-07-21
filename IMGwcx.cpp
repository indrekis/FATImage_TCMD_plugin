// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/*
IMG unpack plugin
Copyright (C) 2002 IvGzury
	ivgzury@hotmail.com

This program is absolutely free software.
If you have any remarks or problems, please don't
hesitate to send me an email.

It was made for Windows Commander.
Windows Commander: is an excellent file manager
				   made by Christian Ghisler
*/

#pragma pvs(disable: 2005,2001,303)
// 2005 -- C casts
// 2001, 303 -- about deprecated functions, like SetFilePointer/SetFilePointerEx

#include "stdafx.h"
#include "wcxhead.h"
#include <new>
#include <memory>
#include <cstddef>
using std::nothrow, std::uint8_t;

// The DLL entry point

BOOL APIENTRY DllMain(HANDLE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	return TRUE;
}

//----------------FAT12 Definitions-------------

constexpr size_t sector_size = 512;

// BPB = BIOS Parameter Block
// BS = Boot Sector
//! FAT is little endian.
typedef struct
{
	uint8_t  BS_jmpBoot[3];
	uint8_t  BS_OEMName[8];
	uint8_t  BPB_bytesPerSec[2];
	uint8_t  BPB_SecPerClus;
	uint8_t  BPB_RsvdSecCnt[2];
	uint8_t  BPB_NumFATs;
	uint8_t  BPB_RootEntCnt[2];
	uint8_t  BPB_TotSec16[2];
	uint8_t  BPB_Media;
	uint8_t  BPB_FATSz16[2];
	uint8_t  BPB_SecPerTrk[2];
	uint8_t  BPB_NumHeads[2];
	uint8_t  BPB_HiddSec[4]; //-V112
	uint8_t  BPB_TotSec32[4]; //-V112
	uint8_t  BS_DrvNum;
	uint8_t  BS_Reserved1;
	uint8_t  BS_BootSig;
	uint8_t  BS_VolID[4]; //-V112
	uint8_t  BS_VolLab[11];
	uint8_t  BS_FilSysType[8];
	uint8_t  remaining_part[448];
	uint8_t  signature[2];
} tFAT12BootSec;

static_assert(sizeof(tFAT12BootSec) == 512, "Wrong boot sector structure size");
static_assert(std::endian::native == std::endian::little, "Wrong endiannes");

typedef struct tFAT12Table2
{
	uint8_t data[12 * 512];
} tFAT12Table;

typedef struct
{
	char  DIR_Name[11];
	uint8_t  DIR_Attr;
	uint8_t  DIR_NTRes;
	uint8_t  DIR_CrtTimeTenth;
	uint8_t  DIR_CrtTime[2];
	uint8_t  DIR_CrtDate[2];
	uint8_t  DIR_LstAccDate[2];
	uint8_t  DIR_FstClusHI[2];
	uint8_t  DIR_WrtTime[2];
	uint8_t  DIR_WrtDate[2];
	uint8_t  DIR_FstClusLO[2];
	uint8_t  DIR_FileSize[4]; //-V112
} tFAT12DirEntry;

static_assert(sizeof(tFAT12DirEntry) == 32, "Wrong size of tFAT12DirEntry"); //-V112

#define ATTR_READONLY     0x01
#define ATTR_HIDDEN       0x02
#define ATTR_SYSTEM       0x04
#define ATTR_VOLUME_ID    0x08
#define ATTR_DIRECTORY    0x10
#define ATTR_ARCHIVE      0x20
#define ATTR_LONG_NAME    0x0F


//--------End of  FAT12 Definitions-------------

//----------------IMG Definitions-------------

typedef struct tDirEntry
{
	char FileName[260];
	char PathName[260];
	unsigned FileSize;
	unsigned FileTime;
	unsigned FileAttr;
	DWORD FirstClus;
	tDirEntry* next;
	tDirEntry* prev;
} tDirEntry;

using file_handle_t = HANDLE;
typedef struct
{
	char archname[MAX_PATH];
	file_handle_t hArchFile;    //opened file handle

	tFAT12Table* fattable;
	tDirEntry* entrylist;
	tFAT12BootSec* bootsec;

	size_t rootarea_ptr; //number of uint8_t before root area
	size_t dataarea_ptr; //number of uint8_t before data area
	uint32_t rootentcnt;
	uint32_t fatsize;// in sectors
	uint32_t cluster_size; // in uint8_ts
	uint32_t counter;

	tChangeVolProc pLocChangeVol;
	tProcessDataProc pLocProcessData;
} tArchive;

typedef tArchive* myHANDLE;

//--------End of  IMG Definitions-------------

//------------------=[ Global Varailables ]=-------------

// tChangeVolProc pGlobChangeVol;
// tProcessDataProc pGlobProcessData;


//------------------=[ "Kernel" ]=-------------

static const auto file_open_error_v = INVALID_HANDLE_VALUE;

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
	res = ReadFile(handle, buffer_ptr, static_cast<DWORD>(size), &result, nullptr);
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
	res = WriteFile(handle, buffer_ptr, static_cast<DWORD>(size), &result, nullptr);
	if (!res) {
		return static_cast<size_t>(-1);
	}
	else {
		return static_cast<size_t>(result);
	}
}

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
	const char nonValid[] = R"("*+,./:;<=>?[\]|)";

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

DWORD NextClus(DWORD firstclus, const tArchive* arch)
{
	const auto FAT_byte_pre = arch->fattable->data + ((firstclus * 3) >> 1); // firstclus + firstclus/2
	//! Extract word, containing next cluster:
	const uint16_t* word_ptr = reinterpret_cast<const uint16_t*>(FAT_byte_pre);
	// Extract correct 12 bits -- lower for odd, upper for even: 
	return ( (*word_ptr) >> (( firstclus % 2) ? 4 : 0) ) & 0x0FFF;
#if 0
	if (firstclus & 0x1)
	{
		return (DWORD(arch->fattable->data[((firstclus * 3) >> 1)]) +
			(DWORD(arch->fattable->data[((firstclus * 3) >> 1) + 1]) << 8)) >> 4;
	}
	return (    DWORD(arch->fattable->data[((firstclus * 3) >> 1)]) +
		    (DWORD(arch->fattable->data[((firstclus * 3) >> 1) + 1]) << 8)) & 0xFFF;
#endif 
}

int CreateFileList(const char* root, DWORD firstclus, tArchive* arch, DWORD depth)
{
	tDirEntry* newentry;
	tFAT12DirEntry* sector = nullptr;
	DWORD i, j;
	size_t result;
	size_t portion_size = 0;

	if (firstclus == 0)
	{
		set_file_pointer(arch->hArchFile, arch->rootarea_ptr);
		portion_size = 512;
	}
	else {
		set_file_pointer(arch->hArchFile, arch->dataarea_ptr + (firstclus - 2) * sector_size); //-V104
		portion_size = arch->cluster_size; //-V101
	}	
	size_t records_number = portion_size / sizeof(tFAT12DirEntry);
	sector = new(nothrow) tFAT12DirEntry[records_number];  //-V121
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
			newentry->FileTime = ((DWORD)sector[j].DIR_WrtDate[1] << 24) +
				((DWORD)sector[j].DIR_WrtDate[0] << 16) +
				((DWORD)sector[j].DIR_WrtTime[1] << 8) +
				(DWORD)sector[j].DIR_WrtTime[0];
			newentry->FileSize = ((DWORD)sector[j].DIR_FileSize[3] << 24) +
				((DWORD)sector[j].DIR_FileSize[2] << 16) +
				((DWORD)sector[j].DIR_FileSize[1] << 8) +
				(DWORD)sector[j].DIR_FileSize[0];
			newentry->FirstClus = ((DWORD)sector[j].DIR_FstClusLO[1] << 8) +
				(DWORD)sector[j].DIR_FstClusLO[0];
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
			set_file_pointer(arch->hArchFile, arch->rootarea_ptr + i * sector_size); //-V104
		}
		else {
			firstclus = NextClus(firstclus, arch);
			if ((firstclus <= 1) || (firstclus >= 0xFF0)) goto error;
			set_file_pointer(arch->hArchFile, arch->dataarea_ptr + static_cast<size_t>(firstclus - 2) * portion_size); 
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
	tArchive* arch = nullptr;
	size_t result;

	ArchiveData->CmtBuf = 0;
	ArchiveData->CmtBufSize = 0;
	ArchiveData->CmtSize = 0;
	ArchiveData->CmtState = 0;

	ArchiveData->OpenResult = E_NO_MEMORY;// default error type
	if ((arch = new(nothrow) tArchive) == nullptr)
	{
		return nullptr;
	}

	// trying to open
	memset(arch, 0, sizeof(tArchive));
	strcpy(arch->archname, ArchiveData->ArcName);
	
	arch->hArchFile = open_file_shared_read(arch->archname);
	if (arch->hArchFile == file_open_error_v)
	{
		goto error;
	}

	//----------begin of bootsec in use
	if ((arch->bootsec = new(nothrow) tFAT12BootSec) == nullptr)
	{
		goto error;
	}
	result = read_file(arch->hArchFile, arch->bootsec, sector_size);
	if (result != sector_size)
	{
		ArchiveData->OpenResult = E_EREAD;
		goto error;
	}

	if ((arch->bootsec->BPB_bytesPerSec[0] != 0x00) ||
		(arch->bootsec->BPB_bytesPerSec[1] != 0x02) 
		)
	{
		ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
		goto error;
	}

	if ((arch->bootsec->signature[0] != 0x55) ||
		(arch->bootsec->signature[1] != 0xAA)
		)
	{
		//! Is it correct to create own dialogs in plugin?
		int msgboxID = MessageBox(
			NULL,
			TEXT("Wrong BPB signature\nContinue?"),
			TEXT("BPB Signature error"),
			MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1
		);
		if (msgboxID == IDCANCEL) {
			ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
			goto error;
		}
	}

	arch->fatsize = 256 * DWORD(arch->bootsec->BPB_FATSz16[1]) +
		arch->bootsec->BPB_FATSz16[0];
	if ((arch->fatsize < 1) || (arch->fatsize > 12))
	{
		ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
		goto error;
	}
	arch->rootentcnt = 256 * DWORD(arch->bootsec->BPB_RootEntCnt[1]) +
		arch->bootsec->BPB_RootEntCnt[0];
	//224 is the maximum in a 1.44 floppy
	// But 2.88 disk exists
	/*
	if (arch->rootentcnt > 0xE0) 
	{
		ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
		goto error;
	}*/
	arch->cluster_size = 512 * arch->bootsec->BPB_SecPerClus;
	arch->rootarea_ptr = (1 + arch->fatsize * arch->bootsec->BPB_NumFATs) * 512;
	arch->dataarea_ptr = arch->rootarea_ptr + arch->rootentcnt * sizeof(tFAT12DirEntry); 
	//----------end of bootsec in use

	// trying to read fat table
	if ((arch->fattable = new(nothrow) tFAT12Table) == nullptr)
	{
		goto error;
	}
	result = read_file(arch->hArchFile, arch->fattable, arch->fatsize * sector_size);
	if (result != arch->fatsize * sector_size)
	{
		ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
		goto error;
	}

	// trying to read root directory
	arch->counter = 0;
	arch->entrylist = nullptr;
	CreateFileList("", 0, arch, 0);
	if (arch->entrylist != nullptr)
	{
		while (arch->entrylist->prev != nullptr)
		{
			arch->entrylist = arch->entrylist->prev;
		}
	}

	ArchiveData->OpenResult = 0;// ok
	return arch;

error:
	// memory must be freed
	if (arch->hArchFile != nullptr) close_file(arch->hArchFile); 
	delete arch->fattable;
	delete arch->bootsec;
	delete arch;
	return nullptr;
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
		set_file_pointer(arch->hArchFile, arch->dataarea_ptr + static_cast<size_t>(nextclus - 2) * arch->cluster_size); //-V104
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

		nextclus = NextClus(nextclus, arch);
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

	close_file(arch->hArchFile);
	delete arch->fattable;
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
