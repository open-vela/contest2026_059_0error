# ##############################################################################
# vendor/sg2000/boards/sg2000/milkv_duos/src/CMakeLists.txt
#
# SPDX-License-Identifier: Apache-2.0
# ##############################################################################

set(SRCS sg2000_appinit.c)

target_sources(board PRIVATE ${SRCS})

if(CONFIG_FS_ROMFS)
  set(ROMFS_ROMDISK_IMG romfs.img)
  set(ROMFS_ROMDISK_HDR romfs_img.h)

  add_custom_command(
    OUTPUT ${ROMFS_ROMDISK_IMG}
    COMMAND genromfs -f ${ROMFS_ROMDISK_IMG} -d ${CMAKE_CURRENT_SOURCE_DIR}/etc
            -V "NuttXBootVol"
    COMMENT "Generating ROMFS image"
    DEPENDS etc)

  nuttx_generate_romfs_header(${ROMFS_ROMDISK_HDR} ${ROMFS_ROMDISK_IMG})
  target_sources(board PRIVATE ${ROMFS_ROMDISK_HDR})
endif()
