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
#include "sysui_winapi.h"
#include "minimal_fixed_string.h"
#include "FAT_definitions.h"
#include "plugin_config.h"

#include "wcxhead.h"
#include <new>
#include <memory>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <optional>
#include <map>
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


bool set_file_attributes_ex(const char* filename, FAT_attrib_t attribute) {
	/*
	DWORD winattr = FILE_ATTRIBUTE_NORMAL;
	if (attribute.is_readonly()) winattr |= FILE_ATTRIBUTE_READONLY;
	if (attribute.is_archive() ) winattr |= FILE_ATTRIBUTE_ARCHIVE;
	if (attribute.is_hidden()  ) winattr |= FILE_ATTRIBUTE_HIDDEN;
	if (attribute.is_system()  ) winattr |= FILE_ATTRIBUTE_SYSTEM;
	*/
	return set_file_attributes(filename, attribute.get_user_attr()); // Codes are equal
}

struct arc_dir_entry_t
{
	minimal_fixed_string_t<MAX_PATH> PathName;
	size_t FileSize;
	uint32_t FileTime;
	FAT_attrib_t FileAttr;
	uint32_t FirstClus;
};

plugin_config_t plugin_config; 

using on_bad_BPB_callback_t = int(*)(void*, int openmode);

struct FAT_image_t
{
	enum FAT_types { unknow_type, FAT12_type, FAT16_type, FAT32_type, exFAT_type, FAT_DOS100_type, FAT_DOS110_type};

	static constexpr size_t sector_size = 512;
	on_bad_BPB_callback_t on_bad_BPB_callback = nullptr;
	int openmode_m = PK_OM_LIST;
	file_handle_t hArchFile = file_handle_t();    //opened file handle
	size_t archive_file_size = 0;	// TODO: rename to volume_size
	size_t boot_sector_offset = 0;

	FAT_types FAT_type = unknow_type;
	bool has_OS2_EA = false;

	std::vector<uint8_t> fattable;
	std::vector<arc_dir_entry_t> arc_dir_entries;
	FAT_boot_sector_t bootsec{};

	size_t FAT1area_off_m = 0; //number of uint8_t before first FAT area 
	size_t rootarea_off_m = 0; //number of uint8_t before root area 
	size_t dataarea_off_m = 0; //number of uint8_t before data area
	uint32_t cluster_size_m = 0;
	uint32_t counter = 0;

	FAT_image_t(const FAT_image_t&) = default;
	FAT_image_t& operator=(const FAT_image_t&) = default;

	FAT_image_t(on_bad_BPB_callback_t clb, size_t vol_size, file_handle_t fh, int openmode) :
		archive_file_size(vol_size), on_bad_BPB_callback(clb), openmode_m(openmode), hArchFile(fh)
	{		
	}

	// File is closed by the MBR archive -- whole_disk_t
	~FAT_image_t() = default; 

	void set_boot_sector_offset(size_t off) {
		boot_sector_offset = off;
	}

	// TODO: boot_sector_offset + ...
	size_t cluster_to_image_off(uint32_t cluster) {
		return get_data_area_offset() + static_cast<size_t>(cluster - 2) * get_cluster_size(); //-V104
	}

	uint32_t get_sectors_per_FAT() const {
		if (bootsec.BPB_SectorsPerFAT != 0) {
			return bootsec.BPB_SectorsPerFAT;
		}
		else { // Possibly -- FAT32
			return bootsec.EBPB_FAT32.BS_SectorsPerFAT32;
		}
	}

	size_t get_bytes_per_FAT() const {
		return get_sectors_per_FAT() * sector_size; //-V104
	}

	uint32_t get_root_dir_entry_count() const {
		// MS-DOS supports 240 max for FDD and 512 for HDD
		return bootsec.BPB_RootEntCnt;
	}

	size_t get_root_dir_size() const {
		return get_data_area_offset() - get_root_area_offset(); //-V110
	}

	size_t get_FAT1_area_offset() const {
		return FAT1area_off_m;
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

	uint64_t get_total_sectors_in_volume() const;
	uint64_t get_data_sectors_in_volume() const;
	uint64_t get_data_clusters_in_volume() const;

	int process_bootsector(bool read_bootsec);
	int process_DOS1xx_image();

	int search_for_bootsector();

	int load_FAT();

	FAT_types detect_FAT_type() const;

	int extract_to_file(file_handle_t hUnpFile, uint32_t idx);

	// root passed by copy to avoid problems while relocating vector
	int load_file_list_recursively(minimal_fixed_string_t<MAX_PATH> root, uint32_t firstclus, uint32_t depth); //-V813

	uint32_t get_first_cluster(const FATxx_dir_entry_t& dir_entry) const;

	uint32_t next_cluster_FAT12(uint32_t firstclus) const;
	uint32_t next_cluster_FAT16(uint32_t firstclus) const;
	uint32_t next_cluster_FAT32(uint32_t firstclus) const;
	uint32_t next_cluster_FAT(uint32_t firstclus) const;

	uint32_t max_cluster_FAT() const;
	uint32_t max_normal_cluster_FAT() const;
	uint32_t min_end_of_chain_FAT() const;
	bool is_end_of_chain_FAT(uint32_t) const;

	struct LFN_accumulator_t {
		minimal_fixed_string_t<MAX_PATH> cur_LFN_name{};
		uint8_t cur_LFN_CRC = 0;
		int cur_LFN_record_index = 0;
		void start_processing(VFAT_LFN_dir_entry_t* LFN_record) {
			cur_LFN_CRC = LFN_record->LFN_DOS_name_CRC;
			cur_LFN_record_index = 1;
			cur_LFN_name.clear();
			LFN_record->dir_LFN_entry_to_ASCII_str(cur_LFN_name);
		}
		void append_LFN_part(VFAT_LFN_dir_entry_t* LFN_record) {
			++cur_LFN_record_index;
			minimal_fixed_string_t<MAX_PATH> next_LFN_part;
			LFN_record->dir_LFN_entry_to_ASCII_str(next_LFN_part);
			next_LFN_part.push_back(cur_LFN_name);
			cur_LFN_name = next_LFN_part;
		}
		void abort_processing() {
			cur_LFN_CRC = 0;
			cur_LFN_record_index = 0;
			cur_LFN_name.clear();
		}
		bool are_processing() {
			return cur_LFN_record_index > 0;
		}
	};
};

struct partition_info_t {
	uint32_t first_sector = 0;
	uint32_t last_sector = 0;

};

struct whole_disk_t {
	static constexpr size_t sector_size = 512; // Duplicated in FAT_image_t for the future support of 
											   // non-partitioned disks with other sizes of sectors
	minimal_fixed_string_t<MAX_PATH> archname; // Should be saved for the TCmd API
	file_handle_t hArchFile = file_handle_t();    //opened file handles

	tChangeVolProc   pLocChangeVol = nullptr;
	tProcessDataProc pLocProcessData = nullptr;

	whole_disk_t(on_bad_BPB_callback_t clb, const char* archname_in, size_t vol_size, file_handle_t fh, int openmode):
		hArchFile{ fh }
	{
		archname.push_back(archname_in);
		// First disk represents also non-partitioned image -- so, initially, it's size = whole image size.
		disks.emplace_back(clb, vol_size, fh, openmode);
	}

	// Note that disks, partition_info, mbrs are not synchronized. 
	// disks contains only known (FAT) volumes, partition_info -- only volumes (no extended partitions)
	// and mbrs -- MBR and all EBRs.
	std::vector<FAT_image_t>		disks;
	std::vector<MBR_t>				mbrs{ 1 };
	std::vector<partition_info_t>	partition_info;
	size_t disc_counter = 0;

	int detect_MBR();
	int process_MBR();

	static minimal_fixed_string_t<MAX_PATH> get_disk_prefix(uint32_t idx) { 
		minimal_fixed_string_t<MAX_PATH> res{ "C\\" };
		if (idx > 'Z' - 'C') { // Quick and dirty
			res[1] = 'P';
			while (idx != 0) {
				res.push_back( static_cast<char>(idx % 10 + '0') );
				idx /= 10;
			}
			res.push_back('\\');
		}
		else {
			res[0] += idx;
		}
		return res; 
	}

	~whole_disk_t() {
		if (hArchFile)
			close_file(hArchFile);
	}
};

//------- archive_t implementation -----------------------------

int FAT_image_t::process_bootsector(bool read_bootsec) {
	if(read_bootsec){
		set_file_pointer(hArchFile, boot_sector_offset);
		auto result = read_file(hArchFile, &bootsec, sector_size);
		if (result != sector_size) {
			return E_EREAD;
		}
	}
	// if read_bootsec == false -- user preread bootsector

	if (bootsec.BPB_bytesPerSec != sector_size) {
		// TODO: Some formats, like DMF, use other sizes
		return E_UNKNOWN_FORMAT;
	}

	if (bootsec.signature != 0xAA55 && !plugin_config.ignore_boot_signature) {
		int res = on_bad_BPB_callback(this, openmode_m);
		if (res != 0) {
			return res;
		}
	}

	cluster_size_m = sector_size * bootsec.BPB_SecPerClus;
	FAT1area_off_m = sector_size * bootsec.BPB_RsvdSecCnt;
	rootarea_off_m = sector_size * (bootsec.BPB_RsvdSecCnt +
		get_sectors_per_FAT() * static_cast<size_t>(bootsec.BPB_NumFATs)); //-V104
	dataarea_off_m = get_root_area_offset() + get_root_dir_entry_count() * sizeof(FATxx_dir_entry_t);

	FAT_type = detect_FAT_type();
	switch (FAT_type) {
	case FAT12_type:
		if ((get_sectors_per_FAT() < 1) || (get_sectors_per_FAT() > 12)) {
			return E_UNKNOWN_FORMAT;
		}
		break;
	case FAT16_type:
		if ((get_sectors_per_FAT() < 1) || (get_sectors_per_FAT() > 256)) { // get_sectors_per_FAT() < 16 according to standard
			return E_UNKNOWN_FORMAT;		
		}
		break;
	case FAT32_type:
		if ((get_sectors_per_FAT() < 1) || (get_sectors_per_FAT() > 2'097'152)) { // get_sectors_per_FAT() < 512 according to standard
			return E_UNKNOWN_FORMAT;
		}
		// There should not be those values in correct FAT32
		if (bootsec.EBPB_FAT.BS_BootSig == 0x29 || bootsec.EBPB_FAT.BS_BootSig == 0x28) {
			return E_UNKNOWN_FORMAT;
		}
		// TODO: Support other active FAT tables:
		if (!(bootsec.EBPB_FAT32.is_FAT_mirrored() || bootsec.EBPB_FAT32.get_active_FAT() == 0)) {
			return E_UNKNOWN_FORMAT; // Not yet implemented;
		}		
		break;
	case unknow_type:
		return E_UNKNOWN_FORMAT;
	default:
		//! Here also unsupported (yet) formats like exFAT
		return E_UNKNOWN_FORMAT;
	}

	return 0;
}

//! Used for reading DOS 1.xx images as a bootsector template. Taken from the IBM PC-DOS 2.00 (5.25) Disk01.img
static uint8_t raw_DOS200_bootsector[] = {
		0xEB, 0x2C, 0x90, 0x49, 0x42, 0x4D, 0x20, 0x20, 0x32, 0x2E, 0x30, 0x00,
		0x02, 0x01, 0x01, 0x00, 0x02, 0x40, 0x00, 0x68, 0x01, 0xFC, 0x02, 0x00,
		0x09, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0xDF, 0x02, 0x25,
		0x02, 0x09, 0x2A, 0xFF, 0x50, 0xF6, 0x00, 0x02, 0xCD, 0x19, 0xFA, 0x33,
		0xC0, 0x8E, 0xD0, 0xBC, 0x00, 0x7C, 0x8E, 0xD8, 0xA3, 0x7A, 0x00, 0xC7,
		0x06, 0x78, 0x00, 0x21, 0x7C, 0xFB, 0xCD, 0x13, 0x73, 0x03, 0xE9, 0x95,
		0x00, 0x0E, 0x1F, 0xA0, 0x10, 0x7C, 0x98, 0xF7, 0x26, 0x16, 0x7C, 0x03,
		0x06, 0x1C, 0x7C, 0x03, 0x06, 0x0E, 0x7C, 0xA3, 0x03, 0x7C, 0xA3, 0x13,
		0x7C, 0xB8, 0x20, 0x00, 0xF7, 0x26, 0x11, 0x7C, 0x05, 0xFF, 0x01, 0xBB,
		0x00, 0x02, 0xF7, 0xF3, 0x01, 0x06, 0x13, 0x7C, 0xE8, 0x7E, 0x00, 0x72,
		0xB3, 0xA1, 0x13, 0x7C, 0xA3, 0x7E, 0x7D, 0xB8, 0x70, 0x00, 0x8E, 0xC0,
		0x8E, 0xD8, 0xBB, 0x00, 0x00, 0x2E, 0xA1, 0x13, 0x7C, 0xE8, 0xB6, 0x00,
		0x2E, 0xA0, 0x18, 0x7C, 0x2E, 0x2A, 0x06, 0x15, 0x7C, 0xFE, 0xC0, 0x32,
		0xE4, 0x50, 0xB4, 0x02, 0xE8, 0xC1, 0x00, 0x58, 0x72, 0x38, 0x2E, 0x28,
		0x06, 0x20, 0x7C, 0x76, 0x0E, 0x2E, 0x01, 0x06, 0x13, 0x7C, 0x2E, 0xF7,
		0x26, 0x0B, 0x7C, 0x03, 0xD8, 0xEB, 0xCE, 0x0E, 0x1F, 0xCD, 0x11, 0xD0,
		0xC0, 0xD0, 0xC0, 0x25, 0x03, 0x00, 0x75, 0x01, 0x40, 0x40, 0x8B, 0xC8,
		0xF6, 0x06, 0x1E, 0x7C, 0x80, 0x75, 0x02, 0x33, 0xC0, 0x8B, 0x1E, 0x7E,
		0x7D, 0xEA, 0x00, 0x00, 0x70, 0x00, 0xBE, 0xC9, 0x7D, 0xE8, 0x02, 0x00,
		0xEB, 0xFE, 0x2E, 0xAC, 0x24, 0x7F, 0x74, 0x4D, 0xB4, 0x0E, 0xBB, 0x07,
		0x00, 0xCD, 0x10, 0xEB, 0xF1, 0xB8, 0x50, 0x00, 0x8E, 0xC0, 0x0E, 0x1F,
		0x2E, 0xA1, 0x03, 0x7C, 0xE8, 0x43, 0x00, 0xBB, 0x00, 0x00, 0xB8, 0x01,
		0x02, 0xE8, 0x58, 0x00, 0x72, 0x2C, 0x33, 0xFF, 0xB9, 0x0B, 0x00, 0x26,
		0x80, 0x0D, 0x20, 0x26, 0x80, 0x4D, 0x20, 0x20, 0x47, 0xE2, 0xF4, 0x33,
		0xFF, 0xBE, 0xDF, 0x7D, 0xB9, 0x0B, 0x00, 0xFC, 0xF3, 0xA6, 0x75, 0x0E,
		0xBF, 0x20, 0x00, 0xBE, 0xEB, 0x7D, 0xB9, 0x0B, 0x00, 0xF3, 0xA6, 0x75,
		0x01, 0xC3, 0xBE, 0x80, 0x7D, 0xE8, 0xA6, 0xFF, 0xB4, 0x00, 0xCD, 0x16,
		0xF9, 0xC3, 0x1E, 0x0E, 0x1F, 0x33, 0xD2, 0xF7, 0x36, 0x18, 0x7C, 0xFE,
		0xC2, 0x88, 0x16, 0x15, 0x7C, 0x33, 0xD2, 0xF7, 0x36, 0x1A, 0x7C, 0x88,
		0x16, 0x1F, 0x7C, 0xA3, 0x08, 0x7C, 0x1F, 0xC3, 0x2E, 0x8B, 0x16, 0x08,
		0x7C, 0xB1, 0x06, 0xD2, 0xE6, 0x2E, 0x0A, 0x36, 0x15, 0x7C, 0x8B, 0xCA,
		0x86, 0xE9, 0x2E, 0x8B, 0x16, 0x1E, 0x7C, 0xCD, 0x13, 0xC3, 0x00, 0x00,
		0x0D, 0x0A, 0x4E, 0x6F, 0x6E, 0x2D, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6D,
		0x20, 0x64, 0x69, 0x73, 0x6B, 0x20, 0x6F, 0x72, 0x20, 0x64, 0x69, 0x73,
		0x6B, 0x20, 0x65, 0x72, 0x72, 0x6F, 0x72, 0x0D, 0x0A, 0x52, 0x65, 0x70,
		0x6C, 0x61, 0x63, 0x65, 0x20, 0x61, 0x6E, 0x64, 0x20, 0x73, 0x74, 0x72,
		0x69, 0x6B, 0x65, 0x20, 0x61, 0x6E, 0x79, 0x20, 0x6B, 0x65, 0x79, 0x20,
		0x77, 0x68, 0x65, 0x6E, 0x20, 0x72, 0x65, 0x61, 0x64, 0x79, 0x0D, 0x0A,
		0x00, 0x0D, 0x0A, 0x44, 0x69, 0x73, 0x6B, 0x20, 0x42, 0x6F, 0x6F, 0x74,
		0x20, 0x66, 0x61, 0x69, 0x6C, 0x75, 0x72, 0x65, 0x0D, 0x0A, 0x00, 0x69,
		0x62, 0x6D, 0x62, 0x69, 0x6F, 0x20, 0x20, 0x63, 0x6F, 0x6D, 0x30, 0x69,
		0x62, 0x6D, 0x64, 0x6F, 0x73, 0x20, 0x20, 0x63, 0x6F, 0x6D, 0x30, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0xAA
	};

int FAT_image_t::process_DOS1xx_image() {
	//! DOS 1.xx disks do not contain BPB so any detection would be a heuristic. 
	//! But the limited number of image types helps a little
	//! So we test media byte and image size. If it looks OK -- recreate BPB and 
	//! call process_bootsector().
	//! 8" disks are not yes supported -- they need 128 bytes sectors, 
	//! though there are no fundamental problems.
	//! One possible source of problems, citing wiki: "Microsoft recommends to distinguish between 
	//! the two 8-inch formats for FAT ID 0xFE by trying to read of a single-density address mark. 
	//! If this results in an error, the medium must be double-density."
	//! https://jeffpar.github.io/kbarchive/kb/075/Q75131/ -- "Q75131: Standard Floppy Disk Formats Supported by MS-DOS"
	//! 
	//! Examples of 8" images: https://winworldpc.com/product/ms-dos/1x and	https://winworldpc.com/product/86-dos/100
	//! https://en.wikipedia.org/wiki/Comparison_of_DOS_operating_systems -- 8" FDD for DOS 1.xx formats
	//! DOS 1.10 boot sector: https://thestarman.pcministry.com/DOS/ibm110/Boot.htm
	
	uint8_t media_descr = 0;

	auto res = set_file_pointer(hArchFile, boot_sector_offset + sector_size);
	if (!res) {
		return E_EREAD;
	}
	auto rdres = read_file(hArchFile, &media_descr, 1);
	if (rdres != 1) {
		return E_EREAD;
	}
	memcpy_s(&bootsec, sector_size, raw_DOS200_bootsector, sizeof(raw_DOS200_bootsector));
	switch (media_descr) {
	case 0xFE: // 5.25" 160Kb/163'840b img; H:C:S = 1:40:8, DOS 1.00
		if (archive_file_size != 160 * 1024) {
			return E_UNKNOWN_FORMAT; 
		}
		bootsec.BPB_SecPerClus = 1;
		bootsec.BPB_RsvdSecCnt = 1;
		bootsec.BPB_NumFATs	   = 2;
		bootsec.BPB_RootEntCnt = (sector_size/sizeof(FATxx_dir_entry_t))*4; // 4 sectors in root dir
		bootsec.BPB_TotSec16   = 320;
		bootsec.BPB_MediaDescr = 0xFE;
		bootsec.BPB_SectorsPerFAT = 1;
		// Free sectors = 313
		// Hidden sectors = 0
		break;
	case 0xFC: // 5.25" 180Kb/184'320b img; H:C:S = 1:40:9, DOS 2.00
		if (archive_file_size != 180 * 1024) {
			return E_UNKNOWN_FORMAT;
		}
		bootsec.BPB_SecPerClus = 1;
		bootsec.BPB_RsvdSecCnt = 1;
		bootsec.BPB_NumFATs    = 2;
		bootsec.BPB_RootEntCnt = (sector_size / sizeof(FATxx_dir_entry_t)) * 4; // 4 sectors in root dir
		bootsec.BPB_TotSec16   = 360;
		bootsec.BPB_MediaDescr = 0xFC;
		bootsec.BPB_SectorsPerFAT = 2;
		// Free sectors = 351
		// Hidden sectors = 0
		break;
	case 0xFF: // 5.25" 320Kb/327'680b img; H:C:S = 2:40:8, DOS 1.10
		if (archive_file_size != 320 * 1024) {
			if (plugin_config.process_DOS1xx_exceptions) {  // 331792
				if (archive_file_size != 331'792) {
					//! Exception for the "MS-DOS 1.12.ver.1.12 OEM [Compaq]" image, containing 
					//! 4112 bytes at the end, bracketed by "Skip  8 blocks " text.
					return E_UNKNOWN_FORMAT;
				} 
			}
			else {
				return E_UNKNOWN_FORMAT;
			}
			
		}
		bootsec.BPB_SecPerClus = 2;
		bootsec.BPB_RsvdSecCnt = 1;
		bootsec.BPB_NumFATs    = 2;
		bootsec.BPB_RootEntCnt = (sector_size / sizeof(FATxx_dir_entry_t)) * 7; // 7 sectors in root dir
		bootsec.BPB_TotSec16   = 640;
		bootsec.BPB_MediaDescr = 0xFF;
		bootsec.BPB_SectorsPerFAT = 1;
		// Free sectors = 630
		// Hidden sectors = 0
		break;
	case 0xFD: // 5.25" 360Kb/368'640b img; H:C:S = 2:40:8, DOS 1.10
		if (archive_file_size != 360 * 1024) {
			return E_UNKNOWN_FORMAT;
		}
		bootsec.BPB_SecPerClus = 2;
		bootsec.BPB_RsvdSecCnt = 1;
		bootsec.BPB_NumFATs    = 2;
		bootsec.BPB_RootEntCnt = (sector_size / sizeof(FATxx_dir_entry_t)) * 7; // 7 sectors in root dir
		bootsec.BPB_TotSec16   = 720;
		bootsec.BPB_MediaDescr = 0xFD;
		bootsec.BPB_SectorsPerFAT = 2;
		// Free sectors = 708
		// Hidden sectors = 0
		break;
	default:
		return E_UNKNOWN_FORMAT;
	}
	return process_bootsector(false);
}

int FAT_image_t::load_FAT() {
	const size_t fat_size_bytes = get_bytes_per_FAT();
	try {
		fattable.reserve(fat_size_bytes); // To minimize overcommit
		fattable.resize(fat_size_bytes);
	}
	catch (std::exception&) { // std::length_error, std::bad_alloc, other can be used by custom allocators
		return E_NO_MEMORY;
	}
	// Read FAT table
	set_file_pointer(hArchFile, boot_sector_offset + get_FAT1_area_offset());
	auto result = read_file(hArchFile, fattable.data(), fat_size_bytes);
	if (result != fat_size_bytes)
	{
		return E_EREAD;
	}
	return 0;
}

uint64_t FAT_image_t::get_total_sectors_in_volume() const {
	uint64_t sectors = 0;
	if (bootsec.BPB_TotSec16 != 0) {
		sectors = bootsec.BPB_TotSec16;
		if (bootsec.EBPB_FAT.BPB_TotSec32 != 0 && bootsec.BPB_TotSec16 != bootsec.EBPB_FAT.BPB_TotSec32) {
			// TODO: inconsistent 
		}
	}
	else if (bootsec.EBPB_FAT.BPB_TotSec32 != 0) {
		sectors = bootsec.EBPB_FAT.BPB_TotSec32;
	}
	else if (bootsec.EBPB_FAT32.BS_BootSig == 0x29) {
		// Some non-standard systems
		const uint64_t* BS_TotSec64 = reinterpret_cast<const uint64_t*>(bootsec.EBPB_FAT32.BS_FilSysType);
		sectors = *BS_TotSec64;
	}
	return sectors;
}

uint64_t FAT_image_t::get_data_sectors_in_volume() const {
	return get_total_sectors_in_volume() - get_data_area_offset() / bootsec.BPB_bytesPerSec;
}

uint64_t FAT_image_t::get_data_clusters_in_volume() const {
	return get_data_sectors_in_volume() / bootsec.BPB_SecPerClus;
}

FAT_image_t::FAT_types FAT_image_t::detect_FAT_type() const {
// See http://jdebp.info/FGA/determining-fat-widths.html
	auto sectors = bootsec.BPB_bytesPerSec;
	if(sectors == 0){
		return FAT_image_t::exFAT_type;
	}
	auto clusters = get_data_clusters_in_volume();
	if (strncmp(bootsec.EBPB_FAT.BS_FilSysType, "FAT12   ", 8) == 0) {
		if (clusters > 0x0FF6) {
			// TODO: inconsistent 
			return FAT_image_t::unknow_type;
		}
		return FAT_image_t::FAT12_type;
	}
	if (strncmp(bootsec.EBPB_FAT.BS_FilSysType, "FAT16   ", 8) == 0) {
		if (clusters > 0x0FFF6) {
			// TODO: inconsistent
			return FAT_image_t::unknow_type;
		}
		return FAT_image_t::FAT16_type;
	}
	if (strncmp(bootsec.EBPB_FAT.BS_FilSysType, "FAT32   ", 8) == 0 || // Could contain it
		strncmp(bootsec.EBPB_FAT32.BS_FilSysType, "FAT32   ", 8) == 0
		) {
		return FAT_image_t::FAT32_type;
	}

	if (clusters >= 0x00000002 && clusters <= 0x00000FF6) { // 2–4086
		return FAT_image_t::FAT12_type;
	} 

	//! TODO: Possible small FAT32 discs without BS_FilSysType could be misdetected.
	if (clusters >= 0x00000FF7 && clusters <= 0x0000FFF6) { // 4087–65526
		return FAT_image_t::FAT16_type;
	}

	if (clusters >= 0x0000FFF7 && clusters <= 0x0FFFFFF6) { // 65527–268435446
		return FAT_image_t::FAT32_type;
	}
	return FAT_image_t::unknow_type; // Unknown format
}

int FAT_image_t::extract_to_file(file_handle_t hUnpFile, uint32_t idx) {
	try { // For bad allocation
		const auto& cur_entry = arc_dir_entries[idx];
		uint32_t nextclus = cur_entry.FirstClus;
		size_t remaining = cur_entry.FileSize;
		std::vector<char> buff(get_cluster_size());
		while (remaining > 0)
		{
			if ( (nextclus <= 1) || (nextclus >= min_end_of_chain_FAT()) )
			{
				close_file(hUnpFile);
				return E_UNKNOWN_FORMAT;
			}
			set_file_pointer(hArchFile, boot_sector_offset + cluster_to_image_off(nextclus));
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

			nextclus = next_cluster_FAT(nextclus);
		}
		return 0;
	}
	catch (std::bad_alloc&) {
		return E_NO_MEMORY;
	}
}

// root passed by copy to avoid problems while relocating vector
int FAT_image_t::load_file_list_recursively(minimal_fixed_string_t<MAX_PATH> root, uint32_t firstclus, uint32_t depth) //-V813
{
	if (root.is_empty()) { // Initial reading
		counter = 0;
		arc_dir_entries.clear();
	}

	if (firstclus == 0 && FAT_type == FAT32_type) {
		// For exotic implementations, if BS_RootFirstClus == 0, will behave as expected
		firstclus = bootsec.EBPB_FAT32.BS_RootFirstClus; 
	}

	size_t portion_size = 0;
	if (firstclus == 0)
	{   // Read whole FAT12/16 dir at once
		set_file_pointer(hArchFile, boot_sector_offset + get_root_area_offset());
		portion_size = get_root_dir_size();
	}
	else {
		set_file_pointer(hArchFile, boot_sector_offset + cluster_to_image_off(firstclus));
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

	if (firstclus >= max_normal_cluster_FAT()) {
		// TODO: Anomaly
	}
	if ( (firstclus == 1) || (firstclus >= max_cluster_FAT()) ) {
		return E_UNKNOWN_FORMAT;
	}
	size_t result = read_file(hArchFile, sector.get(), portion_size);
	if (result != portion_size) {
		return E_EREAD;
	} 

	LFN_accumulator_t current_LFN;
	do {
		size_t entry_in_cluster = 0;
		while ((entry_in_cluster < records_number) && (!sector[entry_in_cluster].is_dir_record_free()))
		{
			if (sector[entry_in_cluster].is_dir_record_longname_part()) {
				if (plugin_config.use_VFAT) {
					auto LFN_record = as_LFN_record(&sector[entry_in_cluster]);
					if (!current_LFN.are_processing()) {
						if (!LFN_record->is_LFN_record_valid() || !LFN_record->is_first_LFN()) {
							entry_in_cluster++;
							continue;
						}
						current_LFN.start_processing(LFN_record);
					}
					else {
						if (!LFN_record->is_LFN_record_valid()) {
							current_LFN.abort_processing();
						} else if (LFN_record->is_first_LFN()) { // Should restart processing 
							current_LFN.start_processing(LFN_record);
						}
						else if (current_LFN.cur_LFN_CRC != LFN_record->LFN_DOS_name_CRC) {
							current_LFN.abort_processing();
						}
						else {
							current_LFN.append_LFN_part(LFN_record);
						}
					}
				}
				entry_in_cluster++;
				continue;				
			}
			if (sector[entry_in_cluster].is_dir_record_deleted() ||
				sector[entry_in_cluster].is_dir_record_unknown() ||
				sector[entry_in_cluster].is_dir_record_volumeID() ||
				sector[entry_in_cluster].is_dir_record_invalid_attr()
				)
			{
				if(current_LFN.are_processing())
					current_LFN.abort_processing();
				entry_in_cluster++;
				continue;
			}
		
			arc_dir_entries.emplace_back();
			auto& newentryref = arc_dir_entries.back();
			newentryref.FileAttr = sector[entry_in_cluster].DIR_Attr;
			// TODO: errors handling
			newentryref.PathName.push_back(root); // Empty root is OK, "\\" OK too
			if (plugin_config.use_VFAT && current_LFN.are_processing()) {
				if (current_LFN.cur_LFN_CRC == VFAT_LFN_dir_entry_t::LFN_checksum(sector[entry_in_cluster].DIR_Name)) {
					newentryref.PathName.push_back(current_LFN.cur_LFN_name);
				}
				else {
					auto invalid_chars = sector[entry_in_cluster].dir_entry_name_to_str(newentryref.PathName);
					// No OS/2 EA on FAT32
				}
				current_LFN.abort_processing();
			}
			else {
				auto invalid_chars = sector[entry_in_cluster].dir_entry_name_to_str(newentryref.PathName);
				if (invalid_chars == FATxx_dir_entry_t::LLDE_OS2_EA) {
					has_OS2_EA = true;
				}
			}

			newentryref.FileTime = sector[entry_in_cluster].get_file_datetime();
			newentryref.FileSize = sector[entry_in_cluster].DIR_FileSize;
			newentryref.FirstClus = get_first_cluster(sector[entry_in_cluster]);

			if (sector[entry_in_cluster].is_dir_record_dir()) {
				newentryref.PathName.push_back('\\'); // Neccessery for empty dirs to be "enterable"
			}
			if (sector[entry_in_cluster].is_dir_record_dir() &&
				(newentryref.FirstClus < max_cluster_FAT()) && (newentryref.FirstClus > 0x1)
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
			firstclus = next_cluster_FAT(firstclus);
			if ((firstclus <= 1) || (firstclus >= max_cluster_FAT())) { return 0; }
			set_file_pointer(hArchFile, boot_sector_offset + cluster_to_image_off(firstclus)); //-V104
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

uint32_t FAT_image_t::get_first_cluster(const FATxx_dir_entry_t& dir_entry) const {
	switch (FAT_type) {
	case FAT12_type:
		return dir_entry.get_first_cluster_FAT12();
		break;
	case FAT16_type:
		return dir_entry.get_first_cluster_FAT16();
		break;
	case FAT32_type:
		return dir_entry.get_first_cluster_FAT32();
		break;
	default:
		return 0;
	}
}

uint32_t FAT_image_t::next_cluster_FAT12(uint32_t firstclus) const
{
	const auto FAT_byte_pre = fattable.data() + ((firstclus * 3) >> 1); // firstclus + firstclus/2 //-V104
	//! Extract word, containing next cluster:
	const uint16_t* word_ptr = reinterpret_cast<const uint16_t*>(FAT_byte_pre);
	// Extract correct 12 bits -- lower for odd, upper for even: 
	return ((*word_ptr) >> ((firstclus % 2) ? 4 : 0)) & 0x0FFF; //-V112
}

uint32_t FAT_image_t::next_cluster_FAT16(uint32_t firstclus) const
{
	const auto FAT_byte_pre = fattable.data() + static_cast<size_t>(firstclus) * 2; 
	const uint16_t* word_ptr = reinterpret_cast<const uint16_t*>(FAT_byte_pre);
	return *word_ptr;
}

uint32_t FAT_image_t::next_cluster_FAT32(uint32_t firstclus) const
{
	const auto FAT_byte_pre = fattable.data() + static_cast<size_t>(firstclus) * 4; //-V112
	const uint32_t* word_ptr = reinterpret_cast<const uint32_t*>(FAT_byte_pre); //-V206
	return (*word_ptr) & 0x0F'FF'FF'FF; // Zero upper 4 bits
}

uint32_t FAT_image_t::next_cluster_FAT(uint32_t firstclus) const
{
	// TODO: Replace by function pointer
	switch (FAT_type) {
	case FAT12_type:
		return next_cluster_FAT12(firstclus);
		break;
	case FAT16_type:
		return next_cluster_FAT16(firstclus);
		break;
	case FAT32_type:
		return next_cluster_FAT32(firstclus);
		break;
	default:
		return 0;
	}	
}

//! Note: Normal values are: 0xFF0-1, 0xFFF0-1, 0x?FFFFFF0-1. They should be used
//! by disk software, but if values 0x0..FF0-0x0..FF6 occurs, they should be 
//! treated as a normal value. 
//! DOS 3.3+ treats 0xFF0 for FAT12 (but not FAT16 and FAT32) as a end-of-chain.
//! See also max_normal_cluster_FAT().
uint32_t FAT_image_t::max_cluster_FAT() const
{
	// TODO: Replace by array
	switch (FAT_type) {
	case FAT12_type:
		return 0xFF6;
		break;
	case FAT16_type:
		return 0xFF'F6;
		break;
	case FAT32_type:
		return 0xF'FF'FF'F6;
		break;
	default:
		return 0;
	}
}

uint32_t FAT_image_t::max_normal_cluster_FAT() const
{
	// TODO: Replace by array
	switch (FAT_type) {
	case FAT12_type:
		return 0xFF0-1;
		break;
	case FAT16_type:
		return 0xFF'F0-1;
		break;
	case FAT32_type:
		return 0xF'FF'FF'F0-1;
		break;
	default:
		return 0;
	}
}

uint32_t FAT_image_t::min_end_of_chain_FAT() const
{
	switch (FAT_type) {
	case FAT12_type:
		return 0xFF8;
		break;
	case FAT16_type:
		return 0xFF'F8;
		break;
	case FAT32_type:
		return 0xF'FF'FF'F8;
		break;
	default:
		return 0;
	}
}

bool FAT_image_t::is_end_of_chain_FAT(uint32_t cluster) const 
{
	return cluster >= min_end_of_chain_FAT();
}

int FAT_image_t::search_for_bootsector() {
	// Search is unaligned to sectors its main intent is to skip metainfo added by some imaging tools
	std::unique_ptr<uint8_t[]> buffer{ new(nothrow) uint8_t[plugin_config.search_for_boot_sector_range] };
	if (buffer == nullptr) {
		return E_NO_MEMORY;
	}
	set_file_pointer(hArchFile, 0);
	auto result = read_file(hArchFile, buffer.get(), plugin_config.search_for_boot_sector_range);
	if (result != plugin_config.search_for_boot_sector_range) {
		return E_EREAD;
	}
	for(size_t i = 0; i<plugin_config.search_for_boot_sector_range-sector_size+1; ++i) {
		uint8_t* EB_pos = static_cast<uint8_t*>(memchr(buffer.get() + i, 0xEB, plugin_config.search_for_boot_sector_range - i));
		if (EB_pos == nullptr) {
			return 1;
		}
		if (*(EB_pos + 2) != 0x90) {
			i = (EB_pos - buffer.get()) + 1;
			continue;
		}
		if (*(EB_pos + 510) != 0x55 || *(EB_pos + 511) != 0xAA) {
			i = (EB_pos - buffer.get()) + 1;
			continue;
		}
		boot_sector_offset = (EB_pos - buffer.get());
		return 0;
	}
	return E_UNKNOWN_FORMAT;
}

//------- whole_disk_t implementation --------------------------
int whole_disk_t::detect_MBR() {
	set_file_pointer(hArchFile, 0);
	auto result = read_file(hArchFile, &mbrs[0], sector_size);
	if (result != sector_size) {
		return E_UNKNOWN_FORMAT;
	}
	if (mbrs[0].signature != 0xAA55) {
		return E_UNKNOWN_FORMAT;
	}
	for (int i = 0; i < mbrs[0].ptables(); ++i) {
		if (!mbrs[0].ptable[i].is_MBR_disk_status_OK())
			return E_UNKNOWN_FORMAT;
	} 
	return 0;
}

using archive_HANDLE = whole_disk_t*;

int whole_disk_t::process_MBR() {
	auto res = detect_MBR();
	if (res)
		return res;
	int extended_partition_idx = -1;
	for (int i = 0; i < mbrs[0].ptables(); ++i) {
		if (mbrs[0].ptable[i].is_total_zero())
			break;
		if (mbrs[0].ptable[i].is_LBAs_zero()) {
#ifdef _WIN32
				MessageBoxEx(
					NULL,
					TEXT("CHS-based partition, skipping"),
					TEXT("MBR error"),
					MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1,
					0
				);
#endif 
				continue;
			}
			partition_info_t curp;
			curp.first_sector = mbrs[0].ptable[i].get_first_sec_by_LBA();
			curp.last_sector = mbrs[0].ptable[i].get_last_sec_by_LBA();
			if (mbrs[0].ptable[i].is_extended()) { //-V807
				if (extended_partition_idx != -1) {
#ifdef _WIN32
					MessageBoxEx(
						NULL,
						TEXT("Too many extended partitions"),
						TEXT("MBR error"),
						MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1,
						0
					);
#endif 
				}
				else {
					extended_partition_idx = i;
				}
				while (true) {
					set_file_pointer(hArchFile, curp.first_sector * sector_size);
					mbrs.push_back({});
					auto result = read_file(hArchFile, &mbrs.back(), sector_size);
					if (result != sector_size) {
#ifdef _WIN32
							MessageBoxEx(
								NULL,
								TEXT("Cannot read extended partition"),
								TEXT("MBR error"),
								MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1,
								0
							);
#endif 
						break;
					}
					partition_info_t curp_ext;
					// Starting sector for extended boot record (EBR) is a relative offset between this 
					// EBR sector and the first sector of the logical partition
					curp_ext.first_sector = curp.first_sector
						+ mbrs.back().ptable[0].get_first_sec_by_LBA();
					// EBR size does not accounts for unused sectors before the EBR and start of the partition
					curp_ext.last_sector = curp_ext.first_sector
						+ mbrs.back().ptable[0].get_last_sec_by_LBA();
					partition_info.push_back(curp_ext);
					if (!mbrs.back().has_next_extended_record())
						break;
					// EBR next starting sector = LBA address of next EBR minus LBA address of extended partition's first EBR
					curp.first_sector = curp.first_sector + mbrs.back().ptable[1].get_first_sec_by_LBA();
					// EBR next size starts counts from the next EBR:
					curp.last_sector = curp_ext.last_sector + mbrs.back().ptable[1].get_last_sec_by_LBA();
				}
			}
			else {
				partition_info.push_back(curp);
			}
	}

	if(partition_info.empty())
		return E_UNKNOWN_FORMAT;
	return 0;
}


//-----------------------=[ DLL exports ]=--------------------

extern "C" {
	// OpenArchive should perform all necessary operations when an archive is to be opened
	DLLEXPORT archive_HANDLE STDCALL OpenArchive(tOpenArchiveData* ArchiveData)
	{
		std::unique_ptr<whole_disk_t> arch; // TCmd API expects HANDLE/raw pointer,
										// so smart pointer is used to manage cleanup on errors 
										// only inside this function
		//! Not used by TCmd yet.
		ArchiveData->CmtBuf = 0;
		ArchiveData->CmtBufSize = 0;
		ArchiveData->CmtSize = 0;
		ArchiveData->CmtState = 0;

		size_t archive_file_size = get_file_size(ArchiveData->ArcName);
		auto hArchFile = open_file_shared_read(ArchiveData->ArcName);
		if (hArchFile == file_open_error_v)
		{
			ArchiveData->OpenResult = E_EOPEN;
			return nullptr;
		}
		try {
			arch = std::make_unique<whole_disk_t>(winAPI_msgbox_on_bad_BPB, ArchiveData->ArcName, archive_file_size,
				hArchFile, ArchiveData->OpenMode);
		}
		catch (std::bad_alloc&) {
			ArchiveData->OpenResult = E_NO_MEMORY;
			return nullptr;
		}

		auto err_code = arch->disks[0].process_bootsector(true);

		if (err_code != 0) {
			if (plugin_config.process_DOS1xx_images) {
				err_code = arch->disks[0].process_DOS1xx_image();
			}
		}
		int first_err_code = 0;
		if (err_code != 0) {
			if (plugin_config.process_MBR) {
				err_code = arch->process_MBR();
				if (!err_code) {
					// Single partition -- treat as a non-partitioned disk for viewing
					arch->disks[0].set_boot_sector_offset(arch->partition_info[0].first_sector * arch->sector_size);
					first_err_code = arch->disks[0].process_bootsector(true);
					for (size_t i = 1; i < arch->partition_info.size(); ++i) {
						// Looks like push and then pop wrong would be more efficient now -- before move operations are implemented
						arch->disks.push_back(arch->disks[0]);
						arch->disks[i].set_boot_sector_offset(arch->partition_info[i].first_sector * arch->sector_size);
						err_code = arch->disks[i].process_bootsector(true);
						if (err_code) {
							// Unknown partition
							arch->disks.pop_back();
						}
					}
					if (arch->disks.empty() || (first_err_code != 0 && arch->disks.size() == 1) ) {
						err_code = E_UNKNOWN_FORMAT;
					}
					else {
						err_code = 0;
					}
				}
				else {
					err_code = E_UNKNOWN_FORMAT;
				}
			}
		}

		if (err_code == 0 && first_err_code) {
			// We have some partitions but first partition is not known
			arch->disks.erase(arch->disks.begin());
		}
		// No partitions -- attempt to find boot sector
		if (err_code != 0) {
			if (arch->disks[0].search_for_bootsector() == 0) {
				err_code = arch->disks[0].process_bootsector(true);
			}
		}

		if (err_code != 0) {
			ArchiveData->OpenResult = err_code;
			return nullptr;
		}

		int loaded_FATs = 0;
		size_t loaded_catalogs = 0;
		while( loaded_catalogs < arch->disks.size() ) {
			err_code = arch->disks[loaded_catalogs].load_FAT();
			if (err_code != 0 && loaded_FATs == 0) { // Saving the first error
				ArchiveData->OpenResult = err_code;
				arch->disks.erase(arch->disks.begin() + loaded_catalogs); // TODO: Test! 
			}
			else {
				++loaded_FATs;
				err_code = arch->disks[loaded_catalogs].load_file_list_recursively(minimal_fixed_string_t<MAX_PATH>{}, 0, 0);
				if (err_code != 0 && loaded_catalogs == 0) { // Saving the first error
					ArchiveData->OpenResult = err_code;
					arch->disks.erase(arch->disks.begin() + loaded_catalogs); // TODO: Test! 
				} 
				else {
					++loaded_catalogs;
				}
			}
		}

		if (loaded_catalogs > 0) {
			ArchiveData->OpenResult = 0; // OK
			return arch.release(); // Returns raw ptr and releases ownership 
		}
		else {
			return nullptr;
		}
	}

	// TCmd calls ReadHeader to find out what files are in the archive
	DLLEXPORT int STDCALL ReadHeader(archive_HANDLE hArcData, tHeaderData* HeaderData)
	{
		if (hArcData->disks[hArcData->disc_counter].counter ==				  //-V104
			hArcData->disks[hArcData->disc_counter].arc_dir_entries.size() ||
			hArcData->disks[hArcData->disc_counter].arc_dir_entries.empty()			
			) { //-V104
			hArcData->disks[hArcData->disc_counter].counter = 0;
			++hArcData->disc_counter;
			if (hArcData->disc_counter == hArcData->disks.size()) {
				hArcData->disc_counter = 0;
				return E_END_ARCHIVE;
			}

		}
		// get_disk_prefix
		auto& current_disk = hArcData->disks[hArcData->disc_counter];
		strcpy(HeaderData->ArcName, hArcData->archname.data());
		if (hArcData->disks.size() == 1) {
			strcpy(HeaderData->FileName, current_disk.arc_dir_entries[current_disk.counter].PathName.data());
		}
		else {
			auto res = hArcData->get_disk_prefix(hArcData->disc_counter);
			// TODO: Implement push_back for minimal_fixed_string
			//! If disk empty -- put only dir with its name
			if( !hArcData->disks[hArcData->disc_counter].arc_dir_entries.empty() )
				res.push_back(current_disk.arc_dir_entries[current_disk.counter].PathName.data());
			strcpy(HeaderData->FileName, res.data());

		}
		if (!hArcData->disks[hArcData->disc_counter].arc_dir_entries.empty()) {
			HeaderData->FileAttr = current_disk.arc_dir_entries[current_disk.counter].FileAttr;
			HeaderData->FileTime = current_disk.arc_dir_entries[current_disk.counter].FileTime;
			// For files larger than 2Gb -- implement tHeaderDataEx
			HeaderData->PackSize = static_cast<int>(current_disk.arc_dir_entries[current_disk.counter].FileSize);
		}
		else { // Just disk dir
			HeaderData->FileAttr = 0;  // TODO: use current time
			HeaderData->FileTime = 0;
			HeaderData->PackSize = 0;
		}
		HeaderData->UnpSize = HeaderData->PackSize;
		HeaderData->CmtBuf = 0;
		HeaderData->CmtBufSize = 0;
		HeaderData->CmtSize = 0;
		HeaderData->CmtState = 0;
		HeaderData->UnpVer = 0;
		HeaderData->Method = 0;
		HeaderData->FileCRC = 0;
		++current_disk.counter;
		return 0; // OK
	}

	// ProcessFile should unpack the specified file or test the integrity of the archive
	DLLEXPORT int STDCALL ProcessFile(archive_HANDLE hArcData, int Operation, char* DestPath, char* DestName) //-V2009
	{
		char dest[MAX_PATH] = "";
		file_handle_t hUnpFile;

		if (Operation == PK_SKIP) return 0;

		if (hArcData->disks[hArcData->disc_counter].counter == 0)
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

		auto res = hArcData->disks[hArcData->disc_counter].extract_to_file(hUnpFile, 
			hArcData->disks[hArcData->disc_counter].counter - 1);
		if (res != 0) {
			return res;
		}
		const auto& cur_entry = hArcData->disks[hArcData->disc_counter].
			arc_dir_entries[hArcData->disks[hArcData->disc_counter].counter - 1];
		set_file_datetime(hUnpFile, cur_entry.FileTime);
		close_file(hUnpFile);
		set_file_attributes_ex(dest, cur_entry.FileAttr);

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

	// PackSetDefaultParams is called immediately after loading the DLL, before any other function. 
	// This function is new in version 2.1. 
	// It requires Total Commander >=5.51, but is ignored by older versions.
	DLLEXPORT void STDCALL PackSetDefaultParams(PackDefaultParamStruct* dps) { //-V2009
		auto res = plugin_config.read_conf(dps);
		if (!res) { // Create default configuration if conf file is absent
			plugin_config.write_conf();
		}
	}
}
