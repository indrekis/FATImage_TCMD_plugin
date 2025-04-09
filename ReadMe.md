Introduction
============

This student project is an attempt to implement write-mode support for the FATImage_TCMD_plugin, a Total Commander plugin designed to work with FAT-formatted disk images. 
While the original plugin provided read-only access, this extended version aims to enable the creation, modification, and addition of files and directories within FAT images.
The implementation is based on the FATFs library, allowing for structured and low-level manipulation of FAT file systems.

The original plugin is available here: https://github.com/indrekis/FATImage_TCMD_plugin
It provides functionality for browsing and extracting files from FAT12/16/32 disk images within Total Commander.

FATImage is a wcx (archive) plugin for 64-bit and 32-bit Total Commander (TCmd) 
supporting access to the floppy disk images and FAT images in general, including partitioned ones.
> There are several such good plugins for the 32-bit TCmd, but none (to the best of my knowledge) for the 64-bit TCmd. 
> > IMG Plugin for Total Commander (TCmd) by IvGzury was used as a starting point because its sources were available.

Supports: FAT12, FAT16, FAT32, VFAT.

Partial Unicode support for the FAT32 -- UCS-16 symbols in LFNs are converted to the local codepage.

Supports DOS 1.xx images without BPB. 
> Support is based on the media descriptor in the FAT table and image size. 
> 
>Supports several optional exceptions for the popular quirks of historical disk images from the retro sites.

Supports MBR-based partitions. Volumes with unknown or unsupported filesystems are shown as such.

The plugin supports searching for the boot sector in the image. This is intended to help open images containing some metadata at the beginning, added by imaging tools. 
> WinImage demonstrates the same behavior. The boot sector for this search is determined by the following pattern of size 512 bytes exactly: 
>
> `0xB8, 0xx, 0x90, 0xx ... 0xx, 0x55, 0xAA`
>
> where 0xx means any byte. 

Additional information in the author's blog (in Ukrainian): "[Анонс -- 64-бітний плагін Total Commander для роботи із образами FAT](https://indrekis2.blogspot.com/2022/08/64-total-commander-fat.html)" 

Installation
============

As usual for the TCmd plugins:
* Manually:
	1. Unzip the WCX to the directory of your choice (for example, c:\wincmd\plugins\wcx)
	2. In TCmd, choose Configuration - Options
	3. Open the 'Packer' page
	4. Click 'Configure packer extension WCXs'
	5. Type 'img' as the extension
	6. Click 'new type', and select the img.wcx64 (img.wcx for 32-bit TCmd)
	7. Click OK
* Using automated install:
	1. Open the archive with the plugin in TCmd -- it will propose to install the plugin. 

Configuration is stored in the `fatdiskimg.ini` file in the configuration path provided by the TCmd (usually -- the same place where wincmd.ini is located). If the configuration file is damaged or absent, it is recreated with default values.

Works in Double Commander for Windows.

Binary releases available [here](https://github.com/indrekis/FDDImage_TCMD_plugin/releases).

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

Plugin configuration
====================

The configuration file, named fatdiskimg.ini, is searched at the path provided by the TCmd (most often -- the path where wincmd.ini is located). If the configuration file is absent or incorrect, it is created with the default configuration.

Except for the logging options, currently, the plugin rereads the configuration before opening each image (archive).

Example configuration file:
```
[FAT_disk_img_plugin]
ignore_boot_signature=1
use_VFAT=1
process_DOS1xx_images=1
process_DOS1xx_exceptions=1
process_MBR=1
search_for_boot_sector=1
search_for_boot_sector_range=65536
allow_dialogs=0
allow_GUI_log=1
log_file_path=D:\Temp\fatimg.txt
debug_level=0
```

* `ignore_boot_signature`. If the first 512-byte sector of the image does not contain 0x55AA and ignore_boot_signature == 0, the image is treated as not a FAT volume, even if the BPB data looks consistent.
* `use_VFAT` -- if 1, long file names are processed (regardless of the FAT type).
* `process_DOS1xx_images == 1` allows to interpret image with exact size, equal to: 160Kb, 180Kb, 320Kb, or 360Kb as a pre-BPB FAT image. Such images mostly came from the DOS 1.xx epoch disks. Disk format is then detected based on the [Media descriptor byte](https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system#FATID) -- the first byte of the FAT, which is considered situated in the first byte of the second image sector. 
  * It is a much less reliable format detection criterion than the full BPB, so enabling this option can interfere with the normal image processing, though the author never met with such a situation.
  * MC/PC-DOS versions 2.00-3.20 [sometimes created bad BPBs](http://www.os2museum.com/wp/dos-boot-sector-bpb-and-the-media-descriptor-byte/), making the media descriptors the main source of disk format information.
* process_DOS1xx_exceptions=1 -- enables DOS 1.xx non-BPB images processing exceptions targeted at supporting the popular quirks of historical disk images from the retro sites. Not recommended for typical users.
* `process_MBR == 1` -- if the disk is not a correct non-partitioned disk image, try to interpret it as an MBR-based partitioned image.
* `search_for_boot_sector==1` -- if the disk image does not contain the correct boot sector or the MBR at the beginning, attempt to find the boot sector in the image file by the pattern `0xB8, 0xx, 0x90, 0xx ... 0xx, 0x55, 0xAA`, where 0xx means any byte. This helps to open images that contain some meta information before the image, but the image itself is not altered. Search for the MBR is not supported, but if there is a partitioned image after the metainformation, it can find the first FAT partition of the image. Not recommended for typical users.
* `search_for_boot_sector_range==65536` -- range of the bootsector search, in bytes. The default value is 64kb. It is not recommended to increase this value substantially beyond this value. 
   * Not only does it slow down working with the images, but it can also lead to false detections. 
   * For example, utilities like `fdisk`, `sys`, or `format` can contain bootsector copies required for them to perform their duties.
* `allow_dialogs==1` -- if FLKT support is enabled while building, enable dialogs on some image peculiarities, like the absence of the 0x55AA signature.
* `allow_txt_log==1` -- allows detailed log output, describing imager properties and anomalies. It can noticeably slow down the plugin. Not for general use, can lead to problems. It is useful for in-depth image analysis.  
  * Additionally, for the debug builds (without NDEBUG defined), important image analysis events are logged to the debugger console. In addition to using a full-fledged debugger, debugging output can be seen using [SimpleProgramDebugger](http://www.nirsoft.net/utils/simple_program_debugger.html).
* `log_file_path=<filename>` -- logging filename. If opening this file for writing fails, logging is disabled (allow_txt_log==0). The file is created from scratch at the first use of the plugin during the current TCmd session. 
* `debug_level` -- not used now. 
* `max_depth` -- maximum depth of the directory tree to be traversed. 
* `max_invalid_chars_in_dir` -- maximum number of invalid characters in the directory name. If the number of invalid characters exceeds this value, the directory is not opened and is presented as empty. Useful for the corrupted images. 
  * Value above 11 effectively disables this check.

Compilation
===========

Code can be compiled using the Visual Studio project or CMakeLists.txt (tested using MSVC and MinGW). 

Uses C++20, with no obligatory external dependencies. 

As an experimental and optional feature, [FLTK](https://www.fltk.org/) is used to create dialogs. Using it can lead to instabilities because of multi-threading issues, so this feature is not for the general public.

Examples of the command lines to compile using CMake are in the CMakeLists.txt.

Preparing images for tests
==========================

The plugin was tested using two kinds of images:
* Images of virtual machines and emulators used for different tasks.
* Historical floppy images from the retro-computing sites: [WinWorld](https://winworldpc.com/home), [BetaArchive](https://www.betaarchive.com/database/browse.php), [Old-DOS](http://old-dos.ru/), [VETUSWARE](https://vetusware.com/), [Bitsavers](http://www.bitsavers.org/bits/)

The plugin is tested on over a thousand floppy images and dozens of HDD images, including custom-made ones with up to eleven partitions.

## Linux
Linux loopback devices are a convenient approach to making test images. 

### Example 1 -- partitioned image 

Create an image 1 GB in size: 

`dd if=/dev/zero of=image_file.img bs=100M count=10`

Create a  loopback device, connected to this file:

`sudo losetup -fP image_file.img`

Partition disk (put your device name in place of loop0):

`sudo fdisk /dev/loop0`

To check existing loopback devices: 

`losetup -a`

On using fdisk, see [Partitioning with fdisk HOWTO](https://tldp.org/HOWTO/Partition/fdisk_partitioning.html) and [fdisk(8) man](https://man7.org/linux/man-pages/man8/fdisk.8.html).

After the partitioning, loopback devices for partitions would have names like: 

`/dev/loop0p1 /dev/loop0p2 /dev/loop0p3 /dev/loop0p4 /dev/loop0p5 /dev/loop0p6 /dev/loop0p7 /dev/loop0p8 /dev/loop0p9 /dev/loop0p10 /dev/loop0p11`

Remember the special role of loop0p4 -- it is an extended partition, containing loop0p5 and so on.

To create FAT12/FAT16/FAT32/, use the following commands, respectively:

```
sudo mkfs.fat -F 12 /dev/loop0p9
sudo mkfs.fat -F 16 /dev/loop0p10
sudo mkfs.fat -F 32 /dev/loop0p11
```

Remember to use the correct partition device. More details: [dosfstools](https://github.com/dosfstools/dosfstools)

To populate image partitions with the files, one can mount it and use it as any other Linux volume:

```
mkdir ~/virtual_disk1
sudo mount -o loop /dev/loop0p10 ~/virtual_disk1
```

Lately, unmount it by: 

`sudo umount ~/virtual_disk1`

`sudo losetup -d /dev/loop0`

The exact command lines could depend on your Linux distribution.

### Example 1 -- nonpartitioned floppy image 

```
dd if=/dev/zero of=floppy_144.img bs=1K count=1440
sudo losetup -fP floppy_144.img
sudo mkfs.fat -F 12 /dev/loop0
mkdir ./fdd
sudo mount -o loop ./fdd  /dev/loop0

<copy files>

sudo umount ./fdd
sudo losetup -d /dev/loop0
```

### Other

Remove loopback device (put your device name in place of loop0):
`sudo losetup -d /dev/loop0`

To create an ext4/3/2 filesystem, use:

```
sudo mkfs.ext4 /dev/loop0p2
sudo mkfs.ext3 /dev/loop0p3
sudo mkfs.ext2 /dev/loop0p5
```

To create NTFS or exFAT: 

```
sudo mkfs.ntfs /dev/loop0p10
sudo mkfs.exfat /dev/loop0p10
```

To check the properties of the mounted loopback partition:

`df -hP  ~/virtual_disk1`

## Windows

Under MS Windows, free (as a beer) [OSFMount](https://www.osforensics.com/tools/mount-disk-images.html) can be used to mount single-partition and multi-partition images. Less capable but open-source similar tool -- [ImDisk](https://sourceforge.net/projects/imdisk-toolkit/).

Other imaging tools used during the tests were famous [WinImage](https://www.winimage.com/) and less known, but free (as a beer), DiskExplorer by junnno (no known site, can be downloaded [here](https://vetusware.com/download/Disk%20Explorer%201.69E/?id=16440)).


Problems and limitations
========================
* Read-only.
* Does not support exFAT.
* No GPT support.
* Does not yet support CHS partitions -- only LBA. 
  * The absolute majority of HDDs created in the 1990s and later are LBA.
* Partial support of Unicode in filenames, the code implements only ANSI functions for now.
  * As a result, the path is limited to 260-1 symbols max.
* No support for the 8" images, including 86-DOS and CP/M images.
* Directories data, time, and some attributes are not set properly.
* Not yet tested on large (close to 2 TB) images.
* 32-bit plugin version does not support background operation -- TCmd crashes or hangs every time the plugin is used if they are allowed.

                        
