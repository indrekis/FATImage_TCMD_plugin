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
	uint32_t dir_entry_name_to_str(T& name);

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

