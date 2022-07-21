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
using std::nothrow;

// The DLL entry point

BOOL APIENTRY DllMain(HANDLE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	return TRUE;
}

//----------------FAT12 Definitions-------------

// BPB = BIOS Parameter Block
// BS = Boot Sector

typedef struct
{
	BYTE  BS_jmpBoot[3];
	BYTE  BS_OEMName[8];
	BYTE  BPB_BytesPerSec[2];
	BYTE  BPB_SecPerClus;
	BYTE  BPB_RsvdSecCnt[2];
	BYTE  BPB_NumFATs;
	BYTE  BPB_RootEntCnt[2];
	BYTE  BPB_TotSec16[2];
	BYTE  BPB_Media;
	BYTE  BPB_FATSz16[2];
	BYTE  BPB_SecPerTrk[2];
	BYTE  BPB_NumHeads[2];
	BYTE  BPB_HiddSec[4]; //-V112
	BYTE  BPB_TotSec32[4]; //-V112
	BYTE  BS_DrvNum;
	BYTE  BS_Reserved1;
	BYTE  BS_BootSig;
	BYTE  BS_VolID[4]; //-V112
	BYTE  BS_VolLab[11];
	BYTE  BS_FilSysType[8];
	BYTE  remaining_part[448];
	BYTE  signature[2];
} tFAT12BootSec;
static_assert(sizeof(tFAT12BootSec) == 512, "Wrong boot sector structure size");

typedef struct tFAT12Table2
{
	BYTE data[12 * 512];
} tFAT12Table;

typedef struct
{
	char  DIR_Name[11];
	BYTE  DIR_Attr;
	BYTE  DIR_NTRes;
	BYTE  DIR_CrtTimeTenth;
	BYTE  DIR_CrtTime[2];
	BYTE  DIR_CrtDate[2];
	BYTE  DIR_LstAccDate[2];
	BYTE  DIR_FstClusHI[2];
	BYTE  DIR_WrtTime[2];
	BYTE  DIR_WrtDate[2];
	BYTE  DIR_FstClusLO[2];
	BYTE  DIR_FileSize[4]; //-V112
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

typedef struct
{
	char archname[MAX_PATH];
	HANDLE hArchFile;    //opened file handle

	tFAT12Table* fattable;
	tDirEntry* entrylist;
	tFAT12BootSec* bootsec;

	LONG rootarea_ptr; //number of bytes before root area
	LONG dataarea_ptr; //number of bytes before data area
	DWORD rootentcnt;
	DWORD fatsize;// in sectors
	DWORD cluster_size; // in bytes
	DWORD counter;	

	tChangeVolProc pLocChangeVol;
	tProcessDataProc pLocProcessData;
} tArchive;

typedef tArchive* myHANDLE;

//--------End of  IMG Definitions-------------

//------------------=[ Global Varailables ]=-------------

// tChangeVolProc pGlobChangeVol;
// tProcessDataProc pGlobProcessData;


//------------------=[ "Kernel" ]=-------------


#define AT_OK      0
#define AT_VOL     1
#define AT_DIR     2
#define AT_LONG    3
#define AT_INVALID 4

DWORD DIR_AttrToFileAttr(BYTE DIR_Attr, unsigned* FileAttr)
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
	if (firstclus & 0x1)
	{
		return (DWORD(arch->fattable->data[((firstclus * 3) >> 1)]) +
			(DWORD(arch->fattable->data[((firstclus * 3) >> 1) + 1]) << 8)) >> 4;
	}
	return (    DWORD(arch->fattable->data[((firstclus * 3) >> 1)]) +
		    (DWORD(arch->fattable->data[((firstclus * 3) >> 1) + 1]) << 8)) & 0xFFF;
}

int CreateFileList(const char* root, DWORD firstclus, tArchive* arch, DWORD depth)
{
	tDirEntry* newentry;
	tFAT12DirEntry* sector = nullptr;
	DWORD i, j;
	DWORD result;
	DWORD portion_size = 0;

	if (firstclus == 0)
	{
		SetFilePointer(arch->hArchFile, arch->rootarea_ptr, 0, FILE_BEGIN);
		portion_size = 512;
	}
	else {
		SetFilePointer(arch->hArchFile, arch->dataarea_ptr + (firstclus - 2) * 512, 0, FILE_BEGIN);
		portion_size = arch->cluster_size;
	}	
	DWORD records_number = portion_size / static_cast<DWORD>(sizeof(tFAT12DirEntry));
	sector = new(nothrow) tFAT12DirEntry[records_number]; //-V121
	if (sector == NULL) goto error;
	if ((firstclus == 1) || (firstclus >= 0xFF0)) goto error;
	bool res = ReadFile(arch->hArchFile, sector, portion_size, &result, 0); //-V124
	if (!res || result != portion_size) goto error;

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
		if ((firstclus == 0) && ((i * records_number) >= arch->rootentcnt)) goto error;
		if (firstclus == 0)
		{
			SetFilePointer(arch->hArchFile, arch->rootarea_ptr + i * 512, 0, FILE_BEGIN);
		}
		else {
			firstclus = NextClus(firstclus, arch);
			if ((firstclus <= 1) || (firstclus >= 0xFF0)) goto error;
			SetFilePointer(arch->hArchFile, arch->dataarea_ptr + (firstclus - 2) * portion_size, 0, FILE_BEGIN);
		}
		ReadFile(arch->hArchFile, sector, portion_size, &result, 0); 
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
	DWORD result;

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
	arch->hArchFile = CreateFile(arch->archname, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	if (arch->hArchFile == INVALID_HANDLE_VALUE)
	{
		goto error;
	}

	//----------begin of bootsec in use
	if ((arch->bootsec = new(nothrow) tFAT12BootSec) == nullptr)
	{
		goto error;
	}
	bool res = ReadFile(arch->hArchFile, arch->bootsec, 512, &result, 0); //-V124
	if (!res || result != 512)
	{
		ArchiveData->OpenResult = E_EREAD;
		goto error;
	}

	if ((arch->bootsec->BPB_BytesPerSec[0] != 0x00) ||
		(arch->bootsec->BPB_BytesPerSec[1] != 0x02) ||
		(arch->bootsec->signature[0] != 0x55)		||
		(arch->bootsec->signature[1] != 0xAA) 
		)
	{
		ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
		goto error;
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
	res = ReadFile(arch->hArchFile, arch->fattable, arch->fatsize * 512, &result, 0);
	if (!res || result != arch->fatsize * 512)
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
	if (arch->hArchFile != nullptr) CloseHandle(arch->hArchFile); // INVALID_HANDLE_VALUE
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
	DWORD result;
	DWORD towrite;
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

	hUnpFile = CreateFile(dest, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_NEW, 0, 0);
	if (hUnpFile == INVALID_HANDLE_VALUE) return E_ECREATE;

	i = 0;
	nextclus = newentry->FirstClus;
	remaining = newentry->FileSize;
	while (remaining > 0)
	{
		if ((nextclus <= 1) || (nextclus >= 0xFF0))
		{
			CloseHandle(hUnpFile);
			return E_UNKNOWN_FORMAT;
		}
		SetFilePointer(arch->hArchFile, arch->dataarea_ptr + (nextclus - 2) * arch->cluster_size,
			           0, FILE_BEGIN);
		towrite = (remaining > arch->cluster_size) ? (arch->cluster_size) : (remaining);
		ReadFile(arch->hArchFile, buff.get(), towrite, &result, 0);
		if (result != towrite)
		{
			CloseHandle(hUnpFile);
			return E_EREAD;
		}
		WriteFile(hUnpFile, buff.get(), towrite, &result, 0);
		if (result != towrite)
		{
			CloseHandle(hUnpFile);
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

	CloseHandle(hUnpFile);

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

	CloseHandle(arch->hArchFile);
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
int __stdcall ProcessFile(myHANDLE hArcData, int Operation, char* DestPath, char* DestName)
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
