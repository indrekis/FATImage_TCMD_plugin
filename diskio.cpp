// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include <map>
//#include <mutex>
#include "minimal_fixed_string.h"
#include "sysio_winapi.h"
#include "plugin_config.h"

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */


struct disk_descriptor_t {
    FILE* file;          
    minimal_fixed_string_t<MAX_PATH> PathName;
};

//std::mutex disk_deskriptors_mux;
std::map<BYTE, disk_descriptor_t> disk_descriptors; 


extern "C" {
    /* Definitions of physical drive number for each drive */
#define DEV_RAM		0	/* Example: Map Ramdisk to physical drive 0 */
#define DEV_MMC		1	/* Example: Map MMC/SD card to physical drive 1 */
#define DEV_USB		2	/* Example: Map USB MSD to physical drive 2 */

    /*-----------------------------------------------------------------------*/
    /* Inidialize a Drive                                                    */
    /*-----------------------------------------------------------------------*/

    DSTATUS disk_initialize(
        BYTE pdrv,				/* Physical drive nmuber to identify the drive */
        const char* image_path
    )
    {
        plugin_config.log_print_dbg("Info# Initializing disk %d, for filename %s, in disk_initialize",
            pdrv, image_path);

        if (disk_descriptors.count(pdrv) > 0) {
            plugin_config.log_print_dbg("Warning# in disk_initialize, disk %d, for filename %s, already opened",
                pdrv, image_path);
            return STA_PROTECT; 
        }

        disk_descriptors[pdrv] = { nullptr, image_path};

        FILE* fp = fopen(image_path, "rb+");
        disk_descriptors[pdrv] = { fp, image_path };

        if (!fp) {
            plugin_config.log_print_dbg("Warning# in disk_initialize, failed to opend image %s",
                image_path);
            return STA_NOINIT;
        }

        return RES_OK;
    }


    /*-----------------------------------------------------------------------*/
    /* Get Drive Status                                                      */
    /*-----------------------------------------------------------------------*/

    DSTATUS disk_status(
        BYTE pdrv		/* Physical drive nmuber to identify the drive */
    )
    {
        plugin_config.log_print_dbg("Info# disk_status called for the disk %d.", pdrv);

        if (disk_descriptors.count(pdrv) == 0) {
            plugin_config.log_print_dbg("Warning# in disk_status, disk %d -- no such driver.", pdrv);
            return RES_PARERR;
        }

        if (!disk_descriptors[pdrv].file) {
            plugin_config.log_print_dbg("Warning# in disk_status, disk %d -- image \'%s\' is not opened.",
                pdrv, disk_descriptors[pdrv].PathName.data());
            return RES_NOTRDY;
        }

        return RES_OK;
    }


    /*-----------------------------------------------------------------------*/
    /* Read Sector(s)                                                        */
    /*-----------------------------------------------------------------------*/

    DRESULT disk_read(
        BYTE pdrv,		/* Physical drive nmuber to identify the drive */
        BYTE* buff,		/* Data buffer to store read data */
        LBA_t sector,	/* Start sector in LBA */
        UINT count		/* Number of sectors to read */
    )
    {
        if (disk_descriptors.count(pdrv) == 0) {
            plugin_config.log_print_dbg("Warning# in disk_read, disk %d -- no such driver.", pdrv);
            return RES_PARERR;
        }

        FILE* fp = disk_descriptors[pdrv].file;
        if (!fp) {
            plugin_config.log_print_dbg("Warning# in disk_read, disk %d -- image \'%s\' is not opened.",
                pdrv, disk_descriptors[pdrv].PathName.data());
            return RES_NOTRDY;
        }
        if (fseek(fp, sector * FF_MIN_SS, SEEK_SET) != 0) {
            perror("fseek failed");
            //fclose(fp);
            return RES_ERROR;
        }

        if ( fseek(fp, sector * FF_MIN_SS, SEEK_SET) != 0) {
            int cur_err = errno;
            plugin_config.log_print_dbg("Warning# in disk_read, disk %d -- image \'%s\' seek error, errno = %d, \'%s\'.",
                pdrv, disk_descriptors[pdrv].PathName.data(), cur_err, strerror(cur_err) );
            return RES_ERROR;
        }
        auto read_s = fread(buff, FF_MIN_SS, count, fp); 
        if (read_s != count) {
            int cur_err = errno;
            plugin_config.log_print_dbg("Warning# in disk_read, disk %d -- image \'%s\' read error, errno = %d, \'%s\'."
                " Requested %d sectors, read %d",
                pdrv, disk_descriptors[pdrv].PathName.data(), cur_err, strerror(cur_err), read_s, count);
            return RES_ERROR;
        }

        return RES_OK;
    }



    /*-----------------------------------------------------------------------*/
    /* Write Sector(s)                                                       */
    /*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

    DRESULT disk_write(
        BYTE pdrv,			/* Physical drive nmuber to identify the drive */
        const BYTE* buff,	/* Data to be written */
        LBA_t sector,		/* Start sector in LBA */
        UINT count			/* Number of sectors to write */
    )
    {
        DRESULT res;
        if (disk_descriptors.count(pdrv) == 0) {
            plugin_config.log_print_dbg("Warning# in disk_write, disk %d -- no such driver.", pdrv);
            return RES_PARERR;
        }

        FILE* fp = disk_descriptors[pdrv].file;
        if (!fp) {
            plugin_config.log_print_dbg("Warning# in disk_write, disk %d -- image \'%s\' is not opened.",
                pdrv, disk_descriptors[pdrv].PathName.data());
            return RES_NOTRDY;
        }

        if (fseek(fp, sector * FF_MIN_SS, SEEK_SET) != 0) {
            int cur_err = errno;
            plugin_config.log_print_dbg("Warning# in disk_write, disk %d -- image \'%s\' seek error, errno = %d, \'%s\'.",
                pdrv, disk_descriptors[pdrv].PathName.data(), cur_err, strerror(cur_err));
            return RES_ERROR;
        }

        auto written_s = fwrite(buff, FF_MIN_SS, count, fp);
        if (written_s != count) {
            int cur_err = errno;
            plugin_config.log_print_dbg("Warning# in disk_read, disk %d -- image \'%s\' read error, errno = %d, \'%s\'."
                " Requested %d sectors, read %d",
                pdrv, disk_descriptors[pdrv].PathName.data(), cur_err, strerror(cur_err), written_s, count);
            return RES_ERROR;
        }

        res = RES_OK;
        return res;
    }

#endif


    /*-----------------------------------------------------------------------*/
    /* Miscellaneous Functions                                               */
    /*-----------------------------------------------------------------------*/

    DRESULT disk_ioctl(
        BYTE pdrv,		/* Physical drive nmuber (0..) */
        BYTE cmd,		/* Control code */
        void* buff		/* Buffer to send/receive control data */
    )
    {
        // Implement flush on CTRL_SYNC? 
        return RES_OK;
    }

    DRESULT disk_deinitialize(const char* name_in) {
        plugin_config.log_print_dbg("Info# disk_deinitialize: \'%s\'.", name_in);
        for (auto& [id, descr] : disk_descriptors) {
            if ( strcmp(descr.PathName.data(), name_in) == 0 ) {
                fclose(descr.file);
                disk_descriptors.erase(id);
                return RES_OK;
            }
        }
        plugin_config.log_print_dbg("Warning# disk_deinitialize failed for: \'%s\'.", name_in);
        return RES_ERROR;
    }


    /*-------------------------------------------------------------------*/
    /* User Provided RTC Function for FatFs module                       */
    /*-------------------------------------------------------------------*/
    /* This is a real time clock service to be called from FatFs module. */
    /* This function is needed when FF_FS_READONLY == 0 and FF_FS_NORTC == 0 */

    DWORD get_fattime(void)
    {
        return get_current_datatime_for_FatFS();
    }

}

