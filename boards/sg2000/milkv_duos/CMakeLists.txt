# ##############################################################################
# vendor/sg2000/boards/sg2000/milkv_duos/CMakeLists.txt
#
# SPDX-License-Identifier: Apache-2.0
# ##############################################################################

add_subdirectory(src)

# Collect extra library files from the board's libs directory if present

file(GLOB EXTRA_LIBS "${BOARD_DIR}/libs/${CONFIG_ARCH_BOARD}/*.a")

set(LINKER_SCRIPT "${BOARD_DIR}/scripts/ld.script")
