# ##############################################################################
# vendor/sg2000/chips/sg2000/CMakeLists.txt
#
# SPDX-License-Identifier: Apache-2.0
# ##############################################################################

set(SRCS
    sg2000_start.c
    sg2000_irq.c
    sg2000_irq_dispatch.c
    sg2000_timerisr.c
    sg2000_allocateheap.c
    sg2000_mm_init.c
    sg2000_pgalloc.c
    sg2000_head.S
)

if(CONFIG_SG2000_ETH)
    list(APPEND SRCS sg2000_eth.c)
endif()

target_sources(arch PRIVATE ${SRCS})

target_include_directories(arch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
