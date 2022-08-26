#pragma once

#ifndef FAT_DEFINITIONS_H_INCLUDED
#define FAT_DEFINITIONS_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <bit>
#include <memory>

#include "sysio_winapi.h"

//! https://wiki.osdev.org/FAT#BPB_.28BIOS_Parameter_Block.29
//! FAT is little endian.
// BPB = BIOS Parameter Block
// BS = (Extended) Boot Record. Extended BR is also called EBPB 
#pragma pack(push, 1) // See also __attribute__((packed)) 

// DOS 3.00, 3.20 use slightly incompatible BPB here, future systems use DOS 3.31 BPB
//! See also http://jdebp.info/FGA/bios-parameter-block.html
struct BPB_DOS3x0_FAT_t {
// For reference:
// BPB 3.00
	uint16_t BPB_HiddSec;		// 0x01C; 2 bytes. Sectors on disk before this partition. 0 for non-partitioned disks. 
								// Do not use if 0x013 == 0.
// BPB 3.20
	uint16_t BPB_TotHiddSec;	// 0x01E; Total logical sectors including hidden sectors. Do not use if 0x013 == 0.
	uint8_t  padding[58];
};

// Extended Boot Record or Extended BIOS Parameter Block (EBPB) for FAT12/16; DOS 4.00+; OS/2 1.00+
// (+ part of BPB, incompartible with DOS 3.20-)
struct EBPB_FAT_t {
	uint32_t BPB_HiddSec;       // 0x01C; 4 bytes. Sectors on disk before this partition -- the LBA of the beginning of the partition. 
								//		  0 for non-partitioned disks. Do not use if 0x013 == 0. 
	uint32_t BPB_TotSec32;		// 0x020; if more than 65535, then  0x013 == 0
	//----------------------------------------
	uint8_t  BS_DrvNum;			// 0x024; 0x00-0x7E for removable, 0x80-0xFE -- fixed disks. 0x7F and 0xFF for boot ROMs and so on. 
								//		  For DOS 3.2 to 3.31 -- similar entry at 0x1FD.
	uint8_t  BS_ReservedOrNT;	// 0x025; For NT: bit 1: physical error, bit 0: was not properly unmounted, check it on next boot.
	uint8_t  BS_BootSig;		// 0x026; Extended boot signature. 0x29 -- contains all 3 next fields, (DOS 4.0, OS/2 1.2);
								//		  0x28 -- only BS_VolID
	uint32_t BS_VolID;			// 0x027; Volume serial number
	uint8_t  BS_VolLab[11];		// 0x02B; Partition volume label, should be padded by spaces. Should be equal to vol. label, but frequently is not
	char     BS_FilSysType[8];  // 0x036; FS Type, "FAT     ", "FAT12   ", "FAT16   ", "FAT32   ",
								//		  "The spec says never to trust the contents of this string for any use", but see: http://jdebp.info/FGA/determining-fat-widths.html
	uint8_t	 padding[62-34];
}; // Sizeof 0x22 = 34 + padding, sizeof tFAT_EBPB_FAT32 == 0x3E == 62

// (+ part of BPB, incompartible with DOS 3.20-)
struct EBPB_FAT32_t {
	uint32_t BPB_HiddSec;       // 0x01C; 4 bytes. Sectors on disk before this partition -- the LBA of the beginning of the partition. 
								//		  0 for non-partitioned disks. Do not use if 0x013 == 0. 
	uint32_t BPB_TotSec32;		// 0x020; if more than 65535, then  0x013 == 0
	//----------------------------------------
	uint32_t BS_SectorsPerFAT32;// 0x024; The byte at offset 0x026 should never == 0x29 or 0x28, to avoid a misindentification with BS_BootSig
								//		   Then BPB_SectorsPerFAT (0x016) should be zero.
	uint16_t BS_ExtFlags;		// 0x028; If bit 7 set; bits 3-0 select active FAT; If not -- FATs are mirrored as usual.
								//			Ignore other bits -- for MS products they are 0, but DR-DOS may use them.
	uint8_t	 BS_FSVer_minor;	// 0x02A; Should be 0.0.
	uint8_t	 BS_FSVer_major;	// 0x02B;
	uint32_t BS_RootFirstClus;  // 0x02C; First cluster of root dir. Never should be 0. Nonstandard implementation may use 0 to 
								//		  show that fixed old-style root dir used.
	uint16_t BS_FSInfoSec;		// 0x030; Logical sector number of FS Information Sector, placed in reserved area, typically 1.
								//		  Nonstandard implementations may use 0x0000 or 0xFFFF to show absent Info. Sec. 
								//		  Absent Info. Sec. allow use sector size 128.
	uint16_t BS_KbpBootSec;		// 0x032; First (logical) sector of boot sectors copy. Mostly should be equal to 6.
								//		  0x0000 or 0xFFFF  -- no backup
	uint8_t  BS_Reserved[12];	// 0x034; 
	//--------------------------// Same as last part of FAT12/16 BPB.
	uint8_t  BS_DrvNum;			// 0x040; Same as 0x024 in tFAT_EBPB_FAT.
	uint8_t  BS_ReservedOrNT;	// 0x041; Same as 0x025 in tFAT_EBPB_FAT. bit 1: physical error, bit 0: was not properly unmounted, check it on next boot.
	uint8_t  BS_BootSig;		// 0x042; Same as 0x026 in tFAT_EBPB_FAT. Most 0x29, but could be 0x28, then only BS_VolID is present
	uint32_t BS_VolID;			// 0x043; Same as 0x027 in tFAT_EBPB_FAT. 
	uint8_t  BS_VolLab[11];		// 0x047; Same as 0x02B in tFAT_EBPB_FAT. 
	char     BS_FilSysType[8];  // 0x052; Same as 0x036 in tFAT_EBPB_FAT. 
								//		   Some implementations can use it as a 64-bit total logical sectors count if 0x020 and 0x013 are 0
	bool is_FAT_mirrored() const {
		return !(BS_ExtFlags & 1 << 7);
	}
	uint32_t get_active_FAT() const {
		return BS_ExtFlags & 0x0F;
	}
	void get_volume_label(char* vol) {
		size_t idx = 0;
		while (idx < sizeof(BS_VolLab) && BS_VolLab[idx] != ' ') {
			vol[idx] = BS_VolLab[idx];
			++idx;
		}
		vol[idx] = '\0';
	}
}; // 0x3E = 62

static_assert(sizeof(BPB_DOS3x0_FAT_t) == sizeof(EBPB_FAT_t), "Wrong variadic BPB part size");
static_assert(sizeof(EBPB_FAT_t) == sizeof(EBPB_FAT32_t), "Wrong variadic BPB part size");

struct FAT_boot_sector_t
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
	//--------------------------// Relies here on correct type punning. GCC supports it: https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html#Type%2Dpunning
								// Didn't found (yet?) in MSVC docs, but it's headers use type punning heavily. Clang -- unknown.
	union {
		BPB_DOS3x0_FAT_t BPB_DOS3x0;
		EBPB_FAT_t	EBPB_FAT;
		EBPB_FAT32_t EBPB_FAT32;
	};
	uint8_t  remaining_part[420];
	uint16_t signature;         // 0x1FE; 0xAA55 (Little endian: signature[0] == 0x55, signature[1] == 0xAA)

#if 0 // Left here as a visual reference
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
#endif 
};
static_assert(std::endian::native == std::endian::little, "Wrong endiannes");
static_assert(sizeof(FAT_boot_sector_t) == 512, "Wrong boot sector structure size");

//! Valid only if bits at 0x041 show clear shutdown.
//! All data in this sector are unreliable, should be used only as a optimization hint.
struct FAT32_FS_InfoSec {
	uint8_t  signature1[4];		// 0x000; Should be {0x52, 0x52, 0x61, 0x41} --  "RRaA". Sector is often situatet at the typical  //-V112
								//		  start of the FAT12/FAT16 FAT, so FAT32 partition would not be misidentified.
								//		  0x41615252  if read to uint32_t (on LE).
	uint8_t  reserved1[480];	// 0x004; Reserved
	uint8_t  signature2[4];		// 0x1E4; {0x72, 0x72, 0x41, 0x61} -- "rrAa" (0x61417272 if read to uint32_t on LE) //-V112
	uint32_t freeClus;			// 0x1E8; Last known number of free data clusters, 0xFFFFFFFF if unknown. 
	uint32_t busyClus;			// 0x1EC; Last known allocated clusters, 0xFFFFFFFF if unknown. OS should start searching for free clusters here
	uint8_t  reserved2[12];		// 0x1F0; Reserved
	uint8_t  signature3[4];		// 0x1FC; {0x00, 0x00, 0x55, 0xAA}, 0xAA550000 if read to uint32_t on LE. //-V112
};
static_assert(sizeof(FAT32_FS_InfoSec) == 512, "Wrong boot sector structure size");

#pragma pack(pop)

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
		bool res = ((attribute & 0xC0) != 0);
		res |= ((attribute & ATTR_VOLUME_ID) != 0) && (!is_volumeID()) && (!is_longname_part());
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
		return (DIR_Name[0] == '\xE5');
	}

	bool is_dir_record_unknown() const {
		return !(is_dir_record_free()) &&
			!(is_dir_record_deleted()) &&
			!is_valid_char(DIR_Name[0]);
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
	uint32_t dir_entry_name_to_str(T& name, bool process_OS2_EA_file = true);

	bool process_E5();

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

	static constexpr char non_valid_chars[] = R"("*+,./:;<=>?[\]|)";
	static int is_valid_char(char mychar)
	{
		return !((mychar >= '\x00') && (mychar <= '\x20')) &&
			strchr(non_valid_chars, mychar) == nullptr;
	}

	static constexpr char OS2_EA_file_entry[] = "EA DATA  SF";
	static constexpr char OS2_EA_file_name[] = "EA DATA. SF";
	static_assert(sizeof(OS2_EA_file_entry) == 11 + 1, "Wrong OS2_EA_file size");
	static_assert(sizeof(OS2_EA_file_name) == 11 + 1, "Wrong OS2_EA_file_name size");
	enum ll_dir_entry_props{ LLDE_OK = 0, LLDE_badinname = 1, LLDE_badinext = 2, LLDE_OS2_EA = 0xFFFF};
};

inline bool FATxx_dir_entry_t::process_E5() {
	// 0x05 is used as a placeholder for the symbol 0xE5 (which is used as deleted marker)
	// But it should be replaced after the test for deletion.
	// See also https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system#DIR_OFS_00h
	if (DIR_Name[0] == '\x05') {
		DIR_Name[0] = '\xE5';
		return false;
	}
	else {
		return true;
	}
}

template<typename T>
uint32_t FATxx_dir_entry_t::dir_entry_name_to_str(T& name, bool process_OS2_EA_file) {
	uint32_t invalid = 0;

	auto ea_os2_found = memcmp(&DIR_Name[0], OS2_EA_file_entry, 11);
	if (ea_os2_found == 0) {
		name.push_back(OS2_EA_file_name);
		return LLDE_OS2_EA;
	}
	else {
		for (int i = 0; i < 8; ++i) {
			if (!is_valid_char(DIR_Name[i])) {
				if (DIR_Name[i] != ' ') ++invalid;
				break;
			}
			name.push_back(DIR_Name[i]);
		}
		if (is_valid_char(DIR_Name[8]))
		{
			name.push_back('.');
		}
		for (int i = 8; i < 8 + 3; ++i) {
			if (!is_valid_char(DIR_Name[i])) {
				if (DIR_Name[i] != ' ') ++invalid;
				break;
			}
			name.push_back(DIR_Name[i]);
		}
	}
	return invalid;
}

#pragma pack(pop)
static_assert(offsetof(FATxx_dir_entry_t, DIR_Attr) == 11, "Wrong FAT_attrib_t offset");
static_assert(sizeof(FATxx_dir_entry_t) == 32, "Wrong size of FATxx_dir_entry_t"); //-V112

#pragma pack(push, 1)
struct VFAT_LFN_dir_entry_t
{
	uint8_t  LFN_index;			 // 0x00; Index of LFN record. Bit 6 set in last LFN item, bits 4-0 -- LFN number
	uint16_t LFN_name_part1[5];  // 0x01; First 5 UCS-2 symbols
	FAT_attrib_t  DIR_Attr;		 // 0x0B; Should be 0x0F
	uint8_t	 LFN_type;			 // 0x0C; Should be 0. WinNT uses bits 3 (lowercase name) & 4 (lowercase ext).
								 //		  For names of type "nnnnnnnn.ext", "nnnnnnnn.EXT", "NNNNNNNN.ext",
								 //		  WinNT does not create VFAT entry, but sets bits 4 and 3: '11', '10', '01' respectively
	uint8_t  LFN_DOS_name_CRC;	 // 0x0D; Checksum of the DOS short dir record this LFN belongs to. See LFN_checksum().
	uint16_t LFN_name_part2[6];  // 0x0E; Second 6 UCS-2 symbols
	uint16_t LFN_FstClsZero;	 // 0x1A; First cluster of record, should be 0.
	uint16_t LFN_name_part3[2];	 // 0x1C; Third, 2 UCS-2 symbols

	static constexpr size_t LFN_name_part1_size = sizeof(LFN_name_part1) / sizeof(LFN_name_part1[0]);
	static constexpr size_t LFN_name_part2_size = sizeof(LFN_name_part2) / sizeof(LFN_name_part2[0]);
	static constexpr size_t LFN_name_part3_size = sizeof(LFN_name_part3) / sizeof(LFN_name_part3[0]);
	static constexpr size_t LFN_name_part_size = LFN_name_part1_size + LFN_name_part2_size + LFN_name_part3_size;

	static uint8_t LFN_checksum(const char* DIR_Name)
	{
		unsigned char sum = 0;
		for (int i = 11; i != 0; --i)
			sum = ((sum & 1) << 7) + (sum >> 1) + *DIR_Name++;
		return sum;
	}

	bool is_LFN_record_valid() const {
		return DIR_Attr.is_longname_part() && LFN_FstClsZero == 0;
	}

	bool is_first_LFN() const {
		return LFN_index & (1 << 6);
	}

	uint8_t get_LFN_index() const {
		return LFN_index & (1 << 6);
	}

	static constexpr char non_valid_chars_LFN[] = R"("*/:<>?\|)";
	static int is_valid_char_LFN(char mychar)
	{
		return !((mychar >= '\x00') && (mychar < '\x20')) && // Space allowed 
			strchr(non_valid_chars_LFN, mychar) == nullptr;
	}

	//! TODO: no space character at the start or end, and no period at the end.
	template<typename T>
	uint32_t dir_LFN_entry_to_ASCII_str(T& name) const {
		wchar_t ucs16_record[LFN_name_part_size] = { 0 };
		char    local_record[LFN_name_part_size + 1] = { 0 };
		uint32_t idx = 0;
		for (uint32_t i = 0; i < LFN_name_part1_size; ++i, ++idx)
			ucs16_record[idx] = LFN_name_part1[i];
		for (uint32_t i = 0; i < LFN_name_part2_size; ++i, ++idx)
			ucs16_record[idx] = LFN_name_part2[i];
		for (uint32_t i = 0; i < LFN_name_part3_size; ++i, ++idx)
			ucs16_record[idx] = LFN_name_part3[i];
		idx = 0;
		int wrong_symbol = 0;
		for (idx = 0; idx < LFN_name_part_size; ++idx) {
			if (ucs16_record[idx] == 0)
				break;
			if (ucs16_record[idx] == 0xFFFF) {
				wrong_symbol = 1;
				break;
			}		
		}
		auto res = ucs16_to_local(local_record, ucs16_record, idx); //-V106
		if (res != 0)
			return res;
		else {
			name.push_back(local_record);
			return wrong_symbol;
		}
	}

#undef UNCOPYMACRO
};
#pragma pack(pop)

inline VFAT_LFN_dir_entry_t* as_LFN_record(FATxx_dir_entry_t* dir_entry) {
	return reinterpret_cast<VFAT_LFN_dir_entry_t*>(dir_entry); // In theory -- UB, but in practice -- should work.
}

inline const VFAT_LFN_dir_entry_t* as_LFN_record(const FATxx_dir_entry_t* dir_entry) {
	return reinterpret_cast<const VFAT_LFN_dir_entry_t*>(dir_entry); // In theory -- UB, but in practice -- should work.
}
static_assert(sizeof(VFAT_LFN_dir_entry_t) == 32, "Wrong size of VFAT_LFN_dir_entry_t"); //-V112

//---------MBR----------------------------------------------
inline uint32_t CHS_to_heads(const uint8_t* CHS) // 3 bytes
{
	return CHS[0];
}

inline uint32_t CHS_to_sectors(const uint8_t* CHS) // 3 bytes
{
	return CHS[1] & 0x3F;
}

inline uint32_t CHS_to_cylinders(const uint8_t* CHS) // 3 bytes
{
	return (CHS[1] >> 6) + CHS[2];
}

inline uint32_t CHS_to_LBA(const uint8_t* CHS, uint32_t max_heads, uint32_t max_sectors) {
	auto sectors = CHS_to_sectors(CHS);
	auto heads = CHS_to_heads(CHS);
	auto cylinders = CHS_to_cylinders(CHS);

	return (cylinders * max_heads + heads) * max_sectors + sectors - 1;
	// max_heads typically 16
	// max_sectors typically 63
}


#pragma pack(push, 1)
struct partition_entry_t {
	uint8_t	 disk_status;		// 0x00; Bit 7 -- active (bootable), old MBR: 0x00 or 0x80 only, 
								// modern (Plug and Play BIOS Specification and BIOS Boot Specification) can store
								// bootable disk ID here, so only 0x01-0x7F values are invalid.
	uint8_t  start_CHS[3];		// 0x01; CHS Address of the first absolute partition sector
								//		 cccc cccc ccss ssss hhhh hhhh 
								//		Sector:   1-63
								//      Cylinder: 0-1023
								//      Head:     0-255
	uint8_t	 type;				// 0x04; Partition type
	uint8_t  end_CHS[3];		// 0x05; CHS Address of the last absolute partition sector
	uint32_t start_LBA;			// 0x08; LBA of first absolute sector in the partition, >0
	uint32_t size_sectors;		// 0x0C; Number of sectors in partition

	bool is_MBR_disk_status_OK() const {
		if (disk_status >= 0x01 && disk_status <= 0x7F)
			return false;
		else
			return true;
	}
	bool is_start_CHS_zero() const {
		return start_CHS[0] == 0 && start_CHS[1] == 0 && start_CHS[2] == 0;
	}
	bool is_end_CHS_zero() const {
		return end_CHS[0] == 0 && end_CHS[1] == 0 && end_CHS[2] == 0;
	}
	bool is_CHSs_zero() const {
		return is_start_CHS_zero() && is_end_CHS_zero();
	}
	bool is_LBAs_zero() const {
		return start_LBA == 0 && size_sectors == 0;
	}
	uint32_t get_first_sec_by_LBA() const {
		return start_LBA;
	}
	uint32_t get_last_sec_by_LBA() const {
		return start_LBA + size_sectors - 1;
	}
	uint32_t get_first_sec_by_CHS(uint32_t max_heads, uint32_t max_sectors) const {
		return CHS_to_LBA(start_CHS, max_heads, max_sectors);
	}
	uint32_t get_last_sec_by_CHS(uint32_t max_heads, uint32_t max_sectors) const {
		return CHS_to_LBA(end_CHS, max_heads, max_sectors);
	}
	bool is_total_zero() const { // Check for empty (unused) records 
		return disk_status == 0 && type == 0 && is_CHSs_zero() && is_LBAs_zero();
	}

	//! Based on https://en.wikipedia.org/wiki/Partition_type
	bool is_extended() const {
		switch (type) {
		case 0x05: // MS extended partition with CHS addressing
			return true; 
		case 0x0F: // MS extended partition with LBA addressing
			return true; 
		case 0x85: // Linux extended partition with CHS addressing
			return true; 
		case 0xC0:
		case 0xC1:
		case 0xC4:
		case 0xC5:
		case 0xC6:
		case 0xCB: 
		case 0xCC:
		case 0xCE:
		case 0xCF: // DR DOS Secured partitions. Not tested.
			return true;
		case 0xD0:
		case 0xD1:
		case 0xD4:
		case 0xD5:
		case 0xD6: // Novel Multiuser DOS partitions. Not tested.
			return true;
		case 0x1F: //  	OS/2 Boot Manager hidden extended partition with LBA
			return true; 
		case 0x91: // FreeDOS hidden extended partition with CHS 
		case 0x9B: // FreeDOS hidden extended partition with LBA
			return true;
		default:
			return false;
		}
	}
};
static_assert(sizeof(partition_entry_t) == 16, "Wrong size of partition_entry_t");
#pragma pack(pop)



#pragma pack(push, 1)
struct MBR_t
{
	uint8_t	 bootcode_1[218];   // 0x000;
//------------------------------// Disk timestamp Windows 95B+/DOS 7.10+; or just boot code
	uint16_t zero;				// 0x0DA; zero value
	uint8_t  orig_disk_id;		// 0x0DC; 0x80-0xFF
	uint8_t  dsk_seconds;		// 0x0DD; 0-59
	uint8_t  dsk_minutes;		// 0x0DE; 0-59		
	uint8_t  dsk_hours;			// 0x0DF; 0-23
	uint8_t	 bootcode_2[216];	// 0x0E0
//------------------------------// Optional disk signature, UEFI, WinNT, Linux etc
	uint32_t disk_signature;	// 0x1B8; 
	uint16_t zero2;				// 0x01BC; Zero, can be 0x5A5A as a write-protected marker
	partition_entry_t ptable[4];// 0x01BE; 0x01CE; 0x01DE; 0x01EE
	uint16_t signature;         // 0x1FE; 0xAA55 (Little endian: signature[0] == 0x55, signature[1] == 0xAA)

	static constexpr int ptables() { return sizeof(ptable)/sizeof(ptable[0]); }
	bool is_correct_extended_record() const {
		return ptable[2].is_total_zero() && ptable[3].is_total_zero();
	}
	bool has_next_extended_record() const {
		return !ptable[1].is_total_zero();
	}
};
static_assert(sizeof(MBR_t) == 512, "Wrong size of MBR_t"); 
#pragma pack(pop)

#pragma pack(push, 1)
// GPT Partition Table Header: 
// https://wiki.osdev.org/GPT#LBA_1:_Partition_Table_Header
// https://en.wikipedia.org/wiki/GUID_Partition_Table
// All fields, except GUID -- little endian
// Note: a single partition of type EEh should be present in protective MBR
// Note: situated at the LBA1 regardless of the sector size.
constexpr char GPT_PTH_signature[8] = { 0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54 };
struct GPT_PTH_t{
	char signature[8];			// 0x00:  "EFI PART", {0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54} 
								//         or 0x5452415020494645ULL (Little endian)
	uint32_t revision;			// 0x08: Upper 2 bytes -- major, lower -- minor revision (0..99). 
								//       Currently 1.00: 00h 00h 01h 00h or 0x00'01'00'00 LE
	uint32_t header_size;		// 0x0C: Header size, often 92 bytes or 5Ch 
	uint32_t header_CRC32;		// 0x10: CRC32 of bytes header 0..91, with this byte zeroed. 
								//	     (Remark: should be header_size used for this CRC32 or fixed 92 bytes value?)
								//		 Possible implementations: https://web.archive.org/web/20190108202303/http://www.hackersdelight.org/hdcodetxt/crc.c.txt
	uint32_t reserved_0;		// 0x14: Reserved, should be 0.

	uint64_t GPT_PTH_LBA;		// 0x18: LBA of this header. Often 0x01.
	uint64_t alt_GPT_PTH_LBA;	// 0x20: LBA of alternate GPT header. Possibly -- last LBA.
	uint64_t first_usable_LBA;	// 0x28: First usable LBA, primary partition table last LBA + 1.
	uint64_t last_usable_LBA;	// 0x30: Last  usable LBA, should be alternative partition table first LBA - 1.

	uint8_t  disk_GUID[16];		// 0x38: Unique disk ID -- GUID, mixed endian: uint32_t, uint16_t[2], uint8_t[8].
	uint64_t part_entries_LBA;	// 0x48: Starting LBA of array of partition entries. Should be 2.
	uint32_t part_entries_num;  // 0x50: Number of partition entries in array.
	uint32_t part_entry_size;   // 0x54: Size of a single partition entry. 
								//       Should be 128 x 2^n, often just 128 bytes (0x80). 
	uint32_t part_entries_CRC32;// 0x58: CRC32 of partition entries array.
	
	uint8_t padding[420];		// 0x5C: padding, should be 0. Sector size at last 512 bytes, but expect it could be larger. 
};

//! Do not expect sector size equal to 512, but I hope it should be no less.
static_assert(sizeof(GPT_PTH_t) == 512, "Wrong size of GPT_PTH_t");
#pragma pack(pop)

//! Links: 
//! https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system
//! https://en.wikipedia.org/wiki/Master_boot_record
//! https://en.wikipedia.org/wiki/Extended_boot_record
//! https://wiki.osdev.org/ExFAT -- contains CRC calculations code 
//! https://wiki.osdev.org/FAT
//! See also: https://en.wikipedia.org/wiki/Filename#Comparison_of_filename_limitations
//! 
//! Interestin or important formats notes:
// 1. XDF images seem to be interpreted correctly. Additionally: http://www.os2museum.com/wp/the-xdf-diskette-format/
// 2. http://ucsd-psystem-fs.sourceforge.net/ http://ucsd-psystem-fs.sourceforge.net/ucsd-psystem-fs-1.22.pdf 
// 3. CP/M FS formats: https://www.seasip.info/Cpm/formats.html
//    https://forums.debian.net//viewtopic.php?f=16&t=112244 -- cpmtools howto 
//    https://github.com/lipro-cpm4l/cpmtools
// 4. 86-DOS FAT variants: https://en.wikipedia.org/wiki/86-DOS#Formats
// 5. UMSDOS: --LINUX-.--- files format: https://gist.github.com/chungy/7852622, http://linux.voyager.hr/umsdos/
// 6. OS/2 Extended attributes for FAT: http://www.tavi.co.uk/os2pages/eadata.html -- some info about EA on-disk format, 
//	  API details:
//	  http://www.naspa.net/magazine/1996/July/T9607014.PDF , 
//	  http://www.edm2.com/index.php/Extended_Attributes_-_what_are_they_and_how_can_you_use_them_%3F
//    http://www.edm2.com/index.php/Encapsulating_Extended_Attributes_-_Part_1/2
//    http://www.edm2.com/index.php/Encapsulating_Extended_Attributes_-_Part_2/2
// 7. http://www.emuverse.ru/wiki/Teledisk , https://hwiegman.home.xs4all.nl/fileformats/teledisk/wteledsk.htm
// 8. https://github.com/lipro-cpm4l/libdsk -- велика бібліотека для роботи із різними образами
// 
// See also https://github.com/aaru-dps/Aaru -- Aaru Data Preservation Suite
#endif 

