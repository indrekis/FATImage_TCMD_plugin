#pragma once

#ifndef FAT_DEFINITIONS_H_INCLUDED
#define FAT_DEFINITIONS_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <bit>
#include <memory>

//! https://wiki.osdev.org/FAT#BPB_.28BIOS_Parameter_Block.29
//! FAT is little endian.
// BPB = BIOS Parameter Block
// BS = (Extended) Boot Record. Extended BR is also called EBPB 
#pragma pack(push, 1) // See also __attribute__((packed)) 
struct tFAT12BootSec
{
	//--------------------------// Common part for the DOS 2.0+
	uint8_t  BS_jmpBoot[3];		// 0x000
	uint8_t  BS_OEMName[8];		// 0x003
	uint16_t BPB_bytesPerSec;	// 0x00B
	uint8_t  BPB_SecPerClus;	// 0x00D
	uint16_t BPB_RsvdSecCnt;	// 0x00E
	uint8_t  BPB_NumFATs;		// 0x010, often 1 or 2
	uint16_t BPB_RootEntCnt;	// 0x011; 0 for FAT32, but check at 0x042 signature to be 0x29 or 0x28. 
								//		  MS-DOS supports 240 max for FDD and 512 for HDD
	uint16_t BPB_TotSec16;      // 0x013; 0 if more than 65535, then val in. BPB_TotSec32 at 0x020 is used. 0 for FAT32
	uint8_t  BPB_MediaDescr;	// 0x015; same as 1-st byte of FAT. See https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system#FATID
	uint16_t BPB_SectorsPerFAT; // 0x016; FAT12/16, 0 for FAT32, it uses val at 0x024.
	//--------------------------// End of common part for the DOS 2.0+.
	uint16_t BPB_SecPerTrk;		// 0x018; If 0 -- reserved, not used. DOS 3.00+
	uint16_t BPB_NumHeads;		// 0x01A; DOS up to 7.10 have bug here, so 255 heads max. 0 -- reserved, not used. DOS 3.00+
	// DOS 3.00, 3.20 use slightly incompatible BPB here, future systems use DOS 3.31 BPB
	// For reference:
	// BPB 3.00
	// uint16_t BPB_HiddSec;	// 0x01C; 2 bytes. Sectors on disk before this partition. 0 for non-partitioned disks. Do not use if 0x013 == 0.
	// BPB 3.20
	// uint16_t BPB_TotHiddSec;	// 0x01E; Total logical sectors including hidden sectors. Do not use if 0x013 == 0.
	//--------------------------// DOS 3.31+ BPB
	uint32_t BPB_HiddSec;       // 0x01C; 4 bytes. Sectors on disk before this partition -- the LBA of the beginning of the partition. 
								//		  0 for non-partitioned disks. Do not use if 0x013 == 0. 
	uint32_t BPB_TotSec32;		// 0x020; if more than 65535, then  0x013 == 0
	//----------------------------------------
	// Extended Boot Record ( Extended BIOS Parameter Block (EBPB) ) for FAT12/16; DOS 4.00+; OS/2 1.00+
	uint8_t  BS_DrvNum;			// 0x024; 0x00-0x7E for removable, 0x80-0xFE -- fixed disks. 0x7F and 0xFF for boot ROMs and so on. 
								//		  For DOS 3.2 to 3.31 -- similar entry at 0x1FD.
	uint8_t  BS_ReservedOrNT;	// 0x025; For NT: bit 1: physical error, bit 0: was not properly unmounted, check it on next boot.
	uint8_t  BS_BootSig;		// 0x026; Extended boot signature. 0x29 -- contains all 3 next fields, (DOS 4.0, OS/2 1.2);
								//		  0x28 -- only BS_VolID
	uint32_t BS_VolID;			// 0x027; Volume serial number
	uint8_t  BS_VolLab[11];		// 0x02B; Partition volume label, should be padded by spaces. Should be equal to vol. label, but frequently is not
	char     BS_FilSysType[8];  // 0x036; FS Type, "FAT     ", "FAT12   ", "FAT16   ", "FAT32   ",
								//		  "The spec says never to trust the contents of this string for any use", but see: http://jdebp.info/FGA/determining-fat-widths.html
	// Rest of bootsector
	uint8_t  remaining_part[448];
	uint16_t signature;         // 0x1FE; 0xAA55 (Little endian: signature[0] == 0x55, signature[1] == 0xAA)
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

	bool is_volumeID() const { return attribute == ATTR_VOLUME_ID; }

	bool is_longname_part() const { return attribute == ATTR_LONG_NAME;	}

	bool is_dir() const { return attribute & ATTR_DIRECTORY; }

	bool is_invalid() const {
		bool res = ((attribute & 0xC8) != 0);
		res |= ((!is_volumeID()) && ((attribute & ATTR_VOLUME_ID) != 0));
		// Other checks here
		return res;
	}

	bool is_readonly() const { return attribute & ATTR_READONLY; }

	bool is_archive() const { return attribute & ATTR_ARCHIVE; }

	bool is_hidden() const { return attribute & ATTR_HIDDEN; }

	bool is_system() const { return attribute & ATTR_SYSTEM; }

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


//! Platform-independent IO function. 
//! Returns true if successful

inline uint32_t combine(uint16_t hi, uint16_t lo) {
	return (static_cast<uint32_t>(hi) << (sizeof(hi) * CHAR_BIT)) + lo;
}

#pragma pack(push, 1)
struct FATxx_dir_entry_t
{
	char  DIR_Name[11];			// 0x00; 8 + 3, padded by spaces
	FAT_attrib_t  DIR_Attr;		// 0x0B; 
	uint8_t  DIR_NTRes;		    // 0x0C; WinNT uses bits 3 & 4 to save case info (see https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system#VFAT)
	uint8_t  DIR_CrtTimeTenth;  // 0x0D; Create time in 10 ms units (0-199), first char of deleted file name Novell DOS, OpenDOS and DR-DOS 7.02+
	uint16_t DIR_CrtTime;		// 0x0E: Create time for DOS 7.0 VFAT. Used as part of password protection on some DR DOS descendants.
	uint16_t DIR_CrtDate;		// 0x10: Create date for DOS 7.0 VFAT. 
	uint16_t DIR_LstAccDate;	// 0x12: Last access date for DOS 7.0 VFAT. Owner ID for some  DR DOS descendants.
	uint16_t DIR_FstClusHI;		// 0x14: High bytes of first cluster on FAT32. 0 for MS DOS FAT12/16.
								//       Access rights bitmap for DR DOS descendants.
	uint16_t DIR_WrtTime;		// 0x16: Last modified time.
	uint16_t DIR_WrtDate;		// 0x18: Last modified date.
	uint16_t DIR_FstClusLO;		// 0x1A: Low bytes of first cluster. Vol Label, ".." pointing to root on FAT12/16, 
								//       empty files, VFAT LFN should have 0 here.
	uint32_t DIR_FileSize;		// 0x1C: File size in bytes. Vol label and dirs should have 0 here. VFAT LFNs never store 0 here.

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
	uint32_t dir_entry_name_to_str(T& name);

	uint32_t get_first_cluster_FAT12() const {
		return DIR_FstClusLO;
	}

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

template<typename T>
uint32_t FATxx_dir_entry_t::dir_entry_name_to_str(T& name) {
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

#pragma pack(pop)
static_assert(offsetof(FATxx_dir_entry_t, DIR_Attr) == 11, "Wrong FAT_attrib_t offset");
static_assert(sizeof(FATxx_dir_entry_t) == 32, "Wrong size of FATxx_dir_entry_t"); //-V112


#endif 

