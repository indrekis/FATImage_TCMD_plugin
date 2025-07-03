- [Introduction](#introduction)
- [Installation](#installation)
- [Plugin configuration](#plugin-configuration)
- [Compilation](#compilation)
- [Preparing images for tests](#preparing-images-for-tests)
  - [Linux](#linux)
    - [Example 1 -- partitioned image](#example-1----partitioned-image)
    - [Example 1 -- nonpartitioned floppy image](#example-1----nonpartitioned-floppy-image)
    - [Example 2 -- script to create a 25-disks image](#example-2----script-to-create-a-25-disks-image)
    - [Other](#other)
  - [Windows](#windows)
- [Problems and limitations](#problems-and-limitations)
  - [Non-problems but caveats](#non-problems-but-caveats)
  - [Plans](#plans)
- [Credits](#credits)


# Introduction

FATImage is a wcx (archive) plugin for 64-bit and 32-bit Total Commander (TCmd) that provides read-write access to FAT filesystem images, including partitioned ones.

Key features:

- Supports read-write access to FAT12, FAT16, FAT32, VFAT images on both single-partition and multiple-partition images.
  - Supports MBR-based partitions.
  - Volumes with unknown or unsupported filesystems are indicated accordingly.
  - Detects GPT-based images and exFAT partitions, but currently does not work with them.
  - Partial Unicode support for the FAT32: UCS-16 symbols in  Long File Names (LFNs) are converted to the local codepage.

> Attention! Write support is experimental. While the author uses it daily, it has undergone significantly less testing compared to reading.

- Write operations include creating, modifying, deleting, and adding files and directories within FAT images.
- Image creation is supported but currently limited: extended partitions are not yet supported, and the UI/UX is debatable.
  - When creating a multiple-partition image, files are not copied because of the ambiguity -- please open the archive, select the appropriate disk, and repeat the copying.  
- The plugin can read a broader range of images than it can write; the reading code is adapted to handle historical images with various quirks that the writing code does not fully support yet.
- Writing is based on the famous [FatFS](https://elm-chan.org/fsw/ff/) modified to plugin-specific requirements: extended partition navigation, support for the image file name and offset, etc.

Additional features:

- Supports DOS 1.xx images without BPB.
  - This support relies on the media descriptor in the FAT table and image size. It is currently read-only.
  - Supports several optional exceptions for the popular quirks of historical disk images from the retro sites.

- The plugin can search for the boot sector within an image, which is useful for opening images containing metadata added by imaging tools at the beginning.

> WinImage demonstrates the same behavior. The boot sector for this search is determined by the following pattern of size 512 bytes exactly: 
>
> `0xB8, 0xx, 0x90, 0xx ... 0xx, 0x55, 0xAA`
>
> where 0xx means any byte. 

Additional information in the author's blog (in Ukrainian, not yet updated for the write support): "[Анонс -- 64-бітний плагін Total Commander для роботи із образами FAT](https://indrekis2.blogspot.com/2022/08/64-total-commander-fat.html)".  

# Installation

As usual for the TCmd plugins:

* Manual installation:
  1. Unzip the WCX to the directory of your choice (e.g., c:\wincmd\plugins\wcx)
  2. In TCmd, choose **Configuration** -> **Options**
  3. Open the '**Packer**' page
  4. Click '**Configure packer extension WCXs**'
  5. Type '*img*' as the extension
  6. Click '**New type**', and select the *img.wcx64* (*img.wcx* for 32-bit TCmd)
  7. Click **OK**
* Automated installation:
  1. Open the archive containing the plugin directly in TCmd.
  2. The program will prompt you to install the plugin.

Settings are stored in the `fatdiskimg.ini` file in the configuration path provided by the TCmd (usually in the same place where wincmd.ini is located). The configuration file will be recreated with default values if it is missing or corrupted.

Works in Double Commander for Windows. A Linux and other POSIX systems port is planned.

Binary releases available [here](https://github.com/indrekis/FDDImage_TCMD_plugin/releases).

**This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.**  

# Plugin configuration

The configuration file, named fatdiskimg.ini, is searched using the path provided by the TCmd (most often, the path where wincmd.ini is located). If the configuration file is absent or incorrect, it is created with the default configuration.

Except for the logging options, the plugin currently rereads the configuration before opening each image (archive).

Options can also be changed from the "Options" dialog of the packing dialog. 

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
max_depth=100
max_invalid_chars_in_dir=0

new_arc_single_part=0
new_arc_custom_unit=2
new_arc_custom_value=1440
new_arc_single_fs=2
new_arc_multi_values_0=1024
new_arc_multi_units_0=2
new_arc_multi_fs_0=2
new_arc_multi_values_1=1024
new_arc_multi_units_1=2
new_arc_multi_fs_1=2
new_arc_multi_values_2=1024
new_arc_multi_units_2=2
new_arc_multi_fs_2=2
new_arc_multi_values_3=1024
new_arc_multi_units_3=2
new_arc_multi_fs_3=2
new_arc_total_unit=2
new_arc_total_value=4096
new_arc_save_config=1
```

* `ignore_boot_signature`. If the first 512-byte sector of the image does not contain 0x55AA and ignore_boot_signature == 0, the image is treated as not a FAT volume, even if the BPB data looks consistent.
* `use_VFAT` -- if 1, long file names are processed (regardless of the FAT type).
* `process_DOS1xx_images == 1` allows interpreting images with exact size, equal to: 160Kb, 180Kb, 320Kb, or 360Kb as a pre-BPB FAT image. Such images mostly came from the DOS 1.xx epoch disks. Disk format is then detected based on the [Media descriptor byte](https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system#FATID) -- the first byte of the FAT, which is considered situated in the first byte of the second image sector.
  * It is a much less reliable format detection criterion than the full BPB, so enabling this option can interfere with the normal image processing. However, the author had never met with such a situation.
  * MC/PC-DOS versions 2.00-3.20 [sometimes created bad BPBs](http://www.os2museum.com/wp/dos-boot-sector-bpb-and-the-media-descriptor-byte/), making the media descriptors the primary source of disk format information.
* process_DOS1xx_exceptions=1 -- enables DOS 1.xx non-BPB images processing exceptions targeted at supporting the popular quirks of historical disk images from the retro sites. Not recommended for typical users.
* `process_MBR == 1` -- if the disk is not a correct non-partitioned disk image, try to interpret it as an MBR-based partitioned image. This option is deprecated and scheduled for removal — the partition-related code is now considered stable enough.
* `search_for_boot_sector==1` -- if the disk image does not contain the correct boot sector or the MBR at the beginning, attempt to find the boot sector in the image file by the pattern `0xB8, 0xx, 0x90, 0xx ... 0xx, 0x55, 0xAA`, where 0xx means any byte. This approach helps open images containing meta information before the image, but the image itself is not altered. Search for the MBR is not supported, but if there is a partitioned image after the metainformation, the first FAT partition of the image can be found. Not recommended for typical users.
* `search_for_boot_sector_range==65536` -- range of the bootsector search, in bytes. The default value is 64kb. It is not recommended to increase this value substantially beyond this value.
   * Not only does it slow down working with the images, but it can also lead to false detections.
   * For example, utilities like `fdisk`, `sys`, or `format` can contain bootsector copies required to perform their duties.
* `allow_dialogs==1` -- if FLKT support is enabled while building, enable dialogs on some image peculiarities, like the absence of the 0x55AA signature. 
  * Because of the write options dialog, FLTK is always linked to the plugin, but those dialogs can be annoying anyway, so the user can still disable them.
* `allow_txt_log==1` -- allows detailed log output, describing image properties and anomalies. It can noticeably slow down the plugin, and it is not for general use, as it can lead to problems. It is helpful for in-depth image analysis.  
  * Additionally, important image analysis events are logged to the debug console for the debug builds (without NDEBUG defined). In addition to using a full-fledged debugger, debugging output can be seen using [SimpleProgramDebugger](http://www.nirsoft.net/utils/simple_program_debugger.html).
* `log_file_path=<filename>` -- logging filename. If opening this file for writing fails, logging is disabled (allow_txt_log==0). The file is created from scratch at the first use of the plugin during the current TCmd session.
* `debug_level` -- not used now.
* `max_depth` -- maximum depth of the directory tree to be traversed.
* `max_invalid_chars_in_dir` -- maximum number of invalid characters in the directory name. If the number of invalid characters exceeds this value, the directory is not opened and is presented as empty. Useful for the corrupted images.
  * Value above 11 effectively disables this check.
* new_arc_* options are related to creating the new images.
  * Please use the options dialog to set them.
  * Manual edition is possible -- please consult the sources or feel free to ask.
  * Units codes: 0 -- Bytes, 1 -- 512b Sectors, 2 -- Kilobytes, 3 -- 4Kb Blocks, 4 -- Megabytes. 
  * FS type codes: 0 -- None, skip formatting, 1 -- FAT12, 2 -- FAT16, 3 -- FAT32, 4 -- Auto, image size based, according to the Microsoft standards.

# Compilation

Code can be compiled using the Visual Studio project or CMakeLists.txt (tested using MSVC and MinGW). 

Uses C++20, with no obligatory external dependencies. Though [FLTK](https://www.fltk.org/) is used to create dialogs, including a packing options dialog, it is highly recommended.

> When FLTK is used, the plugin is substantially large, though absolute size, thanks to FLTK's compactness, is small, around 0.5 Mb.

Examples of the command lines to compile using CMake are in the CMakeLists.txt and make_distr.sh script.

# Preparing images for tests

The plugin was tested using two kinds of images:
* Images of virtual machines and emulators used for different tasks.
* Historical floppy images from the retro-computing sites: [WinWorld](https://winworldpc.com/home), [BetaArchive](https://www.betaarchive.com/database/browse.php), [Old-DOS](http://old-dos.ru/), [VETUSWARE](https://vetusware.com/), [Bitsavers](http://www.bitsavers.org/bits/)

The plugin is tested on over a thousand floppy images and dozens of HDD images, including custom-made ones with up to **25** partitions.

## Linux
Linux loopback devices are a convenient approach to making test images. 

### Example 1 -- partitioned image 

Create an image 1 GB in size: 

`dd if=/dev/zero of=image_file.img bs=100M count=10`

Create a  loopback device, connected to this file:

`sudo losetup -fP image_file.img`

Partition disk (put your device name in place of loop0):

`sudo fdisk /dev/loop0`

To check existing loopback devices: 

`losetup -a'

On using fdisk, see [Partitioning with fdisk HOWTO](https://tldp.org/HOWTO/Partition/fdisk_partitioning.html) and [fdisk(8) man](https://man7.org/linux/man-pages/man8/fdisk.8.html).

After the partitioning, loopback devices for partitions would have names like: 

`/dev/loop0p1 /dev/loop0p2 /dev/loop0p3 /dev/loop0p4 /dev/loop0p5 /dev/loop0p6 /dev/loop0p7 /dev/loop0p8 /dev/loop0p9 /dev/loop0p10 /dev/loop0p11`

Remember the special role of loop0p4 -- it is an extended partition, containing loop0p5 and so on.

To create FAT12/FAT16/FAT32/, use the following commands, respectively:

```bash
sudo mkfs.fat -F 12 /dev/loop0p9
sudo mkfs.fat -F 16 /dev/loop0p10
sudo mkfs.fat -F 32 /dev/loop0p11
```

Remember to use the correct partition device. More details: [dosfstools](https://github.com/dosfstools/dosfstools)

To populate image partitions with the files, one can mount them and use them as any other Linux volume:

```bash
mkdir ~/virtual_disk1
sudo mount -o loop /dev/loop0p10 ~/virtual_disk1
```

Lately, unmount it by: 

`sudo umount ~/virtual_disk1`

`sudo losetup -d /dev/loop0`

The exact command lines could depend on your Linux distribution.

### Example 1 -- nonpartitioned floppy image

```bash
dd if=/dev/zero of=floppy_144.img bs=1K count=1440
sudo losetup -fP floppy_144.img
sudo mkfs.fat -F 12 /dev/loop0
mkdir ./fdd
sudo mount -o loop ./fdd  /dev/loop0

<copy files>

sudo umount ./fdd
sudo losetup -d /dev/loop0
```

### Example 2 -- script to create a 25-disks image

Note -- this image is oversized; most systems do not support FAT disks larger than 2 GB.

```bash
#!/bin/bash
set -e

IMG="image_file4.img"
SIZE=2600  # in Mb (adjust as needed)

# Create a blank image
dd if=/dev/zero of=$IMG bs=1M count=$SIZE

# Create MBR partition table
parted -s $IMG mklabel msdos

# Create 1 primary FAT16 partition from 1MiB to 101MiB (100 MiB size)
parted -s $IMG mkpart primary fat16 1MiB 101MiB

# Create extended partition from 101MiB to end (2499MiB)
parted -s $IMG mkpart extended 101MiB 2599MiB

# Create logical partitions inside the extended partition
# We'll start from 102MiB to 2499MiB split into 24 partitions ~100MiB each

start=102
size=100
for i in $(seq 1 24); do
  end=$((start + size))
  parted -s $IMG mkpart logical fat16 ${start}MiB ${end}MiB
  start=$((end + 1))
done

parted -s $IMG print

# Associate the image with the loop device with the partition scan
LOOP=$(losetup --find --show --partscan $IMG)

# Wait for the kernel to create partition devices
sleep 5

# Format primary partition (partition 1)
mkfs.vfat -F 16 ${LOOP}p1

# Format logical partitions (p5 to p27, because logical partitions start from 5)
for partnum in $(seq 5 28); do
  mkfs.vfat -F 16 ${LOOP}p${partnum}
done

# Detach the loop device
losetup -d $LOOP
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

`df -hP  ~/virtual_disk1`

## Windows

Under MS Windows, free (as a beer) [OSFMount](https://www.osforensics.com/tools/mount-disk-images.html) can be used to mount single-partition and multi-partition images. Less capable but open-source similar tool -- [ImDisk](https://sourceforge.net/projects/imdisk-toolkit/).

Other imaging tools used during the tests were famous [WinImage](https://www.winimage.com/) and less known, but free (as a beer), DiskExplorer by junnno (no known site, can be downloaded [here](https://vetusware.com/download/Disk%20Explorer%201.69E/?id=16440)).

# Problems and limitations

* Does not support exFAT, although it can detect exFAT volumes.
* No GPT support beyond detection.
* It does not yet support CHS partitions, only LBA. 
  * The vast majority of HDDs created since the 1990s use LBA.
* Partial Unicode support in filenames: currently, only ANSI functions are implemented.
  * As a result, the path is limited to a maximum of 260-1 characters.
  * Path lengths close to the system maximum (PATH_MAX, typically 260 on Windows) have not been thoroughly tested.
* Directories, data, time, and some attributes are not set properly when unpacking.
* The processed data size shown in the progress dialog is approximate and not fully precise -- it would complicate the code a little.
* Not yet fully tested on large (close to 2 TB) images. Though, to some extent, it works even on large images.
* 32-bit plugin version does not support background operation -- TCmd crashes or hangs every time the plugin is used if they are allowed.
* Background "packing" is not yet enabled due to potential race condition concerns.
* Read-only files are deleted during move operations **without confirmation**.
* Image creation is currently limited, and the GUI is somewhat inconvenient.
* Cannot create bootable disks.
* Cannot delete "EA DATA. SF" -- OS/2 extended attributes file, possibly also used by Cygwin.
* File main_resources.rc uses name fatimg.wcx64  even for the 32-bit fatimg.wcx.
* It is currently impossible to view the free space remaining within the image.
* Partially copied files (e.g., when there is no free space left) remain as-is and are not cleaned up.

## Non-problems but caveats

* No support for the 8" images, including 86-DOS and CP/M images -- they are too different, a separate plugin is developed for them.
* Using Qt would provide a much nicer and more convenient UI, but it would significantly increase the image size and might introduce additional reentrancy issues.
* Plugin is more permissive than most historical implementations -- as a result, not all images created by it may be compatible with those implementations. Opposite is also true -- the plugin supports more image variations than most other implementations. 
  * Compatibility of the existing images should not change (mostly).
  * Might add some profiles (eg, "MS DOS 3.31 compatible") for future consistency checking.

## Plans

* Improve image creation by supporting extended partitions, enhancing consistency checks, and improving the UI. 
  * The current UI improvement plan includes using a special text file that, when archived, instructs the plugin to create a multi-partition image.
  * Possibly avoid using FatFS partition-related code altogether.
* Add editing support for non-BPB images.
* Improve code quality. There is currently some tech debt accumulated. 
* Improve and test reentrancy.
* Implement exFAT read and write support. Possibly use FatFS for reading as well, since supporting legacy quirks is not required for this relatively recent filesystem.
* Consider adding metafiles within images containing metadata such as available space (potentially displaying free space as the metafile size :-)).
* Implement deletion of partially copied files and add checks for available space before copying. 
  * This will require extending FatFS error handling.
* Add locking for the FatFS and extensively test reentrancy.
* Improve diagnostics.
* Add support for Linux and other POSIX systems. 
  * The boilerplate code is ready, so porting should be straightforward.
* Deletion, moving, and editing existing partitions could be too complex to implement in the near future.

# Credits

IMG Plugin for Total Commander (TCmd) by IvGzury was used as a starting point because its sources were available.

Nataliia Sydor (aka NataMontari) [contributed to development](https://github.com/NataMontari/FAT_Image_TCMD_plugin_write) of the write support as a part of her course project on the "Modern C++" course.
