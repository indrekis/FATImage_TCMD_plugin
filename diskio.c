/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */



/* Definitions of physical drive number for each drive */
#define DEV_RAM		0	/* Example: Map Ramdisk to physical drive 0 */
#define DEV_MMC		1	/* Example: Map MMC/SD card to physical drive 1 */
#define DEV_USB		2	/* Example: Map USB MSD to physical drive 2 */

// TODO: Fix to be reentrant and thread-safe
FILE* fp = NULL;

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/


DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	DSTATUS stat;
	int result;

    //printf("calling disk_status successful\n");

    result = RES_OK;
    stat = 0x00;
    return stat;

//	switch (pdrv) {
//	case DEV_RAM :
//		result = RAM_disk_status();
//
//		// translate the reslut code here
//
//		return stat;
//
//	case DEV_MMC :
//		result = MMC_disk_status();
//
//		// translate the reslut code here
//
//		return stat;
//
//	case DEV_USB :
//		result = USB_disk_status();
//
//		// translate the reslut code here
//
//		return stat;
//	}
//	return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	DSTATUS stat = RES_OK;
	int result;
    fp = fopen(drives, "rb+");
    if (!fp) {
        perror("fopen failed");
        return STA_NOINIT;
	}

    //printf("calling disk_initialize successful\n");

    result = RES_OK;
    return stat;
//	switch (pdrv) {
//	case DEV_RAM :
//		result = RAM_disk_initialize();
//
//		// translate the reslut code here
//
//		return stat;
//
//	case DEV_MMC :
//		result = MMC_disk_initialize();
//
//		// translate the reslut code here
//
//		return stat;
//
//	case DEV_USB :
//		result = USB_disk_initialize();
//
//		// translate the reslut code here
//
//		return stat;
//	}
//	return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	DRESULT res;
	int result;

    //printf(">>> disk_read called\n");
    //printf("  pdrv   = %d\n", pdrv);
    //printf("  sector = %llu\n", (unsigned long long)sector);
    //printf("  count  = %u\n", count);

    // fp = fopen(drives, "rb");
    // int tt = errno; 
    if (fp == NULL) {
        printf("Error: Failed to open file %s\n", drives);
        return RES_ERROR;
    }
    fseek(fp, sector * FF_MIN_SS, SEEK_SET);
    fread(buff, FF_MIN_SS, count, fp);
    //fclose(fp);
    
    result = RES_OK;
    return result;
//	switch (pdrv) {
//	case DEV_RAM :
//		// translate the arguments here
//
//		result = RAM_disk_read(buff, sector, count);
//
//		// translate the reslut code here
//
//		return res;
//
//	case DEV_MMC :
//		// translate the arguments here
//
//		result = MMC_disk_read(buff, sector, count);
//
//		// translate the reslut code here
//
//		return res;
//
//	case DEV_USB :
//		// translate the arguments here
//
//		result = USB_disk_read(buff, sector, count);
//
//		// translate the reslut code here
//
//		return res;
//	}
//
//	return RES_PARERR;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	DRESULT res;
	int result;

    //printf(">>> disk_write called\n");
    //printf("  pdrv   = %d\n", pdrv);
    //printf("  sector = %llu\n", (unsigned long long)sector);
    //printf("  count  = %u\n", count);

    // FILE* fp = fopen(drives, "rb+");
    if (!fp) {
        perror("fopen failed");
        return RES_PARERR;
    }

    if (fseek(fp, sector * FF_MIN_SS, SEEK_SET) != 0) {
        perror("fseek failed");
        //fclose(fp);
        return RES_ERROR;
    }

    size_t written = fwrite(buff, FF_MIN_SS, count, fp);
    if (written != count) {
        fprintf(stderr, "fwrite error: expected %u sectors, wrote %zu\n", count, written);
        //fclose(fp);
        return RES_ERROR;
    }

    //fclose(fp);

    result = RES_OK;
    return result;
//	switch (pdrv) {
//	case DEV_RAM :
//		// translate the arguments here
//
//		result = RAM_disk_write(buff, sector, count);
//
//		// translate the reslut code here
//
//		return res;
//
//	case DEV_MMC :
//		// translate the arguments here
//
//		result = MMC_disk_write(buff, sector, count);
//
//		// translate the reslut code here
//
//		return res;
//
//	case DEV_USB :
//		// translate the arguments here
//
//		result = USB_disk_write(buff, sector, count);
//
//		// translate the reslut code here
//
//		return res;
//	}
//
//	return RES_PARERR;
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
    DRESULT res;
    int result;

    res = RES_OK;

    //	switch (pdrv) {
    //	case DEV_RAM :
    //
    //		// Process of the command for the RAM drive
    //
    //		return res;
    //
    //	case DEV_MMC :
    //
    //		// Process of the command for the MMC/SD card
    //
    //		return res;
    //
    //	case DEV_USB :
    //
    //		// Process of the command the USB drive
    //
    //		return res;
    //	}

    return res;
}



