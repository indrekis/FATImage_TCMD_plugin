// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/*
* Floppy disk images unpack plugin for the Total Commander.
* Copyright (c) 2002, IvGzury ( ivgzury@hotmail.com )
* Copyright (c) 2022-2025, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
* Copyright (c) 2025, Nataliia Sydor aka NataMontari ( natalya.sydor@gmail.com )
*
* Oleg Farenyuk's code is released under the MIT License.
*
* Original IvGzury copyright message:
* This program is absolutely free software.
* If you have any remarks or problems, please don't
* hesitate to send me an email.
*/

#include "sysio_winapi.h"
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
#include <sys/stat.h>
#include <sys/types.h>

#include <fstream>
#include <filesystem>


// #define FLTK_ENABLED_EXPERIMENTAL  // Here for the quick tests -- should be defined by the build system

#ifdef FLTK_ENABLED_EXPERIMENTAL
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/fl_ask.H>
#endif

extern "C" {
#include "ff.h"
#include "diskio.h"
}

using std::nothrow, std::uint8_t;

#ifdef _WIN32
#define WCX_PLUGIN_EXPORTS
#endif 

#ifdef WCX_PLUGIN_EXPORTS
#define DLLEXPORT __declspec(dllexport)
#define STDCALL __stdcall
//! Not enough for the Win32 -- exports would be decorated by: _name@XX.
//! This can help but reverting to the def-file is simpler:
//! #pragma comment(linker, "/EXPORT:" __FUNCTION__ "=" __FUNCDNAME__) 
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
	size_t FileSize = 0;
	uint32_t FileTime = 0;
	FAT_attrib_t FileAttr{};
	uint32_t FirstClus = 0;
};

plugin_config_t plugin_config;

//! Contains archive configuration, so FAT_image_t needs it
struct whole_disk_t;

struct FAT_image_t
{
	enum FAT_types { unknown_FS_type, FAT12_type, FAT16_type, FAT32_type, exFAT_type}; // , FAT_DOS100_type, FAT_DOS110_type

	const whole_disk_t* whole_disk_ptr = nullptr;
	size_t boot_sector_offset = 0;

	bool is_processed_m = false;	// Useful when unknown FS or empty FAT FS

	FAT_types FAT_type = unknown_FS_type;
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

	FAT_image_t(const whole_disk_t* ptr) : whole_disk_ptr(ptr)
	{
	}

	~FAT_image_t() = default;

	void set_boot_sector_offset(size_t off) {
		boot_sector_offset = off;
	}

	size_t get_boot_sector_offset() const {
		return boot_sector_offset;
	}

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

	size_t get_bytes_per_FAT() const;

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

	size_t get_sector_size() const;
	auto   get_openmode() const;
	size_t get_image_file_size() const;
	file_handle_t get_archive_handler() const;

	uint64_t get_total_sectors_in_volume() const;
	uint64_t get_data_sectors_in_volume() const;
	uint64_t get_data_clusters_in_volume() const;

	bool is_processed() const {
		if (arc_dir_entries.empty())
			return is_processed_m;
		else
			return counter == arc_dir_entries.size(); //-V104
	}

	void set_processed_for_empty() {
		is_processed_m = true;
	}

	int process_bootsector(bool read_bootsec);
	int process_DOS1xx_image();

	int search_for_bootsector();

	int load_FAT();

	FAT_types detect_FAT_type() const;

	bool is_known_FS_type() const {
		return FAT_type == FAT12_type || FAT_type == FAT16_type || FAT_type == FAT32_type;
	}

	int extract_to_file(file_handle_t hUnpFile, uint32_t idx);

	// root passed by copy to avoid problems while relocating vector
	int load_file_list_recursively(minimal_fixed_string_t<MAX_PATH> root, uint32_t firstclus, uint32_t depth); //-V813

	uint32_t get_first_cluster(const FATxx_dir_entry_t& dir_entry) const;

	uint32_t next_cluster_FAT12(uint32_t firstclus) const;
	uint32_t next_cluster_FAT16(uint32_t firstclus) const;
	uint32_t next_cluster_FAT32(uint32_t firstclus) const;
	uint32_t next_cluster_FAT(uint32_t firstclus) const;

	uint32_t max_cluster_FAT(FAT_types type) const; // For FAT detect
	uint32_t max_cluster_FAT() const;
	uint32_t max_normal_cluster_FAT(FAT_types type) const;
	uint32_t max_normal_cluster_FAT() const;
	uint32_t min_end_of_chain_FAT() const;
	bool is_end_of_chain_FAT(uint32_t) const;

	struct LFN_accumulator_t {
		minimal_fixed_string_t<MAX_PATH> cur_LFN_name{};
		uint8_t cur_LFN_CRC = 0;
		int cur_LFN_record_index = 0;
		void start_processing(const VFAT_LFN_dir_entry_t* LFN_record) {
			cur_LFN_CRC = LFN_record->LFN_DOS_name_CRC;
			cur_LFN_record_index = 1;
			cur_LFN_name.clear();
			LFN_record->dir_LFN_entry_to_ASCII_str(cur_LFN_name);
		}
		void append_LFN_part(const VFAT_LFN_dir_entry_t* LFN_record) {
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
		void process_LFN_record(const FATxx_dir_entry_t* entry);
	};
};

struct partition_info_t {
	uint32_t first_sector = 0;
	uint32_t last_sector = 0;
	uint8_t  partition_id = 0;
};

struct whole_disk_t {
	static constexpr uint32_t sector_size = 512;
	minimal_fixed_string_t<MAX_PATH> archname; // Should be saved for the TCmd API
	file_handle_t hArchFile = file_handle_t(); //opened file handles
	int openmode_m = PK_OM_LIST;
	size_t image_file_size = 0;

	tChangeVolProc   pLocChangeVol = nullptr;
	tProcessDataProc pLocProcessData = nullptr;

	whole_disk_t(const char* archname_in, size_t vol_size, file_handle_t fh, int openmode):
		hArchFile{ fh }, openmode_m(openmode), image_file_size(vol_size)
	{
		archname.push_back(archname_in);
		// First disk represents also non-partitioned image -- so, initially, it's size = whole image size.
		disks.emplace_back(this);
	}

	// Note that disks, partition_info, mbrs are not synchronized. 
	std::vector<FAT_image_t>		disks;
	std::vector<MBR_t>				mbrs{ 1 };
	std::vector<partition_info_t>	partition_info;
	uint32_t disc_counter = 0;

	int detect_MBR();
	int process_MBR();

	int detect_GPT();

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

	//! Process boot record if it is a single-disk volume or process all known volumes from the MBR 
	int process_volumes();

	//! Error handler for safe functions:
	_invalid_parameter_handler oldHandler = nullptr;
};

//------- FAT_image_t implementation -----------------------------
// Though using methods of the whole_disk_t would be more OOP-style, code verbosity becomes too large for me (Indrekis)
size_t FAT_image_t::get_sector_size() const {
	return whole_disk_ptr->sector_size; //-V109
}

size_t FAT_image_t::get_bytes_per_FAT() const {
	return get_sectors_per_FAT() * get_sector_size(); //-V104 //-V109
}

auto FAT_image_t::get_openmode() const {
	return whole_disk_ptr->openmode_m;
}

file_handle_t FAT_image_t::get_archive_handler() const {
	return whole_disk_ptr->hArchFile;
}

size_t FAT_image_t::get_image_file_size() const {
	return whole_disk_ptr->image_file_size;
}

int FAT_image_t::process_bootsector(bool read_bootsec) {
	if(read_bootsec){
		set_file_pointer(get_archive_handler(), boot_sector_offset);
		auto result = read_file(get_archive_handler(), &bootsec, get_sector_size());
		if (result != get_sector_size()) {
			return E_EREAD;
		}
	}
	// if read_bootsec == false -- user preread bootsector

	if (bootsec.BPB_bytesPerSec != get_sector_size()) {
		return E_UNKNOWN_FORMAT;
	}

	if (bootsec.signature != 0xAA55) {
		plugin_config.log_print_dbg("Warning# Wrong boot signature: 0x%04X", bootsec.signature);
		if (!plugin_config.ignore_boot_signature) {
			if (plugin_config.allow_dialogs) {
#ifdef FLTK_ENABLED_EXPERIMENTAL
				if (get_openmode() == PK_OM_LIST) {
					auto res = fl_choice("Wrong boot signature: %04x", "Stop", "OK", "Try MBR", bootsec.signature);

					//! Conf would be re-read for the new image
					switch (res) {
					case 0: // Left btn -- Stop 
						plugin_config.process_DOS1xx_images = false;
						plugin_config.search_for_boot_sector = false;
						plugin_config.process_MBR = false;
						return E_BAD_ARCHIVE;
						break;
					case 1: // Middle btn -- OK (default)
						break;
					case 2: // Right button -- Try MBR (only -- skip DOS1.xx and do not search for bootsector)
						plugin_config.process_DOS1xx_images = false;
						plugin_config.search_for_boot_sector = false;
						return E_BAD_ARCHIVE;
						break;
					default:;
					}
					// int res = whole_disk_ptr->on_bad_BPB_callback(this);
				}
#endif 
			}
			else {
				return E_BAD_ARCHIVE;
			}
		}
	}

	cluster_size_m = static_cast<uint32_t>(get_sector_size() * bootsec.BPB_SecPerClus);
	FAT1area_off_m = get_boot_sector_offset() + get_sector_size() * bootsec.BPB_RsvdSecCnt;
	rootarea_off_m = get_boot_sector_offset() + get_sector_size() * (bootsec.BPB_RsvdSecCnt +
		get_sectors_per_FAT() * static_cast<size_t>(bootsec.BPB_NumFATs)); //-V104
	dataarea_off_m = get_root_area_offset() + get_root_dir_entry_count() * sizeof(FATxx_dir_entry_t);

	plugin_config.log_print("Info# Bytes per sector: %d", bootsec.BPB_bytesPerSec);
	plugin_config.log_print("Info# Sectors per cluster: %d", static_cast<int>(bootsec.BPB_SecPerClus));
	plugin_config.log_print("Info# Reserved sectors: %d", bootsec.BPB_RsvdSecCnt);
	plugin_config.log_print("Info# Number of FATs: %d", static_cast<int>(bootsec.BPB_NumFATs));
	plugin_config.log_print("Info# Root entries count: %d", bootsec.BPB_RootEntCnt);
	plugin_config.log_print("Info# Total sectors 16-bit: %d", bootsec.BPB_TotSec16);
	plugin_config.log_print("Info# Media descriptor: %d", static_cast<int>(bootsec.BPB_MediaDescr));
	plugin_config.log_print("Info# Sectors per FAT: %d", bootsec.BPB_SectorsPerFAT);
	plugin_config.log_print("Info# Sectors per track: %d", bootsec.BPB_SecPerTrk);
	plugin_config.log_print("Info# Heads: %d", bootsec.BPB_NumHeads);
	plugin_config.log_print("\nInfo# Bytes in cluster: %d", cluster_size_m);
	plugin_config.log_print("Info# FAT1 area offset: 0x%010X", FAT1area_off_m);
	plugin_config.log_print("Info# Root area offset: 0x%010X", rootarea_off_m);
	plugin_config.log_print("Info# Data area offset: 0x%010X", dataarea_off_m);

	FAT_type = detect_FAT_type();

	switch (FAT_type) {
	case FAT12_type:
		plugin_config.log_print("Info# Preliminary FAT type: FAT12");
		if ((get_sectors_per_FAT() < 1) || (get_sectors_per_FAT() > 12)) {
			return E_UNKNOWN_FORMAT;
		}
		break;
	case FAT16_type:
		plugin_config.log_print("Info# Preliminary FAT type: FAT16");
		if ((get_sectors_per_FAT() < 1) || (get_sectors_per_FAT() > 256)) { // get_sectors_per_FAT() < 16 according to standard
			return E_UNKNOWN_FORMAT;
		}
		break;
	case FAT32_type:
		plugin_config.log_print("Info# Preliminary FAT type: FAT32");
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
	case exFAT_type:
		plugin_config.log_print_dbg("Warning# Preliminary FAT type: exFAT. Skipping.");
		break;
	case unknown_FS_type:
		plugin_config.log_print_dbg("Warning# Filesystem type unknown. Skipping.");
		return E_UNKNOWN_FORMAT;
	default:
		plugin_config.log_print_dbg("Warning# Filesystem type unknown. Skipping.");
		//! Here also unsupported (yet) formats like exFAT
		return E_UNKNOWN_FORMAT;
	}

	if (FAT_type == FAT32_type) {
		plugin_config.log_print("Info# FAT32 hidden sectors: %d", bootsec.EBPB_FAT32.BPB_HiddSec);
		plugin_config.log_print("Info# FAT32 total sectors 32-bit: %d", bootsec.EBPB_FAT32.BPB_TotSec32);
		plugin_config.log_print("Info# FAT32 sectors per FAT: %d", bootsec.EBPB_FAT32.BS_SectorsPerFAT32);
		plugin_config.log_print("Info# FAT32 FAT mirroring: %d", bootsec.EBPB_FAT32.is_FAT_mirrored());
		if (!bootsec.EBPB_FAT32.is_FAT_mirrored()) {
			plugin_config.log_print("Info# FAT32 active FAT: %d", bootsec.EBPB_FAT32.get_active_FAT());
		}
		plugin_config.log_print("Info# FAT32 Information Sector: %d", bootsec.EBPB_FAT32.BS_FSInfoSec);
		plugin_config.log_print("Info# FAT32 backup of boot sector: %d", bootsec.EBPB_FAT32.BS_KbpBootSec);
		plugin_config.log_print("Info# FAT32 Volume ID: 0x%010X", bootsec.EBPB_FAT32.BS_VolID);
		char vol_label[12];
		bootsec.EBPB_FAT32.get_volume_label(vol_label);
		plugin_config.log_print("Info# FAT32 Volume label: %s", vol_label);
	}

	return 0;
}

//! Used for reading DOS 1.xx images as a bootsector template. Taken from the IBM PC-DOS 2.00 (5.25) Disk01.img
//! See also https://github.com/microsoft/MS-DOS
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
	//! 
	//! TODO: DOS 2.0 - DOS 3.2 sometimes wrote wrong BPB. For non DOS 1.xx images -- add check of the media ID
	//!			http://www.os2museum.com/wp/dos-boot-sector-bpb-and-the-media-descriptor-byte/ -- describes possible heuristics 
	//!			d

	uint8_t media_descr = 0;

	// Using get_boot_sector_offset() is questionable here -- partitions should have BPB, but it does not harm.
	auto res = set_file_pointer( get_archive_handler(), get_boot_sector_offset() + get_sector_size() );
	if (!res) {
		return E_EREAD;
	}
	auto rdres = read_file(get_archive_handler(), &media_descr, 1);
	if (rdres != 1) {
		return E_EREAD;
	}
	memcpy_s(&bootsec, get_sector_size(), raw_DOS200_bootsector, sizeof(raw_DOS200_bootsector));
	switch (media_descr) {
	case 0xFE: // 5.25" 160Kb/163'840b img; H:C:S = 1:40:8, DOS 1.00
		if (get_image_file_size() != 160 * 1024) {
			return E_UNKNOWN_FORMAT;
		}
		plugin_config.log_print("Info# DOS 1.00 image -- 160Kb");
		bootsec.BPB_SecPerClus = 1;
		bootsec.BPB_RsvdSecCnt = 1;
		bootsec.BPB_NumFATs = 2;
		bootsec.BPB_RootEntCnt = static_cast<uint16_t>((get_sector_size()/sizeof(FATxx_dir_entry_t)) * 4); // 4 sectors in root dir
		bootsec.BPB_TotSec16 = 320;
		bootsec.BPB_MediaDescr = 0xFE;
		bootsec.BPB_SectorsPerFAT = 1;
		// Free sectors = 313
		// Hidden sectors = 0
		break;
	case 0xFC: // 5.25" 180Kb/184'320b img; H:C:S = 1:40:9, DOS 2.00
		if (get_image_file_size() != 180 * 1024) {
			return E_UNKNOWN_FORMAT;
		}
		plugin_config.log_print("Info# DOS 2.00 image -- 180Kb");
		bootsec.BPB_SecPerClus = 1;
		bootsec.BPB_RsvdSecCnt = 1;
		bootsec.BPB_NumFATs = 2;
		bootsec.BPB_RootEntCnt = static_cast<uint16_t>((get_sector_size() / sizeof(FATxx_dir_entry_t)) * 4); // 4 sectors in root dir
		bootsec.BPB_TotSec16 = 360;
		bootsec.BPB_MediaDescr = 0xFC;
		bootsec.BPB_SectorsPerFAT = 2;
		// Free sectors = 351
		// Hidden sectors = 0
		break;
	case 0xFF: // 5.25" 320Kb/327'680b img; H:C:S = 2:40:8, DOS 1.10
		if (get_image_file_size() != 320 * 1024) {
			if (plugin_config.process_DOS1xx_exceptions) {  // 331792
				plugin_config.log_print("Info# Processing DOS 1.xx exceptions");
				if (get_image_file_size() != 331'792) {
					//! Exception for the "MS-DOS 1.12.ver.1.12 OEM [Compaq]" image, containing 
					//! 4112 bytes at the end, bracketed by "Skip  8 blocks " text.
					return E_UNKNOWN_FORMAT;
				}
			}
			else {
				return E_UNKNOWN_FORMAT;
			}
		}
		plugin_config.log_print("Info# DOS 1.10 image -- 320Kb");
		bootsec.BPB_SecPerClus = 2;
		bootsec.BPB_RsvdSecCnt = 1;
		bootsec.BPB_NumFATs = 2;
		bootsec.BPB_RootEntCnt = static_cast<uint16_t>((get_sector_size() / sizeof(FATxx_dir_entry_t)) * 7); // 7 sectors in root dir
		bootsec.BPB_TotSec16 = 640;
		bootsec.BPB_MediaDescr = 0xFF;
		bootsec.BPB_SectorsPerFAT = 1;
		// Free sectors = 630
		// Hidden sectors = 0
		break;
	case 0xFD: // 5.25" 360Kb/368'640b img; H:C:S = 2:40:8, DOS 1.10
		if (get_image_file_size() != 360 * 1024) {
			return E_UNKNOWN_FORMAT;
		}
		plugin_config.log_print("Info# DOS 1.10 image -- 360Kb");
		bootsec.BPB_SecPerClus = 2;
		bootsec.BPB_RsvdSecCnt = 1;
		bootsec.BPB_NumFATs = 2;
		bootsec.BPB_RootEntCnt = static_cast<uint16_t>((get_sector_size() / sizeof(FATxx_dir_entry_t)) * 7); // 7 sectors in root dir
		bootsec.BPB_TotSec16 = 720;
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
	set_file_pointer(get_archive_handler(), get_FAT1_area_offset());
	auto result = read_file(get_archive_handler(), fattable.data(), fat_size_bytes);
	if (result != fat_size_bytes)
	{
		plugin_config.log_print_dbg("Error# Failed to read FAT from the image: %zd", result);
		return E_EREAD;
	}
	return 0;
}

uint64_t FAT_image_t::get_total_sectors_in_volume() const {
	uint64_t sectors = 0;
	if (bootsec.BPB_TotSec16 != 0) {
		sectors = bootsec.BPB_TotSec16;
		//! Formally, FAT12 images can contain DOS 3.31+ BPB and have inconsistent BPB_TotSec32/BPB_TotSec16,
		//! but checking this for FAT12 causes too many false warnings -- because of old images
		if (FAT_type != FAT12_type && bootsec.EBPB_FAT.BPB_TotSec32 != 0 && bootsec.BPB_TotSec16 != bootsec.EBPB_FAT.BPB_TotSec32) {
			plugin_config.log_print_dbg("Warning# Inconsistent BPB_TotSec16 and BPB_TotSec32: %d / %d; or pre DOS 3.31 BPB",
				bootsec.BPB_TotSec16, bootsec.EBPB_FAT.BPB_TotSec32);
#ifdef FLTK_ENABLED_EXPERIMENTAL
			if (plugin_config.allow_dialogs) {
				if (get_openmode() == PK_OM_LIST) {
					fl_alert("Inconsistent BPB_TotSec16 and BPB_TotSec32 or pre-DOS 3.31 BPB");
				}
			}
#endif 
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
	plugin_config.log_print("Info# Total sectors in FAT: %d", sectors);
	return sectors;
}

uint64_t FAT_image_t::get_data_sectors_in_volume() const {
	return get_total_sectors_in_volume() -
		(get_data_area_offset() - get_boot_sector_offset() )/ bootsec.BPB_bytesPerSec;
}

uint64_t FAT_image_t::get_data_clusters_in_volume() const {
	return get_data_sectors_in_volume() / bootsec.BPB_SecPerClus;
}

FAT_image_t::FAT_types FAT_image_t::detect_FAT_type() const {
	// See http://jdebp.info/FGA/determining-fat-widths.html
	auto bytes_per_sector = bootsec.BPB_bytesPerSec;
	if(bytes_per_sector == 0){
		return FAT_image_t::exFAT_type;
	}
	auto clusters = get_data_clusters_in_volume();
	if (strncmp(bootsec.EBPB_FAT.BS_FilSysType, "FAT12   ", 8) == 0) {
		if (clusters > max_cluster_FAT(FAT12_type)) {
			plugin_config.log_print_dbg("Warning# String \"FAT12\" found in boot, "
				"but too many clusters: %zd of %d", clusters, max_cluster_FAT(FAT12_type));
			return FAT_image_t::unknown_FS_type;
		}
		if (clusters > max_normal_cluster_FAT(FAT12_type)) {
			plugin_config.log_print_dbg("Warning# FAT12 contains unusual "
				" clusters number: %zd of %d", clusters, max_normal_cluster_FAT(FAT12_type));
		}
		return FAT_image_t::FAT12_type;
	}
	if (strncmp(bootsec.EBPB_FAT.BS_FilSysType, "FAT16   ", 8) == 0) {
		if (clusters > 0x0FFF6) {
			plugin_config.log_print_dbg("Warning# String \"FAT16\" found in boot, "
				"but too many clusters: %zd of %d", clusters, max_cluster_FAT(FAT16_type));
			return FAT_image_t::unknown_FS_type;
		}
		if (clusters > max_normal_cluster_FAT(FAT16_type)) {
			plugin_config.log_print_dbg("Warning# FAT16 contains unusual "
				" clusters number:  %zd of %d", clusters, max_normal_cluster_FAT(FAT16_type));
		}
		return FAT_image_t::FAT16_type;
	}
	if (strncmp(bootsec.EBPB_FAT.BS_FilSysType, "FAT32   ", 8) == 0 || // Could contain it
		strncmp(bootsec.EBPB_FAT32.BS_FilSysType, "FAT32   ", 8) == 0
		) {
		return FAT_image_t::FAT32_type;
	}

	if (clusters >= 0x00000002 && clusters <= max_cluster_FAT(FAT12_type)) { // 2 - 0x00000FF6: 2–4086
		if (clusters > max_normal_cluster_FAT(FAT12_type)) {
			plugin_config.log_print_dbg("Warning# FAT12 contains unusual "
				" clusters number:  %zd of %d", clusters, max_normal_cluster_FAT(FAT12_type));
		}
		return FAT_image_t::FAT12_type;
	}

	//! TODO: Possible small FAT32 discs without BS_FilSysType could be misdetected.
	if (clusters >= max_cluster_FAT(FAT12_type)+1 &&
		clusters <= max_cluster_FAT(FAT16_type)) { // 0x00000FF7 - 0x0000FFF6: 4087–65526
		if (clusters > max_normal_cluster_FAT(FAT16_type)) {
			plugin_config.log_print_dbg("Warning# FAT16 contains unusual "
				" clusters number: %zd of %d", clusters, max_normal_cluster_FAT(FAT16_type));
		}
		return FAT_image_t::FAT16_type;
	}

	if (clusters >= max_cluster_FAT(FAT16_type)+1 &&
		clusters <= max_cluster_FAT(FAT32_type)) { // 0x0000FFF7 - 0x0FFFFFF6: 65527–268435446
		if (clusters > max_normal_cluster_FAT(FAT32_type)) {
			plugin_config.log_print_dbg("Warning# FAT32 contains unusual "
				" clusters number: %zd of %d", clusters, max_normal_cluster_FAT(FAT32_type));
		}
		return FAT_image_t::FAT32_type;
	}
	return FAT_image_t::unknown_FS_type; // Unknown format
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
				plugin_config.log_print_dbg("Error# Wrong cluster number in chain: %d in file: %s",
					nextclus, cur_entry.PathName.data());
				close_file(hUnpFile);
				return E_UNKNOWN_FORMAT;
			}
			set_file_pointer(get_archive_handler(), cluster_to_image_off(nextclus));
			size_t towrite = std::min<size_t>(get_cluster_size(), remaining);
			size_t result = read_file(get_archive_handler(), buff.data(), towrite);
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

			nextclus = next_cluster_FAT(nextclus); // TODO: fix if too large cluster number
		}
		return 0;
	}
	catch (std::bad_alloc&) {
		return E_NO_MEMORY;
	}
}

void FAT_image_t::LFN_accumulator_t::process_LFN_record(const FATxx_dir_entry_t* entry) {
	auto LFN_record = as_LFN_record(entry);
	if (!are_processing()) {
		if (!LFN_record->is_LFN_record_valid() || !LFN_record->is_first_LFN()) {
			return; // No record
		}
		start_processing(LFN_record);
	}
	else {
		if (!LFN_record->is_LFN_record_valid()) {
			abort_processing();
		}
		else if (LFN_record->is_first_LFN()) { // Should restart processing 
			start_processing(LFN_record);
		}
		else if (cur_LFN_CRC != LFN_record->LFN_DOS_name_CRC) {
			abort_processing();
		}
		else {
			append_LFN_part(LFN_record);
		}
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
		set_file_pointer(get_archive_handler(), get_root_area_offset());
		portion_size = get_root_dir_size();
	}
	else {
		set_file_pointer(get_archive_handler(), cluster_to_image_off(firstclus));
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
		plugin_config.log_print_dbg("Warning# Unusual first "
			"clusters number:  %d of %d", firstclus, max_normal_cluster_FAT());
	}
	if ( (firstclus == 1) || (firstclus >= max_cluster_FAT()) ) {
		plugin_config.log_print_dbg("Error# Wrong first "
			"clusters number: %d of 2-%d", firstclus, max_cluster_FAT());
		return E_UNKNOWN_FORMAT;
	}
	size_t result = read_file(get_archive_handler(), sector.get(), portion_size);
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
					current_LFN.process_LFN_record(&sector[entry_in_cluster]);
				}
				entry_in_cluster++;
				continue;
			}

			if (sector[entry_in_cluster].is_dir_record_volumeID())
			{
				minimal_fixed_string_t<12> voll;
				sector[entry_in_cluster].dir_entry_name_to_str(voll);
				plugin_config.log_print_dbg("Info# Volume label: %s", voll.data());
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
			newentryref.PathName.push_back(root); // Empty root is OK, "\\" OK too
			uint32_t invalid_chars = 0;
			if (plugin_config.use_VFAT && current_LFN.are_processing()) {
				if (current_LFN.cur_LFN_CRC == VFAT_LFN_dir_entry_t::LFN_checksum(sector[entry_in_cluster].DIR_Name)) {
					newentryref.PathName.push_back(current_LFN.cur_LFN_name);
				}
				else {
					auto res = sector[entry_in_cluster].process_E5();
					if(!res)
						plugin_config.log_print_dbg("Warning# E5 occurred at first symbol.");
					invalid_chars = sector[entry_in_cluster].dir_entry_name_to_str(newentryref.PathName);
					// No OS/2 EA on FAT32
				}
				current_LFN.abort_processing();
			}
			else {
				auto res = sector[entry_in_cluster].process_E5();
				if (!res)
					plugin_config.log_print_dbg("Warning# E5 occurred at first symbol.");
				invalid_chars = sector[entry_in_cluster].dir_entry_name_to_str(newentryref.PathName);
				if (invalid_chars == FATxx_dir_entry_t::LLDE_OS2_EA) {
					plugin_config.log_print_dbg("Info# OS/2 Extended attributes found.");
					has_OS2_EA = true;
				}
			}

			newentryref.FileTime = sector[entry_in_cluster].get_file_datetime();
			newentryref.FileSize = sector[entry_in_cluster].DIR_FileSize;
			newentryref.FirstClus = get_first_cluster(sector[entry_in_cluster]);

			if (sector[entry_in_cluster].is_dir_record_dir()) {
				newentryref.PathName.push_back('\\'); // Neccessery for empty dirs to be "enterable"
			}
			if (depth > plugin_config.max_depth) {
				plugin_config.log_print_dbg("Too many nested directories: %d.", depth);
				break;
			}
			if (sector[entry_in_cluster].is_dir_record_dir() &&
				(newentryref.FirstClus < max_cluster_FAT()) && (newentryref.FirstClus > 0x1)
				&& (depth <= plugin_config.max_depth))  //-V560 // Always true after the previous if, but leaving it here for clarity
			{
				if(invalid_chars > plugin_config.max_invalid_chars_in_dir && invalid_chars != FATxx_dir_entry_t::LLDE_OS2_EA) {
					plugin_config.log_print_dbg("Warning# Invalid characters in directory name: %s, skipping", newentryref.PathName.data());
				}
				else {
					load_file_list_recursively(newentryref.PathName, newentryref.FirstClus, depth + 1);
				}
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
			if (firstclus >= max_normal_cluster_FAT() && !is_end_of_chain_FAT(firstclus)) {
				plugin_config.log_print_dbg("Warning# Unusual next "
					"clusters number: %d of %d", firstclus, max_normal_cluster_FAT());
				break;
			}
			if ((firstclus <= 1) || ( (firstclus >= max_cluster_FAT()) && !is_end_of_chain_FAT(firstclus)) ) {
				plugin_config.log_print_dbg("Error# Wrong next "
					"clusters number: %d of 2-%d", firstclus, max_cluster_FAT());
				break;
			}
			if (is_end_of_chain_FAT(firstclus)) {
				break;
			}

			set_file_pointer(get_archive_handler(), cluster_to_image_off(firstclus)); //-V104
		}
		result = read_file(get_archive_handler(), sector.get(), portion_size);
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
	if (FAT_byte_pre >= fattable.data() + fattable.size()){
		plugin_config.log_print_dbg("Warning# Too large cluster number %u of %zu present", firstclus, (3 * fattable.size())/2);
		return max_cluster_FAT(FAT12_type);
	}
	//! Extract word, containing next cluster:
	const uint16_t* word_ptr = reinterpret_cast<const uint16_t*>(FAT_byte_pre);
	// Extract correct 12 bits -- lower for odd, upper for even: 
	return ((*word_ptr) >> ((firstclus % 2) ? 4 : 0)) & 0x0FFF; //-V112
}

uint32_t FAT_image_t::next_cluster_FAT16(uint32_t firstclus) const
{
	const auto FAT_byte_pre = fattable.data() + static_cast<size_t>(firstclus) * 2;
	if( FAT_byte_pre >= fattable.data() + fattable.size() ) {
		plugin_config.log_print_dbg("Warning# Too large cluster number %u of %zu present", firstclus, fattable.size()/2);
		return max_cluster_FAT(FAT16_type);
	}
	const uint16_t* word_ptr = reinterpret_cast<const uint16_t*>(FAT_byte_pre);
	return *word_ptr;
}

uint32_t FAT_image_t::next_cluster_FAT32(uint32_t firstclus) const
{
	const auto FAT_byte_pre = fattable.data() + static_cast<size_t>(firstclus) * 4; //-V112
	if (FAT_byte_pre >= fattable.data() + fattable.size()) {
		plugin_config.log_print_dbg("Warning# Too large cluster number %u of %zu present", firstclus, fattable.size()/4);
		return max_cluster_FAT(FAT32_type);
	}
	const uint32_t* word_ptr = reinterpret_cast<const uint32_t*>(FAT_byte_pre); //-V206
	return (*word_ptr) & 0x0F'FF'FF'FF; // Zero upper 4 bits
}

uint32_t FAT_image_t::next_cluster_FAT(uint32_t firstclus) const
{
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
uint32_t FAT_image_t::max_cluster_FAT(FAT_types type) const
{
	switch (type) {
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

uint32_t FAT_image_t::max_cluster_FAT() const
{
	return max_cluster_FAT(FAT_type);
}

uint32_t FAT_image_t::max_normal_cluster_FAT(FAT_types type) const
{
	switch (type) {
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

uint32_t FAT_image_t::max_normal_cluster_FAT() const
{
	return max_normal_cluster_FAT(FAT_type);
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
	set_file_pointer(get_archive_handler(), 0);
	auto result = read_file(get_archive_handler(), buffer.get(), plugin_config.search_for_boot_sector_range);
	if (result != plugin_config.search_for_boot_sector_range) {
		return E_EREAD;
	}
	for(size_t i = 0; i<plugin_config.search_for_boot_sector_range-get_sector_size()+1; ++i) {
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
using archive_HANDLE = whole_disk_t*;

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

//! TODO: Add support for other sector sizes -- at least, 4K
//! Use 
//! losetup --sector-size ...
//! and parted to created debug images:
//! (parted) mklabel GPT
//! (parted) mkpart primary 2048s 100%
int whole_disk_t::detect_GPT() {
	// https://en.wikipedia.org/wiki/GUID_Partition_Table
	set_file_pointer(hArchFile, sector_size); // Second sector -- LBA 1
	GPT_PTH_t buff;
	auto result = read_file(hArchFile, &buff, sector_size);
	if (result != sector_size) {
		return E_UNKNOWN_FORMAT;
	}
	auto cmp = memcmp(&buff.signature, GPT_PTH_signature, sizeof(GPT_PTH_signature));
	if (cmp != 0) {
		return E_UNKNOWN_FORMAT;
	}

	if (buff.reserved_0 != 0) {
		return E_UNKNOWN_FORMAT;
	}

	return 0;
}

int whole_disk_t::process_MBR() {
	auto res = detect_MBR();
	if (res) {
		return res;
	}
	// As for now -- just for debug
	auto resGPT = detect_GPT();
	if (resGPT == 0) {
		plugin_config.log_print_dbg("Warning# not supported GPT disk detected.");
		if (mbrs[0].ptable[0].type != 0xEE) {
			plugin_config.log_print_dbg("Warning# Wrong GPT protective partititon MBR.");
		}
#ifdef FLTK_ENABLED_EXPERIMENTAL
		if (openmode_m == PK_OM_LIST) {
			fl_alert("Not supported GPT disk detected.");
		}
#endif 
		return E_UNKNOWN_FORMAT;
	}
	int extended_partition_idx = -1;
	for (int i = 0; i < mbrs[0].ptables(); ++i) {
		if (mbrs[0].ptable[i].is_total_zero()) //-V807
			break;
		if (mbrs[0].ptable[i].is_LBAs_zero()) {
			plugin_config.log_print_dbg("Warning# CHS-based partition, skipping");
			if (plugin_config.allow_dialogs) {
#ifdef FLTK_ENABLED_EXPERIMENTAL
				if (openmode_m == PK_OM_LIST) {
					fl_alert("CHS-based partition, skipping");
				}
				// TODO: Add detection based on first boot sector CHS values or/and ask user for CHS
#endif 
			}
			continue;
		}
		partition_info_t curp;
		curp.first_sector = mbrs[0].ptable[i].get_first_sec_by_LBA();
		curp.last_sector = mbrs[0].ptable[i].get_last_sec_by_LBA();
		curp.partition_id = mbrs[0].ptable[i].type;
		if (mbrs[0].ptable[i].is_extended()) { //-V807
			if (extended_partition_idx != -1) {
				plugin_config.log_print_dbg("Warning# Too many extended partitions, new extended index: %d",
					extended_partition_idx);
				if (plugin_config.allow_dialogs) {
#ifdef FLTK_ENABLED_EXPERIMENTAL
					if (openmode_m == PK_OM_LIST) {
						fl_alert("Too many extended partitions");
					}
#endif 
				}
			}
			extended_partition_idx = i;
			auto cur_ext_start = mbrs[0].ptable[i].get_first_sec_by_LBA();
			uint32_t EBR_offset = 0;
			while (true) {
				set_file_pointer(hArchFile, (cur_ext_start + EBR_offset) * sector_size); //-V106
				mbrs.push_back({});
				auto result = read_file(hArchFile, &mbrs.back(), sector_size);
				if (result != sector_size) {
					plugin_config.log_print_dbg("Warning# Error reading boot sector: %zd", result);
					break;
				}
				partition_info_t curp_ext;
				// Starting sector for extended boot record (EBR) is a relative offset between this 
				// EBR sector and the first sector of the logical partition
				curp_ext.first_sector = cur_ext_start + EBR_offset
					+ mbrs.back().ptable[0].get_first_sec_by_LBA();
				// EBR size does not accounts for unused sectors before the EBR and start of the partition
				curp_ext.last_sector = curp_ext.first_sector
					+ mbrs.back().ptable[0].get_last_sec_by_LBA();
				curp_ext.partition_id = mbrs.back().ptable[0].type;
				partition_info.push_back(curp_ext);
				if (!mbrs.back().has_next_extended_record())
					break;
				// EBR next starting sector = LBA address of next EBR minus LBA address of extended partition's first EBR
				// EBR next size starts counts from the next EBR:
				EBR_offset = mbrs.back().ptable[1].get_first_sec_by_LBA();
				// Note: CHS addresses are absolute, while LBA -- relative to the EBR start 
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

int whole_disk_t::process_volumes() {
	auto err_code = disks[0].process_bootsector(true);

	if (err_code != 0) {
		plugin_config.log_print("Warning# Error reading boot sector: %d", err_code);
		if (plugin_config.process_DOS1xx_images) {
			plugin_config.log_print("Info# Processing DOS1.xx image");
			err_code = disks[0].process_DOS1xx_image();
			if(err_code != 0)
				plugin_config.log_print("Warning# Erorr processing DOS1.xx image: %d", err_code);
		}
	}
	int first_err_code = 0;
	if (err_code != 0) {
		if (plugin_config.process_MBR) {
			plugin_config.log_print("Info# Processing MBR");
			err_code = process_MBR();
			if (!err_code) {
				// Single partition -- treat as a non-partitioned disk for viewing
				disks[0].set_boot_sector_offset(partition_info[0].first_sector * sector_size);
				plugin_config.log_print_dbg("Info# Processing partition 0, offset: 0x%010X", disks[0].get_boot_sector_offset());
				first_err_code = disks[0].process_bootsector(true);
				if(first_err_code != 0)
					plugin_config.log_print_dbg("Warning# Error processing partition 0: %d", first_err_code);
				else
					plugin_config.log_print("Info# Processed partition 0");

				for (size_t i = 1; i < partition_info.size(); ++i) {
					disks.emplace_back(this);
					disks.back().set_boot_sector_offset(partition_info[i].first_sector * sector_size);
					err_code = disks.back().process_bootsector(true);
					if (err_code != 0)
						plugin_config.log_print_dbg("Warning# Error processing partition %zd: %d", i, first_err_code);
					else
						plugin_config.log_print("Info# Processed partition %zd, offset: 0x%010X", i,
							disks[0].get_boot_sector_offset());
				}
				if (disks.empty() || (first_err_code != 0 && disks.size() == 1)) {
					err_code = E_UNKNOWN_FORMAT;
				}
				else {
					err_code = 0;
				}
			}
			else {
				err_code = E_UNKNOWN_FORMAT; //-V1048
			}
		}
	}

	// No partitions -- attempt to find boot sector
	if (err_code != 0) {
		if (plugin_config.search_for_boot_sector) {
			if (disks[0].search_for_bootsector() == 0) {
				plugin_config.log_print("Info# Searching for boot sector");
				err_code = disks[0].process_bootsector(true);
				if (err_code != 0)
					plugin_config.log_print_dbg("Warning# Error searching for boot sector: %d", err_code);
				else
					plugin_config.log_print("Info# Found boot sector at: 0x%010X", disks[0].boot_sector_offset);
			}
		}
	}

	return err_code;
}
//-----------------------=[ DLL exports ]=--------------------

extern "C" {
	// https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/set-invalid-parameter-handler-set-thread-local-invalid-parameter-handler?view=msvc-170
	// https://learn.microsoft.com/en-us/cpp/c-runtime-library/parameter-validation?view=msvc-170
	// https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/strcpy-s-wcscpy-s-mbscpy-s?view=msvc-170

	char drives[MAX_PATH] ; //open file pathes
	int disks_size = 0;

	PARTITION VolToPart[] = {
    {0, 1}, // partition 1 on drive 0
    {0, 2}, // partition 2 on drive 0
    {0, 3}, // partition 3 on drive 0
	{0, 4}, // partition 4 on drive 0
	{1, 0}
	};

	void myInvalidParameterHandler(const wchar_t* expression,
		const wchar_t* function,
		const wchar_t* file,
		unsigned int line,
		uintptr_t pReserved)
	{
		plugin_config.log_print("\n\nError# Invalid parameter detected in function: %s\n"
			"File: %s Line: %d\nExpression: %s\n", function, file, line, expression);
	}

	// OpenArchive should perform all necessary operations when an archive is to be opened
	DLLEXPORT archive_HANDLE STDCALL OpenArchive(tOpenArchiveData* ArchiveData)
	{
		//DEBUG PRINT, DON'T FORGET TO DELETE
		{
			std::string log_path = "D:\\log_file.txt";


			std::FILE* cf = std::fopen(log_path.data(), "a");
			std::filesystem::path some_path = ArchiveData->ArcName;
			std::string path_str = some_path.generic_string();
			const char* try_path = path_str.c_str();
			//std::ofstream cf{ config_file_path.data() };
			fprintf(cf, "[FAT_disk_img_plugin]\n");
			fprintf(cf, "[Open Archive] Called\n");
			fprintf(cf, "Version 1.1\n");
			fprintf(cf, "Archive Name: %s\n", try_path);
			std::fclose(cf);
		}

		plugin_config.log_print("\n\nInfo# Opening file: %s", ArchiveData->ArcName);

		auto rdconf = plugin_config.read_conf(nullptr, true); // Reread confuguration

		std::unique_ptr<whole_disk_t> arch; // TCmd API expects HANDLE/raw pointer,
		// so smart pointer is used to manage cleanup on errors 
		// only inside this function
//! Not used by TCmd yet.
		ArchiveData->CmtBuf = 0;
		ArchiveData->CmtBufSize = 0;
		ArchiveData->CmtSize = 0;
		ArchiveData->CmtState = 0;


		size_t image_file_size = get_file_size(ArchiveData->ArcName);
		auto hArchFile = open_file_shared_read(ArchiveData->ArcName);
		if (hArchFile == file_open_error_v)
		{
			ArchiveData->OpenResult = E_EOPEN;
			return nullptr;
		}
		try {
			arch = std::make_unique<whole_disk_t>( ArchiveData->ArcName, image_file_size,
				hArchFile, ArchiveData->OpenMode);
		}
		catch (std::bad_alloc&) {
			ArchiveData->OpenResult = E_NO_MEMORY;
			return nullptr;
		}

		arch->oldHandler = _set_invalid_parameter_handler(myInvalidParameterHandler);

		auto err_code = arch->process_volumes();

		int loaded_FATs = 0;
		size_t loaded_catalogs = 0;
		if (err_code == 0) {
			for (size_t i = 0; i < arch->disks.size(); ++i) {
				if (!arch->disks[i].is_known_FS_type())
					continue;
				err_code = arch->disks[i].load_FAT();
				if (err_code != 0 && loaded_FATs == 0) { // Saving the first error
					ArchiveData->OpenResult = err_code;
				}
				else {
					++loaded_FATs; //-V127
					err_code = arch->disks[i].load_file_list_recursively(minimal_fixed_string_t<MAX_PATH>{}, 0, 0);
					if (err_code != 0 && loaded_catalogs == 0) { // Saving the first error

						ArchiveData->OpenResult = err_code;
					}
					else {
						++loaded_catalogs;
					}
				}
			}
		}

		plugin_config.log_print("Info# Loaded FATs %d, of them -- catalogs: %zd", loaded_FATs, loaded_catalogs);

		if (loaded_catalogs > 0 || err_code == 0) { // Second condition -- disk has unknown partitions only
			//assume that the volume may be mounted with FATFs for now
			std::filesystem::path path = ArchiveData->ArcName;
			std::string temp_str = path.generic_string();
			strncpy(drives, temp_str.c_str(), MAX_PATH); //adds the file-folder path to the drives list - shared between dikio.c and fatimg_wcx.cpp

			disks_size = arch->disks.size();
			ArchiveData->OpenResult = 0; // OK
			return arch.release(); // Returns raw ptr and releases ownership 
		}
		else {
			ArchiveData->OpenResult = err_code;
			return nullptr;
		}
	}

	// TCmd calls ReadHeader to find out what files are in the archive
	DLLEXPORT int STDCALL ReadHeader(archive_HANDLE hArcData, tHeaderData* HeaderData)
	{
		auto& prev_current_disk = hArcData->disks[hArcData->disc_counter];
		if ( prev_current_disk.is_processed() ) { //-V104
			prev_current_disk.counter = 0;
			++hArcData->disc_counter;
			if (hArcData->disc_counter == hArcData->disks.size()) { //-V104
				hArcData->disc_counter = 0;
				return E_END_ARCHIVE;
			}
		}
		prev_current_disk.set_processed_for_empty();
		// get_disk_prefix
		auto& current_disk = hArcData->disks[hArcData->disc_counter];
		strcpy(HeaderData->ArcName, hArcData->archname.data());
		if (hArcData->disks.size() == 1) {
			if(!current_disk.arc_dir_entries.empty())
				strcpy(HeaderData->FileName, current_disk.arc_dir_entries[current_disk.counter].PathName.data());
			else
				return E_END_ARCHIVE;
		}
		else {
			auto disk_name = hArcData->get_disk_prefix(hArcData->disc_counter); // Case of empty or unknown partitions
			//! If disk empty -- put only dir with its name
			if ( !hArcData->disks[hArcData->disc_counter].is_known_FS_type() ) {
				disk_name.pop_back();
				disk_name.push_back("_Unknown");
			}
			else if( !hArcData->disks[hArcData->disc_counter].arc_dir_entries.empty() )
				disk_name.push_back(current_disk.arc_dir_entries[current_disk.counter].PathName);
			strcpy(HeaderData->FileName, disk_name.data());
		}
		if (!hArcData->disks[hArcData->disc_counter].arc_dir_entries.empty() &&
			hArcData->disks[hArcData->disc_counter].is_known_FS_type() )
		{
			HeaderData->FileAttr = current_disk.arc_dir_entries[current_disk.counter].FileAttr;
			HeaderData->FileTime = current_disk.arc_dir_entries[current_disk.counter].FileTime;
			// For files larger than 2Gb -- implement tHeaderDataEx
			HeaderData->PackSize = static_cast<int>(current_disk.arc_dir_entries[current_disk.counter].FileSize);
		}
		else { // Just disk dir
			HeaderData->FileAttr = FAT_attrib_t{ FAT_attrib_t::ATTR_DIRECTORY };
			HeaderData->FileTime = get_current_datetime();
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
		_set_invalid_parameter_handler(hArcData->oldHandler);
		delete hArcData;
		return 0; // OK
	}

	// This function allows you to notify user about changing a volume when packing files
	DLLEXPORT void STDCALL SetChangeVolProc(archive_HANDLE hArcData, tChangeVolProc pChangeVolProc)
	{
		hArcData->pLocChangeVol = pChangeVolProc; // Was commented in Nataliia code 
	}

	// This function allows you to notify user about the progress when you un/pack files
	DLLEXPORT void STDCALL SetProcessDataProc(archive_HANDLE hArcData, tProcessDataProc pProcessDataProc)
	{
		hArcData->pLocProcessData = pProcessDataProc; // Was commented in Nataliia code 
	}

	// PackSetDefaultParams is called immediately after loading the DLL, before any other function. 
	// This function is new in version 2.1. 
	// It requires Total Commander >=5.51, but is ignored by older versions.
	DLLEXPORT void STDCALL PackSetDefaultParams(PackDefaultParamStruct* dps) { //-V2009
		auto res = plugin_config.read_conf(dps, false);
		if (!res) { // Create default configuration if conf file is absent
			plugin_config.write_conf();
		}
	}

	// GetBackgroundFlags is called to determine whether a plugin supports background packing or unpacking.
	// BACKGROUND_UNPACK == 1 Calls to OpenArchive, ReadHeader(Ex), ProcessFile and CloseArchive are thread-safe 
#ifdef _WIN64
	DLLEXPORT int STDCALL GetBackgroundFlags(PackDefaultParamStruct* dps) {
		return BACKGROUND_PACK | BACKGROUND_UNPACK | BACKGROUND_MEMPACK;
	}
#endif 
	DLLEXPORT int STDCALL CanYouHandleThisFile(char* FileName) { // BOOL == int 
		size_t image_file_size = get_file_size(FileName);
		auto hArchFile = open_file_shared_read(FileName);
		if (hArchFile == file_open_error_v)
		{
			return 0;
		}
		// Caching results here would complicate code too much as for now
		whole_disk_t arch{ FileName, image_file_size,
				hArchFile, PK_OM_LIST };

		auto err_code = arch.process_volumes();
		int is_OK = (err_code != 0);
		return is_OK;
	}

	DLLEXPORT int STDCALL GetPackerCaps() {
		return PK_CAPS_NEW | PK_CAPS_MODIFY | PK_CAPS_DELETE | PK_CAPS_BY_CONTENT | PK_CAPS_SEARCHTEXT | PK_CAPS_MULTIPLE;
	}

	int PackDirectory(const std::string& hostPath, const std::string& fatPath) {

		std::string name;
		std::string srcFullPath;
		std::string dstFullPath;
		FIL dstFile;
		FILE* srcFile = nullptr;
		FRESULT fr;
		UINT bytesWritten;
		char buffer[4096];
		int overallResult = 0;


		fr = f_mkdir(fatPath.c_str());
		if (fr != FR_OK && fr != FR_EXIST) return E_ECREATE;

		try {
			for (const auto& entry : std::filesystem::directory_iterator(hostPath)) {
				name = entry.path().filename().string();
				srcFullPath = entry.path().string();
				dstFullPath = fatPath + "/" + name;

				if (entry.is_directory()) {
					int res = PackDirectory(srcFullPath, dstFullPath);
					if (res != 0) {
						overallResult = res;
						continue;
					}
				}
				else {
					srcFile = fopen(srcFullPath.c_str(), "rb");
					if (!srcFile) {
						overallResult = E_EOPEN;
						continue;
					}

					fr = f_open(&dstFile, dstFullPath.c_str(), FA_WRITE | FA_CREATE_ALWAYS);
					if (fr != FR_OK) {
						fclose(srcFile);
						overallResult = E_ECREATE;
						continue;
					}

					size_t bytesRead;
					bool errorOccurred = false;
					while ((bytesRead = fread(buffer, 1, sizeof(buffer), srcFile)) > 0) {
						fr = f_write(&dstFile, buffer, bytesRead, &bytesWritten);
						if (fr != FR_OK || bytesWritten != bytesRead) {
							errorOccurred = true;
							overallResult = E_EWRITE;
							break;
						}
					}

					fclose(srcFile);
					f_close(&dstFile);
					if (errorOccurred) {
						// Remove partially written file
						f_unlink(dstFullPath.c_str());
						continue;
					}
				}
			}
		}
		catch (const std::filesystem::filesystem_error& e) {
			// Handle filesystem-related errors
			
			return E_BAD_ARCHIVE;
		}
		catch (...) {
			// Catch any other unexpected errors
			return E_UNKNOWN_FORMAT;
		}
		return overallResult;
	}

	//// write-mode functions
	DLLEXPORT int STDCALL PackFiles(char* PackedFile, char* SubPath, char* SrcPath, char* AddList, int Flags) {

		//DEBUG PRINT, DON'T FORGET TO DELETE
		{
			std::string log_path = "D:\\log_file.txt";


			std::FILE* cf = std::fopen(log_path.data(), "a");

			//std::ofstream cf{ config_file_path.data() };
			if (!cf) {
				return false;
			}
			fprintf(cf, "[FAT_disk_img_plugin]\n");
			fprintf(cf, "[PackFiles] Called with:\n");
			fprintf(cf, "  PackedFile : %s\n", PackedFile ? PackedFile : "NULL");
			fprintf(cf, "  SubPath    : %s\n", SubPath ? SubPath : "NULL");
			fprintf(cf, "  SrcPath    : %s\n", SrcPath ? SrcPath : "NULL");
			fprintf(cf, "  AddList    : %s\n", AddList ? AddList : "NULL");
			fprintf(cf, "  Flags      : %d\n", Flags);
			fprintf(cf, "Returning : 0\n");
			std::fclose(cf);
		}

		FATFS fs;
		FRESULT fr;
		int logical_drive_number = 4;
		char drive_letter;

		if (disks_size >= 2) {
			if (SubPath != NULL) {
				drive_letter = toupper(SubPath[0]);

				if (drive_letter >= 'C' && drive_letter <= 'F') {
					logical_drive_number = drive_letter - 'C';
				}
				else {
					return E_ECLOSE;
				}
			}
			else {
				if (AddList != NULL && std::strlen(AddList) > 1) {
					drive_letter = toupper(AddList[0]);

					if (drive_letter >= 'C' && drive_letter <= 'F' && AddList[1] == '\\') {
						logical_drive_number = drive_letter - 'C';
					}
					else {
						return E_ECLOSE;
					}
				}
				else {
					return E_ECLOSE;
				}
			}
		}
		std::string lv_drv_num = std::string(1, '0' + logical_drive_number) + ":";
		fr = f_mount(&fs, lv_drv_num.c_str(), 1);
		if (fr != FR_OK) return E_UNKNOWN_FORMAT;
		try {

			for (char* current = AddList; current && *current != '\0'; current += std::strlen(current) + 1) {



				std::string srcFullPath = std::string(SrcPath) + current;

				if (disks_size >= 2) {
					if (current[1] == '\\' && current[0] >= 'C' && current[0] <= 'F') {
						current += 2;
					}
				}

				std::string targetPath;
				if (SubPath && std::strlen(SubPath) > 0) {
					targetPath = std::string(SubPath) + "/" + current;

					if (disks_size >= 2) {
						if (targetPath.size() >= 2 && targetPath[1] == '\\') {
							targetPath = targetPath.substr(2);
						}
					}
					targetPath = lv_drv_num + "/" + targetPath;
				}
				else {
					targetPath = lv_drv_num + "/" + std::string(current);
				}

				DWORD fileAttributes = GetFileAttributes(srcFullPath.c_str());
				if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
					continue;  // Couldn't get file attributes
				}

				if (fileAttributes & FILE_ATTRIBUTE_DIRECTORY) {  // If it's a dir
					// Create a dir in FAT img

					int res = PackDirectory(srcFullPath, targetPath);
					if (res != 0) {
						// Directory packing failed
						f_mount(nullptr, lv_drv_num.c_str(), 0);
						return res;
					}
					continue;
				}

				FILE* srcFile = std::fopen(srcFullPath.data(), "rb");
				if (!srcFile) {
					// Cannot open source file
					f_mount(nullptr, lv_drv_num.c_str(), 0);
					return E_EOPEN;
				}

				FIL dstFile;
				fr = f_open(&dstFile, targetPath.c_str(), FA_WRITE | FA_CREATE_ALWAYS);
				if (fr != FR_OK) {
					fclose(srcFile);
					f_mount(nullptr, lv_drv_num.c_str(), 0);
					return E_BAD_ARCHIVE;
				}



				char buffer[4096];
				size_t bytesRead;
				UINT bytesWritten;
				bool errorOccurred = false;

				while ((bytesRead = fread(buffer, 1, sizeof(buffer), srcFile)) > 0) {
					fr = f_write(&dstFile, buffer, bytesRead, &bytesWritten);
					if (fr != FR_OK || bytesWritten != bytesRead) {
						// Write error occurred
						errorOccurred = true;
						break;
					}
				}

				fclose(srcFile);
				f_close(&dstFile);

				if (errorOccurred) {
					f_unlink(targetPath.c_str());
					f_mount(nullptr, lv_drv_num.c_str(), 0);
					return E_EWRITE;
				}
			}
		}
		catch (...) {
			// Unmount filesystem in case of any unexpected errors
			f_mount(nullptr, lv_drv_num.c_str(), 0);
			return E_UNKNOWN_FORMAT;
		}

		f_mount(nullptr, lv_drv_num.c_str(), 0);

		return 0;
	}
		
	DLLEXPORT int STDCALL DeleteFiles(char *PackedFile, char *DeleteList) {

		//DEBUG PRINT, DON'T FORGET TO DELETE
		std::string log_path = "D:\\log_file.txt";


		std::FILE* cf = std::fopen(log_path.data(), "a");

		//std::ofstream cf{ config_file_path.data() };
		if (!cf) {
			return false;
		}
		fprintf(cf, "[FAT_disk_img_plugin]\n");
		fprintf(cf, "[DeleteFiles] Called with:\n");
		fprintf(cf, "  PackedFile : %s\n", PackedFile ? PackedFile : "NULL");
		fprintf(cf, "  DeleteList : %s\n", DeleteList ? DeleteList : "NULL");
		fprintf(cf, "Returning : 0\n");
		std::fclose(cf);

		int logical_drive_number = 4;

		if (disks_size >= 2) {

			if (DeleteList && std::strlen(DeleteList) >= 2) {
				char drive_letter = toupper(DeleteList[0]);

			    if (drive_letter >= 'C' && drive_letter <= 'F' && DeleteList[1] == '\\') {
				   logical_drive_number = drive_letter - 'C';
			    }
			    else {
				  fprintf(cf, "Invalid drive prefix in DeleteList: %c\n", DeleteList[0]);
				  std::fclose(cf);
				  return E_ECLOSE;
			     }
			}
			else {
				// to not delete a disk
				fprintf(cf, "DeleteList does not specify a valid drive or path\n");
				std::fclose(cf);
				return E_ECLOSE;
			}
		}
		FATFS fs;
		std::string lv_drv_num = std::string(1, '0' + logical_drive_number) + ":";

		FRESULT fr = f_mount(&fs, lv_drv_num.c_str(), 1);
		if (fr != FR_OK) {
			return E_NOT_SUPPORTED;
		}

		auto recursive_del = [&](const char* path, auto&& recursive_del_ref) -> FRESULT {
			DIR dir;
			FILINFO fno;
			FRESULT res = f_opendir(&dir, path);
			if (res != FR_OK) return res;

			while (true) {
				res = f_readdir(&dir, &fno);
				if (res != FR_OK || fno.fname[0] == 0) break;

				if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0) continue;

				std::string fullPath = std::string(path) + "/" + fno.fname;
				if (fno.fattrib & AM_DIR) {
					// Recurse into subdirectory
					res = recursive_del_ref(fullPath.c_str(), recursive_del_ref);
					if (res != FR_OK) return res;
				}
				else {
					res = f_unlink(fullPath.c_str());
					if (res != FR_OK) return res;
				}
			}
			f_closedir(&dir);

			// Delete the now-empty directory
			return f_unlink(path);
			};


		//implementing multiple files delete:

		bool anyFailed = false;

		char* current = DeleteList;

		while (*current != '\0') {
			std::string deletePath = current;
			fprintf(cf, "  DeleteList entry: %s\n", deletePath.c_str());

			if (disks_size >= 2) {

				if (deletePath.size() >= 2 && deletePath[1] == '\\') {
					deletePath = deletePath.substr(2);
				}
			}

			if (deletePath.size() >= 2) {
				bool isDirectoryDelete = false;

				if (deletePath.size() >= 3 && deletePath.substr(deletePath.size() - 3) == "*.*") {
					isDirectoryDelete = true;
				}

				// If it's a directory delete command, truncate the path
				if (isDirectoryDelete) {
					size_t lastSlash = deletePath.find_last_of("\\/");
					if (lastSlash != std::string::npos) {
						deletePath = deletePath.substr(0, lastSlash);
					}
				}
			}

			deletePath = lv_drv_num + "/" + deletePath;

			FILINFO info;
			fr = f_stat(deletePath.c_str(), &info);
			if (fr != FR_OK) {
				fprintf(cf, "f_stat failed on %s\n", DeleteList);
				std::fclose(cf);
				return E_ECLOSE;
			}

			if (info.fattrib & AM_DIR) {
				fr = recursive_del(deletePath.c_str(), recursive_del); // if it's a dir del recursive
			}
			else {
				fr = f_unlink(deletePath.c_str()); // unlink the file
			}



			if (fr != FR_OK) {
				// Log failure and return an error code
				fprintf(cf, "Failed to delete: %s\n", deletePath.c_str());
				anyFailed = true;
			}
			else {
				// Log success for each deleted file
				fprintf(cf, "Successfully deleted file: %s\n", deletePath.c_str());
			}
			current += strlen(current) + 1; // move onto next file
		}

		f_mount(nullptr, lv_drv_num.c_str(), 0);
		fprintf(cf, "Returning : %d\n", anyFailed ? E_ECLOSE : 0);
		std::fclose(cf);

		return anyFailed ? E_ECLOSE : 0;

	}

	//DLLEXPORT void STDCALL ConfigurePacker(HWND Parent, HINSTANCE DllInstance) {
	//	std::string log_path = "E:\\log_file.txt";


	//	std::FILE* cf = std::fopen(log_path.data(), "a");

	//	//std::ofstream cf{ config_file_path.data() };
	//	fprintf(cf, "[FAT_disk_img_plugin]\n");
	//	fprintf(cf, "[ConfigurePacker] Called with :\n  PackedFile : \n  DeleteList :\n  Returning : E_NOT_SUPPORTED\n");
	//	std::fclose(cf);
	//};

}

#if 0 // FLTK Dialogs:
Fl_Window* w = new Fl_Window(400, 300);
w->set_modal();
w->show();
while (w->shown()) Fl::wait();

https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setparent -- 
Changes the parent window of the specified child window.
HWND SetParent(
	[in]           HWND hWndChild,
	[in, optional] HWND hWndNewParent
);


FLTK windows as children of system windows: "replaces WS_POPUP with WS_CHILD flags"
https ://fltk.easysw.narkive.com/FJwMvdmO/windows-as-children-of-system-windows
	HWND hWnd = (HWND)fl_xid(dlgWnd_);
SetWindowLong(hWnd, GWL_STYLE, (GetWindowLong(hWnd, GWL_STYLE) & ~WS_POPUP) | WS_CHILD);
SetParent(hWnd, parentWindow);

https://www.fltk.org/doc-1.3/osissues.html#osissues_win32
See "Handling Other WIN32 Messages"

Fl_Window* fl_find(HWND xid)
Returns the Fl_Window that corresponds to the given window handle, or NULL if not found.
This function uses a cache so it is slightly faster than iterating through the windows yourself.
#endif
