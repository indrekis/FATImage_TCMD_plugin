Introduction
============

This project is intended to create a wcx (archive) plugin for 64-bit Total Commander (TCmd) 
supporting access to the floppy disk images and FAT images in general.
There are several such good plugins for the 32-bit TCmd, but none (to the best of my knowledge) for the 64-bit TCmd. 
IMG Plugin for Total Commander (TCmd) by IvGzury was used as a starting point because its sources were available.

Supports: FAT12, FAT16, FAT32, VFAT.

Partial Unicode support for the FAT32 -- UCS-16 symbols in LFNs are converted to the local codepage.

Supports DOS 1.xx images without BPB. Support is based on the media descriptor in the FAT table and image size. Added several exceptions 
for the popular quirks of historical disk images from the retro sites.

Supports MBR-based partitions. Volumes with unknown filesystems are shown as such.

The plugin supports searching for the boot sector in the image. This is intended to help open images containing some metadata at 
the beginning, added by imaging tools. WinImage demonstrates the same behavior. Though the partitioned disks are not yet supported, 
this feature allows the opening of the first FAT partition on partitioned disk images. 
The boot sector for this search is determined by the following pattern of size 512 bytes exactly: 

`0xB8, 0xx, 0x90, 0xx ... 0xx, 0x55, 0xAA`

where 0xx means any byte. The search range currently is equal to 64kb.


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
	1. Open the archive with the plugin in TCmd -- it would propose to install the plugin. 

Configuration is stored in the `fatdiskimg.ini` file in the configuration path provided by the TCmd (usually -- the same place, where wincmd.ini is located). If the configuration file is damaged or absent, it is recreated with default values.

Works in Double Commander for Windows.

Binary releases available [here](https://github.com/indrekis/FDDImage_TCMD_plugin/releases).

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

Plugin configuration
====================

Configuration file, named fatdiskimg.ini, is searched at the path, provided by the TCmd (most often -- the path where wincmd.ini is located). If configuration file is absent or incorrect it is created with the default configuration.

In addition to using a full-fledged debugger, debugging output can be seen, using [SimpleProgramDebugger](http://www.nirsoft.net/utils/simple_program_debugger.html).

Compilation
===========

Code can be compiled using the Visual Studio project or CMakeLists.txt (tested using MSVC and MinGW). 

Uses C++20, with no obligatory external dependencies. 

As an experimental and optional feature, [FLTK](https://www.fltk.org/) is used to create dialogs and a logging window. 

Examples of the command lines to compile are in the CMakeLists.txt.

Preparing images for tests
==========================

The plugin was tested using two kinds of images:
* Images of virtual machines and emulators used for different tasks.
* Historical floppy images from the retro-computing sites: [WinWorld](https://winworldpc.com/home), [BetaArchive](https://www.betaarchive.com/database/browse.php), [Old-DOS](http://old-dos.ru/), [VETUSWARE](https://vetusware.com/), [Bitsavers](http://www.bitsavers.org/bits/)

The plugin is tested on over 1000 floppy images and dozens of HDD images, including custom-made with up to eleven partitions.

## Linux
Linux loopback devices are a convenient approach to making test images. 

### Example 1 -- partitioned image 

Create image 1Gb in size: 

`dd if=/dev/zero of=image_file.img bs=100M count=10`

Create loopback device, connected to this file:

`sudo losetup -fP image_file.img`

Partition disk (put your device name in place of loop0):

`sudo fdisk /dev/loop0`

To check existing loopback devices: 

`losetup -a`

On using fdisk see [Partitioning with fdisk HOWTO](https://tldp.org/HOWTO/Partition/fdisk_partitioning.html) and [fdisk(8) man](https://man7.org/linux/man-pages/man8/fdisk.8.html).

After the partitioning, loopback devices for partitions would have names like: 

`/dev/loop0p1 /dev/loop0p2 /dev/loop0p3 /dev/loop0p4 /dev/loop0p5 /dev/loop0p6 /dev/loop0p7 /dev/loop0p8 /dev/loop0p9 /dev/loop0p10 /dev/loop0p11`

Remember the special role of loop0p4 -- it is an extended partition, containing loop0p5 and so on.

To create FAT12/FAT16/FAT32/, use the following commands, respectively:

`sudo mkfs.fat -F 12 /dev/loop0p9`

`sudo mkfs.fat -F 16 /dev/loop0p10`

`sudo mkfs.fat -F 32 /dev/loop0p11`

Remember to use the correct partition device. More details: [dosfstools](https://github.com/dosfstools/dosfstools)

To populate image partitions with the files, one can mount it and use it as any other Linux volume:

`mkdir ~/virtual_disk1`

`sudo mount -o loop /dev/loop0p10 ~/virtual_disk1`

Lately, unmount it by: 

`sudo umount ~/virtual_disk1`

The exact command lines could depend on your Linux distribution.

### Example 1 -- nonpartitioned floppy image 

`dd if=/dev/zero of=floppy_144.img bs=1K count=1440`

`sudo losetup -fP floppy_144.img`

`sudo mkfs.fat -F 12 /dev/loop0`

`mkdir ./fdd`

`sudo mount -o loop ./fdd  /dev/loop0`

`<copy files>`

`sudo umount ./fdd`

`sudo losetup -d /dev/loop0`

### Other

Remove loopback device (put your device name in place of loop0):
`sudo losetup -d /dev/loop0`

To create a ext4/3/2 filesystem, use:

`sudo mkfs.ext4 /dev/loop0p2`

`sudo mkfs.ext3 /dev/loop0p3`

`sudo mkfs.ext2 /dev/loop0p5`


To create NTFS or exFAT: 

`sudo mkfs.ntfs /dev/loop0p10`

`sudo mkfs.exfat /dev/loop0p10`

To check the properties of the mounted loopback partition:

`df -hP  ~/virtual_disk1`

## Windows

Under MS Windows, free (as a beer) [OSFMount](https://www.osforensics.com/tools/mount-disk-images.html) can be used to mount single-partition and multi-partition images.

Other imaging tools used during the tests were famous [WinImage](https://www.winimage.com/) and less known, but free (as a beer), DiskExplorer by junnno (no known site, can be downloaded [here](https://vetusware.com/download/Disk%20Explorer%201.69E/?id=16440)).


Problems and limitations
========================
* Read-only
* Does not support exFAT 
* No GPT support 
* Partial support of Unicode in filenames, code implements only ANSI functions as for now.
  * As a result, the path is limited to 260-1 symbols max.
* No support for the 8" images, including 86-DOS and CP/M images
* Directories data, time, and some attributes are not set properly
* Not yet tested on large (close to 2Tb) images.

