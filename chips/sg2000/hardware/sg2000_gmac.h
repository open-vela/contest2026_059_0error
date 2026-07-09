/****************************************************************************
 * vendor/sg2000/chips/sg2000/hardware/sg2000_gmac.h
 *
 * SG2002/CV181x Synopsys DWMAC1000 (GMAC v3.50a) Hardware Definitions
 *
 * Reference: SG2002_CV181x_Net_Driver_Manual.md
 * Based on the Linux 5.10 stmmac driver for CV181x/SG2002.
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

#ifndef __VENDOR_SG2000_HARDWARE_SG2000_GMAC_H
#define __VENDOR_SG2000_HARDWARE_SG2000_GMAC_H

/****************************************************************************
 * Pre-processor Definitions — Base Addresses
 ****************************************************************************/

#define SG2000_GMAC_BASE          0x04070000  /* GMAC register base */
#define SG2000_EPHY_BASE          0x03009000  /* EPHY base address */
#define SG2000_EPHY_TOP           0x03009800  /* EPHY top control register */

/* Clock Generator (clkgen) at 0x03002000 */

#define SG2000_CLKGEN_BASE        0x03002000
#define REG_CLK_EN_0              (SG2000_CLKGEN_BASE + 0x000)

#define CLK_EN_ETH0_500M          (1 << 25)   /* clk_500m_eth0 gate */
#define CLK_EN_ETH0_AXI4          (1 << 26)   /* clk_axi4_eth0 gate */

/* Clock-reset controller at 0x03002000 (same bank as clkgen) */

#define CLK_RST_500M_ETH0         13
#define CLK_RST_AXI_ETH0          14

/* Main reset-controller at 0x03003000 */

#define SG2000_RESET_BASE         0x03003000
#define REG_RESET_BANK(n)         (SG2000_RESET_BASE + ((n) * 4))

#define RST_ETH0                  12
#define RST_ETHPHY                96
#define RST_ETHPHYRST_APB         97

/****************************************************************************
 * Pre-processor Definitions — MAC Core Registers
 *
 * All offsets relative to SG2000_GMAC_BASE (0x04070000).
 * These offsets are validated against the SG2002 silicon (old working driver).
 ****************************************************************************/

#define GMAC_CONTROL              0x0000  /* MAC Configuration Register */
#define GMAC_FRAME_FILTER         0x0004  /* Frame Filter (aka PACKET_FILTER) */
#define GMAC_MII_ADDR             0x0010  /* MII Management Address (legacy) */
#define GMAC_MII_DATA             0x0014  /* MII Management Data (legacy) */
#define GMAC_FLOW_CTRL            0x0018  /* Flow Control Register */
#define GMAC_VLAN_TAG             0x001C  /* VLAN Tag Register */
#define GMAC_INT_STATUS           0x00B0  /* Interrupt Status */
#define GMAC_INT_MASK             0x00B4  /* Interrupt Mask / Enable */
#define GMAC_VLAN_TAG             0x001C  /* VLAN Tag Register */
#define GMAC_DEBUG                0x0024  /* Debug Register */
#define GMAC_MAC_ADDR0_HIGH       0x0300  /* MAC Address 0 High (incl. AE bit) */
#define GMAC_MAC_ADDR0_LOW        0x0304  /* MAC Address 0 Low */

/****************************************************************************
 * Pre-processor Definitions — MAC Address Registers
 *
 * Slot 0 is the primary address. Each slot is 8 bytes.
 ****************************************************************************/

#define GMAC_ADDR_HIGH(n)         (0x0300 + (n) * 8)
#define GMAC_ADDR_LOW(n)          (0x0304 + (n) * 8)

/****************************************************************************
 * Pre-processor Definitions — GMAC_CONTROL (0x0000) Bits
 *
 * TX proven working with these bit positions (ARP seen in Wireshark).
 ****************************************************************************/

#define GMAC_CONTROL_RE           (1 << 2)   /* Receiver Enable */
#define GMAC_CONTROL_TE           (1 << 3)   /* Transmitter Enable */
#define GMAC_CONTROL_DC           (1 << 4)   /* Deferral Check */
#define GMAC_CONTROL_ACS          (1 << 7)   /* Auto Pad/CRC Stripping */
#define GMAC_CONTROL_DCRS         (1 << 16)  /* Disable Carrier Sense */
#define GMAC_CONTROL_DM           (1 << 11)  /* Duplex Mode (1=Full) */
#define GMAC_CONTROL_FES          (1 << 14)  /* Speed (0=10Mbps, 1=100Mbps) */
#define GMAC_CONTROL_PS           (1 << 15)  /* Port Select (0=GMII, 1=MII) */
#define GMAC_CONTROL_BE           (1 << 21)  /* Frame Burst Enable */
#define GMAC_CONTROL_JD           (1 << 22)  /* Jabber Disable */

/* GMAC_CORE_INIT: JD | PS | ACS | BE | DCRS */

#define GMAC_CORE_INIT            (GMAC_CONTROL_JD   | GMAC_CONTROL_PS  | \
                                   GMAC_CONTROL_ACS  | GMAC_CONTROL_BE  | \
                                   GMAC_CONTROL_DCRS)

/****************************************************************************
 * Pre-processor Definitions — GMAC_FRAME_FILTER (0x0004) Bits
 ****************************************************************************/

#define GMAC_FILTER_PR            (1 << 0)   /* Promiscuous Mode */
#define GMAC_FILTER_HUC           (1 << 1)   /* Hash Unicast */
#define GMAC_FILTER_HMC           (1 << 2)   /* Hash Multicast */
#define GMAC_FILTER_DAIF          (1 << 3)   /* DA Inverse Filtering */
#define GMAC_FILTER_PM            (1 << 4)   /* Pass All Multicast */
#define GMAC_FILTER_DBF           (1 << 5)   /* Disable Broadcast Frames */
#define GMAC_FILTER_PCF           (3 << 6)   /* Pass Control Frames mask */
#define GMAC_FILTER_SAF           (1 << 8)   /* Source Address Filter */
#define GMAC_FILTER_SAIF          (1 << 9)   /* SA Inverse Filtering */
#define GMAC_FILTER_HPF           (1 << 10)  /* Hash or Perfect Filter */
#define GMAC_FILTER_RA            (1 << 31)  /* Receive All */

/****************************************************************************
 * Pre-processor Definitions — GMAC_MII_ADDR (0x0010) Bits (Legacy MDIO)
 *
 * CV181x uses the TRM Chapter 18 legacy GMAC MDIO layout, NOT the
 * standard DWMAC4 MDIO controller at 0x200/0x204.
 * Reference: Manual §8.2, §10.10
 ****************************************************************************/

#define GMAC_MII_ADDR_BUSY        (1 << 0)   /* MII Busy */
#define GMAC_MII_ADDR_WRITE       (1 << 1)   /* MII Write (1=Write, 0=Read) */
#define GMAC_MII_ADDR_CR_SHIFT    2          /* CSR Clock Divider shift */
#define GMAC_MII_ADDR_CR_MASK     (0x3f << GMAC_MII_ADDR_CR_SHIFT)
#define GMAC_MII_ADDR_CR_250_300M (5 << GMAC_MII_ADDR_CR_SHIFT)
                                            /* CSR=250MHz, div=5 → 250/10=25MHz */
#define GMAC_MII_ADDR_REG_SHIFT   6          /* MII Register Address shift */
#define GMAC_MII_ADDR_PHY_SHIFT   11         /* PHY Address shift */

/* CSR clock divider value for MDC ≈ 1MHz:
 * CSR_CLK = 250MHz, desired MDC = 1~2.5MHz
 * MDC = CSR_CLK / (2 * (csr_div + 1)) where csr_div is 0-63
 * For 1MHz: csr_div = 124, but max is 63, so use 124 via CR field
 * With CR=5 (div 10): MDC = 250/20 = 12.5MHz — high but works
 * For proper 1MHz use csr_div=124 direct:
 *   MDC = 250 / (2 * (124+1)) = 250/250 = 1MHz
 * But the hardware max for CR field is only 0-63.
 * With max CR=63: MDC = 250/(2*63) ≈ 2MHz (acceptable range)
 * Linux uses the CR field where CR selects from a lookup table,
 * use the same value as the Linux driver: CR=5 (matches dwmac-cvitek.c)
 */

/* Practical csr_div for direct formula (bypassing CR lookup):
 * use value 124 for ~1MHz MDC from 250MHz CSR clock
 */
#define GMAC_MDIO_CSR_DIV_1MHZ    124

/****************************************************************************
 * Pre-processor Definitions — GMAC_INT_STATUS / GMAC_INT_MASK Bits
 *
 * Reference: Manual §2.7
 ****************************************************************************/

#define GMAC_INT_RGMII            (1 << 0)   /* RGMII Interrupt */
#define GMAC_INT_PCS_LINK         (1 << 1)   /* PCS Link Status */
#define GMAC_INT_PCS_AN_COMPLETE  (1 << 2)   /* PCS AN Complete */
#define GMAC_INT_PMT              (1 << 4)   /* PMT Interrupt */
#define GMAC_INT_LPI              (1 << 5)   /* LPI Interrupt */

/* Default MAC interrupt mask: disable timestamp, RGMII, PCS interrupts */
#define GMAC_INT_DEFAULT_MASK     0x00000227

/****************************************************************************
 * Pre-processor Definitions — GMAC_MAC_ADDR0_HIGH (0x0040) Bits
 ****************************************************************************/

#define GMAC_HI_REG_AE            (1 << 31)  /* Address Enable */

/****************************************************************************
 * Pre-processor Definitions — DMA Registers
 *
 * DWMAC1000 v3.50a flat DMA register layout.
 * All offsets relative to SG2000_GMAC_BASE (0x04070000).
 * Reference: Manual §2.3, §10.2
 ****************************************************************************/

#define DMA_BUS_MODE              0x1000  /* Bus Mode (CSR0) */
#define DMA_XMT_POLL_DEMAND       0x1004  /* Transmit Poll Demand */
#define DMA_RCV_POLL_DEMAND       0x1008  /* Receive Poll Demand */
#define DMA_RCV_BASE_ADDR         0x100C  /* RX Descriptor List Base Address */
#define DMA_TX_BASE_ADDR          0x1010  /* TX Descriptor List Base Address */
#define DMA_STATUS                0x1014  /* Status Register (CSR5) */
#define DMA_CONTROL               0x1018  /* Operation Mode (CSR6) */
#define DMA_INTR_ENA              0x101C  /* Interrupt Enable (CSR7) */
#define DMA_MISSED_FRAME_CTR      0x1020  /* Missed Frame Counter */
#define DMA_RX_WATCHDOG           0x1024  /* RX Watchdog Timer */
#define DMA_AXI_BUS_MODE          0x1028  /* AXI Master Bus Mode */
#define DMA_HOST_TX_DESC          0x1048  /* Current Host TX Descriptor */
#define DMA_HOST_RX_DESC          0x104C  /* Current Host RX Descriptor */
#define DMA_CUR_TX_BUF_ADDR       0x1050  /* Current Host TX Buffer Address */
#define DMA_CUR_RX_BUF_ADDR       0x1054  /* Current Host RX Buffer Address */
#define DMA_HW_FEATURE            0x1058  /* Hardware Feature Register */

/****************************************************************************
 * Pre-processor Definitions — DMA_BUS_MODE (0x1000) Bits
 *
 * Reference: Manual §2.5
 ****************************************************************************/

#define DMA_SFT_RESET             (1 << 0)   /* Software Reset (SWR) */
#define DMA_DSL_SHIFT             2           /* Descriptor Skip Length */
#define DMA_DSL_MASK              (0x1f << DMA_DSL_SHIFT)
#define DMA_ATDS                  (1 << 7)   /* Alternate Descriptor Size */
                                              /* 0 = 16B basic, 1 = 32B enhanced */
#define DMA_PBL_SHIFT             8           /* Programmable Burst Length */
#define DMA_PBL_MASK              (0x3f << DMA_PBL_SHIFT)
#define DMA_PBL_1                 (0 << DMA_PBL_SHIFT)
#define DMA_PBL_2                 (1 << DMA_PBL_SHIFT)
#define DMA_PBL_4                 (2 << DMA_PBL_SHIFT)
#define DMA_PBL_8                 (3 << DMA_PBL_SHIFT)
#define DMA_PBL_16                (4 << DMA_PBL_SHIFT)
#define DMA_PBL_32                (5 << DMA_PBL_SHIFT)
#define DMA_PR_SHIFT              14         /* Priority Ratio */
#define DMA_FB                    (1 << 16)  /* Fixed Burst */
#define DMA_RPBL_SHIFT            17         /* RX Programmable Burst Length */
#define DMA_RPBL_MASK             (0x3f << DMA_RPBL_SHIFT)
#define DMA_USP                   (1 << 23)  /* Use Separate PBL */
#define DMA_8XPBL                 (1 << 24)  /* 8xPBL Mode */
#define DMA_AAL                   (1 << 25)  /* Address-Aligned Beats */
#define DMA_MB                    (1 << 26)  /* Mixed Burst */

/****************************************************************************
 * Pre-processor Definitions — DMA_CONTROL (CSR6, 0x1018) Bits
 *
 * Reference: Manual §2.6
 ****************************************************************************/

/* DMA_CONTROL (CSR6, 0x1018) bits.  Per CV181x documentation §2.6:
 *   bit 1 = SR (Start/Stop Receive), bit 13 = ST (Start/Stop Transmit)
 */
#define DMA_CONTROL_SR            (1 << 1)   /* Start/Stop Receive */
#define DMA_CONTROL_RES0          (1 << 0)   /* Reserved (do not use) */
#define DMA_CONTROL_OSF           (1 << 2)   /* Operate on Second Frame */
#define DMA_CONTROL_RTC_SHIFT     3          /* Receive Threshold Control */
#define DMA_CONTROL_RTC_MASK      (3 << DMA_CONTROL_RTC_SHIFT)
#define DMA_CONTROL_RTC_64        (0 << DMA_CONTROL_RTC_SHIFT)
#define DMA_CONTROL_RTC_32        (1 << DMA_CONTROL_RTC_SHIFT)
#define DMA_CONTROL_RTC_96        (2 << DMA_CONTROL_RTC_SHIFT)
#define DMA_CONTROL_RTC_128       (3 << DMA_CONTROL_RTC_SHIFT)
#define DMA_CONTROL_ST            (1 << 13)  /* Start/Stop Transmission */
#define DMA_CONTROL_TTC_SHIFT     14         /* Transmit Threshold Control */
#define DMA_CONTROL_TTC_MASK      (7 << DMA_CONTROL_TTC_SHIFT)
#define DMA_CONTROL_TTC_64        (0 << DMA_CONTROL_TTC_SHIFT)
#define DMA_CONTROL_TTC_128       (1 << DMA_CONTROL_TTC_SHIFT)
#define DMA_CONTROL_TTC_192       (2 << DMA_CONTROL_TTC_SHIFT)
#define DMA_CONTROL_TTC_256       (3 << DMA_CONTROL_TTC_SHIFT)
#define DMA_CONTROL_TTC_40        (4 << DMA_CONTROL_TTC_SHIFT)
#define DMA_CONTROL_TTC_32        (5 << DMA_CONTROL_TTC_SHIFT)
#define DMA_CONTROL_TTC_24        (6 << DMA_CONTROL_TTC_SHIFT)
#define DMA_CONTROL_TTC_16        (7 << DMA_CONTROL_TTC_SHIFT)
#define DMA_CONTROL_FTF           (1 << 20)  /* Flush Transmit FIFO */
#define DMA_CONTROL_TSF           (1 << 21)  /* Transmit Store and Forward */
#define DMA_CONTROL_RFD_SHIFT     22         /* Receive FIFO Depth threshold */
#define DMA_CONTROL_RFD_MASK      (3 << DMA_CONTROL_RFD_SHIFT)
#define DMA_CONTROL_RFA_SHIFT     24         /* Receive FIFO Almost Full threshold */
#define DMA_CONTROL_RFA_MASK      (3 << DMA_CONTROL_RFA_SHIFT)
#define DMA_CONTROL_RSF           (1 << 25)  /* Receive Store and Forward */
#define DMA_CONTROL_DT            (1 << 26)  /* Disable Drop TCP/IP csum error */

/****************************************************************************
 * Pre-processor Definitions — DMA_STATUS (CSR5, 0x1014) Bits
 *
 * Reference: Manual §2.7
 ****************************************************************************/

#define DMA_STATUS_TI             (1 << 0)   /* Transmit Interrupt */
#define DMA_STATUS_TPS            (1 << 1)   /* Transmit Process Stopped */
#define DMA_STATUS_TU             (1 << 2)   /* Transmit Buffer Unavailable */
#define DMA_STATUS_TJT            (1 << 3)   /* Transmit Jabber Timeout */
#define DMA_STATUS_OVF            (1 << 4)   /* Receive Overflow */
#define DMA_STATUS_UNF            (1 << 5)   /* Transmit Underflow */
#define DMA_STATUS_RI             (1 << 6)   /* Receive Interrupt */
#define DMA_STATUS_RU             (1 << 7)   /* Receive Buffer Unavailable */
#define DMA_STATUS_RPS            (1 << 8)   /* Receive Process Stopped */
#define DMA_STATUS_RWT            (1 << 9)   /* Receive Watchdog Timeout */
#define DMA_STATUS_ETI            (1 << 10)  /* Early Transmit Interrupt */
#define DMA_STATUS_FBI            (1 << 11)  /* Fatal Bus Interrupt */
#define DMA_STATUS_ERI            (1 << 14)  /* Early Receive Interrupt */
#define DMA_STATUS_AIS            (1 << 15)  /* Abnormal Interrupt Summary */
#define DMA_STATUS_NIS            (1 << 16)  /* Normal Interrupt Summary */

/* Write-1-to-clear mask: bits 0-16 can be cleared by writing 1 */
#define DMA_STATUS_W1C_MASK       0x0001ffff

/****************************************************************************
 * Pre-processor Definitions — DMA_INTR_ENA (CSR7, 0x101C) Bits
 *
 * Reference: Manual §2.7
 ****************************************************************************/

#define DMA_INTR_TIE              (1 << 0)   /* Transmit Interrupt Enable */
#define DMA_INTR_TUE              (1 << 2)   /* Transmit Buffer Unavailable Enable */
#define DMA_INTR_RIE              (1 << 6)   /* Receive Interrupt Enable */
#define DMA_INTR_FBE              (1 << 11)  /* Fatal Bus Error Enable */
#define DMA_INTR_ERE              (1 << 14)  /* Early Receive Interrupt Enable */
#define DMA_INTR_AIE              (1 << 15)  /* Abnormal Interrupt Summary Enable */
#define DMA_INTR_NIE              (1 << 16)  /* Normal Interrupt Summary Enable */

/* Default interrupt mask from Linux stmmac driver (Manual §2.7, Appendix B)
 * NIE | RIE | TIE | AIE | FBE | TUE = 0x0001C07D
 */
#define DMA_INTR_DEFAULT_MASK     (DMA_INTR_NIE | DMA_INTR_RIE | \
                                   DMA_INTR_TIE | DMA_INTR_AIE | \
                                   DMA_INTR_FBE | DMA_INTR_TUE)

/****************************************************************************
 * Pre-processor Definitions — DMA_AXI_BUS_MODE (0x1028) Bits
 ****************************************************************************/

#define DMA_AXI_WR_OSR_LMT_SHIFT  0
#define DMA_AXI_RD_OSR_LMT_SHIFT  4
#define DMA_AXI_BLEN_SHIFT        1
#define DMA_AXI_BLEN_MASK         (0xf << DMA_AXI_BLEN_SHIFT)

/****************************************************************************
 * Pre-processor Definitions — Basic DMA Descriptor Fields
 *
 * CV181x uses basic descriptors (16 bytes, 4 × 32-bit words).
 * Enhanced descriptors are NOT used on this silicon.
 * Reference: Manual §3.1, §3.2, §3.3
 ****************************************************************************/

/* ---- TDES0: Transmit Descriptor Word 0 (Status/Control) ---- */

#define TDES0_DEFERRED            (1 << 0)   /* Frame deferred */
#define TDES0_UNDERFLOW_ERROR     (1 << 1)   /* Underflow error */
#define TDES0_EXCESSIVE_DEFERRAL  (1 << 2)   /* Excessive deferral */
#define TDES0_COLLISION_COUNT_SHIFT 3
#define TDES0_COLLISION_COUNT_MASK (0xf << TDES0_COLLISION_COUNT_SHIFT)
#define TDES0_VLAN_FRAME          (1 << 7)   /* VLAN frame */
#define TDES0_EXCESSIVE_COLLISIONS (1 << 8)  /* Excessive collisions */
#define TDES0_LATE_COLLISION      (1 << 9)   /* Late collision */
#define TDES0_NO_CARRIER          (1 << 10)  /* No carrier */
#define TDES0_LOSS_CARRIER        (1 << 11)  /* Loss of carrier */
#define TDES0_IP_PAYLOAD_ERROR    (1 << 12)  /* IP payload error */
#define TDES0_FRAME_FLUSHED       (1 << 13)  /* Frame flushed */
#define TDES0_JABBER_TIMEOUT      (1 << 14)  /* Jabber timeout */
#define TDES0_ERROR_SUMMARY       (1 << 15)  /* Error summary (ES) */
#define TDES0_IP_HEADER_ERROR     (1 << 16)  /* IP header error */
#define TDES0_TIME_STAMP_STATUS   (1 << 17)  /* Timestamp status */
#define TDES0_VLAN_TAG_INSERTED   (1 << 18)  /* VLAN tag inserted */
#define TDES0_CIC_SHIFT           27         /* Checksum Insertion Control */
#define TDES0_CIC_MASK            (3 << TDES0_CIC_SHIFT)
#define TDES0_CIC_DISABLE         0          /* Disable checksum insertion */
#define TDES0_CIC_IPHDR_ONLY      1          /* IP header only */
#define TDES0_CIC_IPHDR_PAYLOAD   2          /* IP header + payload */
#define TDES0_CIC_FULL            3          /* Full: IP hdr + pseudo-hdr + payload */
#define TDES0_OWN                 (1u << 31) /* DMA Ownership (1=DMA) */

/* ---- TDES1: Transmit Descriptor Word 1 (Buffer Sizes / Control) ---- */

#define TDES1_BUFFER1_SIZE_SHIFT  0
#define TDES1_BUFFER1_SIZE_MASK   0x000007ff
#define TDES1_BUFFER2_SIZE_SHIFT  11
#define TDES1_BUFFER2_SIZE_MASK   0x003ff800
#define TDES1_DISABLE_PADDING     (1 << 23)  /* Disable padding (DP) */
#define TDES1_SECOND_ADDR_CHAINED (1 << 24)  /* Second address chained */
#define TDES1_END_RING            (1 << 25)  /* End of Ring (TER) */
#define TDES1_CRC_DISABLE         (1 << 26)  /* Disable CRC */
#define TDES1_CIC_SHIFT           27         /* Checksum Insertion Control */
#define TDES1_CIC_MASK            (3 << TDES1_CIC_SHIFT)
#define TDES1_FIRST_SEGMENT       (1 << 29)  /* First Segment (FS) */
#define TDES1_LAST_SEGMENT        (1 << 30)  /* Last Segment (LS) */
#define TDES1_INTERRUPT           (1 << 31)  /* Interrupt on Completion (IC) */

/* Helper for TX checksum insertion (CIC field in TDES1) */
#define TX_CIC_FULL               3

/* ---- RDES0: Receive Descriptor Word 0 (Status) ---- */

#define RDES0_PAYLOAD_CSUM_ERROR  (1 << 0)   /* Payload checksum error */
#define RDES0_CRC_ERROR           (1 << 1)   /* CRC error */
#define RDES0_DRIBBLING           (1 << 2)   /* Dribbling bit error */
#define RDES0_MII_ERROR           (1 << 3)   /* MII/RX_ER error */
#define RDES0_RX_WATCHDOG_TIMEOUT (1 << 4)   /* Receive watchdog timeout */
#define RDES0_FRAME_TYPE          (1 << 5)   /* Frame type (0=802.3, 1=Ethernet) */
#define RDES0_LATE_COLLISION      (1 << 6)   /* Late collision */
#define RDES0_IPC_CSUM_ERROR      (1 << 7)   /* IPC checksum error */
#define RDES0_LAST_DESCRIPTOR     (1 << 8)   /* Last descriptor (LD) */
#define RDES0_FIRST_DESCRIPTOR    (1 << 9)   /* First descriptor (FD) */
#define RDES0_VLAN_TAG            (1 << 10)  /* VLAN frame tag */
#define RDES0_OVERFLOW_ERROR      (1 << 11)  /* Overflow error */
#define RDES0_LENGTH_ERROR        (1 << 12)  /* Length error */
#define RDES0_SA_FILTER_FAIL      (1 << 13)  /* Source address filter fail */
#define RDES0_DESCRIPTOR_ERROR    (1 << 14)  /* Descriptor error */
#define RDES0_ERROR_SUMMARY       (1 << 15)  /* Error summary (ES) */
#define RDES0_FRAME_LEN_SHIFT     16
#define RDES0_FRAME_LEN_MASK      0x3fff0000
#define RDES0_DA_FILTER_FAIL      (1 << 30)  /* Destination address filter fail */
#define RDES0_OWN                 (1u << 31) /* DMA Ownership (1=DMA) */

/* ---- RDES1: Receive Descriptor Word 1 (Buffer Size / Control) ---- */

#define RDES1_BUFFER1_SIZE_SHIFT  0
#define RDES1_BUFFER1_SIZE_MASK   0x000007ff
#define RDES1_BUFFER2_SIZE_SHIFT  11
#define RDES1_BUFFER2_SIZE_MASK   0x003ff800
#define RDES1_SECOND_ADDR_CHAINED (1 << 24)  /* Second address chained */
#define RDES1_END_RING            (1 << 25)  /* End of Ring (RER) */
#define RDES1_DISABLE_IC          (1 << 31)  /* Disable Interrupt on Completion */

/****************************************************************************
 * Pre-processor Definitions — EPHY (CV181x Built-in PHY)
 *
 * Reference: Manual §2.8, §10.3 Phase 1.2
 ****************************************************************************/

/* EPHY top register at +0x0 */
#define EPHY_TOP_SHUTDOWN         (1 << 0)   /* PHY shutdown */
#define EPHY_TOP_ANA_RST_N        (1 << 1)   /* ANA reset (active low) */
#define EPHY_TOP_DIG_RST_N        (1 << 2)   /* Digital reset (active low) */

/* EPHY top +0x4: Interface select */
#define EPHY_TOP_APB_SEL          (1 << 0)   /* 1=APB, 0=MDIO control */

/****************************************************************************
 * Pre-processor Definitions — PHY ID
 ****************************************************************************/

/* CVitek built-in EPHY ID */
#define CVITEK_PHY_ID             0x00435649

/****************************************************************************
 * Pre-processor Definitions — Descriptor Ring Configuration
 *
 * Reference: Manual §10.7
 ****************************************************************************/

/* Number of TX descriptors (must be power of 2).
 * Default 64 balances memory usage (~100KB per ring) with throughput.
 * The SG2002 kernel heap is limited (512KB), keep allocations modest.
 */
#ifndef CONFIG_SG2000_ETH_NTXDESC
#define TX_RING_SIZE              64
#else
#define TX_RING_SIZE              CONFIG_SG2000_ETH_NTXDESC
#endif

/* Number of RX descriptors (must be power of 2) */
#ifndef CONFIG_SG2000_ETH_NRXDESC
#define RX_RING_SIZE              64
#else
#define RX_RING_SIZE              CONFIG_SG2000_ETH_NRXDESC
#endif

/* Buffer sizes: standard Ethernet frame + alignment */
#define RX_BUF_SIZE               1536
#define TX_BUF_SIZE               1536
#define ETH_FCSSLEN               4       /* Ethernet FCS/CRC length */
#define SG2000_ETH_BUFSIZE        1600    /* Max frame buffer for copy */

/****************************************************************************
 * Pre-processor Definitions — PHY Link Status Check
 ****************************************************************************/

/* Timeouts in milliseconds */
#define PHY_RESET_TIMEOUT_MS      1000
#define PHY_AN_TIMEOUT_MS         5000
#define DMA_RESET_TIMEOUT_MS      200

#endif /* __VENDOR_SG2000_HARDWARE_SG2000_GMAC_H */
