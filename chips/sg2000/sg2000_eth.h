/****************************************************************************
 * vendor/sg2000/chips/sg2000/sg2000_eth.h
 *
 * SG2002 (CV181x) DWMAC1000 Ethernet Driver for OpenVela
 * Uses the netdev_lowerhalf_s architecture.
 *
 * Reference: SG2002_CV181x_Net_Driver_Manual.md
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __VENDOR_SG2000_SG2000_ETH_H
#define __VENDOR_SG2000_SG2000_ETH_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/net/netdev_lowerhalf.h>
#include <stdint.h>

#include "hardware/sg2000_gmac.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Descriptor ring sizes — defined in sg2000_gmac.h, check here for sanity */

#if (TX_RING_SIZE & (TX_RING_SIZE - 1)) != 0
#  error "TX_RING_SIZE must be a power of 2"
#endif

#if (RX_RING_SIZE & (RX_RING_SIZE - 1)) != 0
#  error "RX_RING_SIZE must be a power of 2"
#endif

/* Cache line size for T-Head C906 D-Cache */

#define C906_CACHE_LINE_SIZE      64

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* DWMAC1000 basic DMA descriptor (16 bytes, 4 × 32-bit words).
 *
 * CV181x/SG2002 uses the basic descriptor format, NOT the enhanced format.
 * Each descriptor is 4-byte aligned; the ring is 64-byte aligned (cache line).
 *
 * Reference: Manual §3.1, §3.2, §3.3
 */

struct sg2000_dma_desc_s
{
  volatile uint32_t des0;  /* Status / OWN / control (TDES0 or RDES0) */
  volatile uint32_t des1;  /* Buffer sizes / control flags (TDES1 or RDES1) */
  volatile uint32_t des2;  /* Buffer address 1 (32-bit physical) */
  volatile uint32_t des3;  /* Buffer address 2 / next descriptor (unused) */
};

/* Ensure 16-byte descriptor size at compile time */

_Static_assert(sizeof(struct sg2000_dma_desc_s) == 16,
               "DMA descriptor must be 16 bytes (basic format)");

/* Driver private data.
 *
 * The netdev_lowerhalf_s MUST be the first member so the OpenVela network
 * stack can cast between struct netdev_lowerhalf_s * and our private struct.
 */

struct sg2000_eth_priv_s
{
  /* MUST be first — visible to the OpenVela network stack */

  struct netdev_lowerhalf_s dev;

  /* Hardware base addresses */

  uintptr_t gmac_base;              /* GMAC register base (0x04070000) */
  uintptr_t phy_base;               /* EPHY register base (0x03009000) */
  uintptr_t phy_top;                /* EPHY top register base (0x03009800) */

  /* DMA descriptors and data buffers.
   * Descriptors and RX buffers are allocated with 64-byte alignment
   * to match the C906 D-Cache line size, preventing false sharing
   * between adjacent descriptors/buffers.
   */

  struct sg2000_dma_desc_s *tx_desc;        /* TX descriptor ring (VA) */
  struct sg2000_dma_desc_s *rx_desc;        /* RX descriptor ring (VA) */
  uint8_t *tx_buf[TX_RING_SIZE];            /* TX data buffers (VA) */
  uint8_t *rx_buf[RX_RING_SIZE];            /* RX data buffers (VA) */

  uintptr_t tx_desc_phys;                   /* TX descriptor ring PA */
  uintptr_t rx_desc_phys;                   /* RX descriptor ring PA */
  uintptr_t tx_buf_phys[TX_RING_SIZE];      /* TX data buffer PAs */
  uintptr_t rx_buf_phys[RX_RING_SIZE];      /* RX data buffer PAs */

  /* Descriptor ring indices */

  volatile unsigned int tx_cur;             /* Next TX descriptor to use */
  volatile unsigned int tx_dirty;           /* Oldest in-flight TX descriptor */
  volatile unsigned int rx_cur;             /* Next RX descriptor to check */

  /* MAC address and IRQ */

  uint8_t mac_addr[6];
  int irq;

  /* PHY link state */

  volatile int link_up;                     /* 1 = link up, 0 = down */
  int speed;                                /* 10 or 100 (Mbps) */
  int duplex;                               /* 1 = full, 0 = half */

  /* TX packet tracking — one netpkt per in-flight descriptor */

  FAR netpkt_t *tx_pkt[TX_RING_SIZE];

  /* DMA memory allocation tracking */

  bool dma_allocated;                       /* True if DMA resources allocated */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: sg2000_eth_initialize
 *
 * Description:
 *   Initialize the SG2000 Ethernet driver and register it with the OpenVela
 *   network stack using the netdev_lowerhalf_s architecture.
 *
 *   Called from board_late_initialize() during system bringup.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK on success; Negated errno on failure.
 *
 ****************************************************************************/

int sg2000_eth_initialize(void);

#endif /* __VENDOR_SG2000_SG2000_ETH_H */
