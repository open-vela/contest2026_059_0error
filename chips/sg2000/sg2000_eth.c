/****************************************************************************
 * vendor/sg2000/chips/sg2000/sg2000_eth.c
 *
 * SG2002 (CV181x) DWMAC1000 Ethernet Driver for OpenVela
 * Implements the netdev_lowerhalf_s architecture.
 *
 * Reference: SG2002_CV181x_Net_Driver_Manual.md
 *
 * The SG2002/CV181x uses a Synopsys DesignWare GMAC v3.50a (DWMAC1000)
 * with a built-in CVitek EPHY connected via RMII.
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <debug.h>
#include <inttypes.h>
#include <stdbool.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/spinlock.h>
#include <nuttx/net/netdev_lowerhalf.h>

#include "sg2000_eth.h"

/****************************************************************************
 * Pre-processor Definitions — Debug
 ****************************************************************************/

#ifdef CONFIG_SG2000_ETH_DEBUG
#  define ethinfo(fmt, ...)  syslog(LOG_INFO, "eth: " fmt, ##__VA_ARGS__)
#  define etherr(fmt, ...)   syslog(LOG_ERR, "eth: ERROR: " fmt, ##__VA_ARGS__)
#else
#  define ethinfo(fmt, ...)
#  define etherr(fmt, ...)   syslog(LOG_ERR, "eth: ERROR: " fmt, ##__VA_ARGS__)
#endif

#define ethwarn(fmt, ...)    syslog(LOG_WARNING, "eth: WARN: " fmt, ##__VA_ARGS__)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* EPHY register-value pair for initialization sequences */

struct ephy_regval_s
{
  uint8_t offset;
  uint16_t value;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct sg2000_eth_priv_s g_eth_priv;
static bool g_eth_initialized;

/****************************************************************************
 * Private Functions — Register Access Helpers
 ****************************************************************************/

static inline uint32_t getreg32(uintptr_t addr)
{
  return *(volatile uint32_t *)addr;
}

static inline void putreg32(uint32_t val, uintptr_t addr)
{
  *(volatile uint32_t *)addr = val;
}

/****************************************************************************
 * Private Functions — Reset and Clock Control
 ****************************************************************************/

static void reset_assert(unsigned int id)
{
  unsigned int bank = id / 32;
  unsigned int bit  = id % 32;
  uint32_t reg;

  reg = getreg32(REG_RESET_BANK(bank));
  putreg32(reg & ~(1u << bit), REG_RESET_BANK(bank));
}

static void reset_deassert(unsigned int id)
{
  unsigned int bank = id / 32;
  unsigned int bit  = id % 32;
  uint32_t reg;

  reg = getreg32(REG_RESET_BANK(bank));
  putreg32(reg | (1u << bit), REG_RESET_BANK(bank));
}

static void clk_reset_deassert(unsigned int id)
{
  unsigned int bank = id / 32;
  unsigned int bit  = id % 32;
  uintptr_t addr = SG2000_CLKGEN_BASE + bank * 4;
  uint32_t reg;

  reg = getreg32(addr);
  putreg32(reg | (1u << bit), addr);
}

/****************************************************************************
 * Private Functions — Clock Enable
 *
 * Enables the GMAC 500MHz and AXI4 clocks, deasserts resets.
 * Reference: Manual §10.3 Phase 1.1
 ****************************************************************************/

static void gmac_clock_enable(void)
{
  uint32_t reg;

  /* Enable GMAC0 clocks: clk_500m_eth0 + clk_axi4_eth0 */

  reg = getreg32(REG_CLK_EN_0);
  reg |= CLK_EN_ETH0_500M | CLK_EN_ETH0_AXI4;
  putreg32(reg, REG_CLK_EN_0);

  /* Deassert clock-domain resets in the clkgen controller */

  clk_reset_deassert(CLK_RST_500M_ETH0);
  clk_reset_deassert(CLK_RST_AXI_ETH0);

  /* Release the main reset-controller lines for GMAC and built-in EPHY */

  reset_assert(RST_ETH0);
  reset_assert(RST_ETHPHY);
  reset_assert(RST_ETHPHYRST_APB);
  up_udelay(10);
  reset_deassert(RST_ETHPHYRST_APB);
  reset_deassert(RST_ETHPHY);
  reset_deassert(RST_ETH0);
  up_udelay(10);

  ethinfo("GMAC clocks enabled, CLK_EN_0=0x%08" PRIX32
          " RESET0=0x%08" PRIX32 " RESET3=0x%08" PRIX32 "\n",
          getreg32(REG_CLK_EN_0), getreg32(REG_RESET_BANK(0)),
          getreg32(REG_RESET_BANK(3)));
}

/****************************************************************************
 * Private Functions — D-Cache Operations (T-Head C906)
 *
 * The C906 has a 32KB 4-way set-associative D-Cache with 64-byte cache
 * lines using PIPT (Physical Index + Physical Tag).  DDR memory
 * (0x80000000-0xC0000000) is mapped as cacheable.  DMA descriptors and
 * buffers allocated from the kernel heap are therefore cacheable, and
 * explicit cache maintenance is REQUIRED before any DMA engine access.
 *
 * The Synopsys DWMAC1000 DMA engine is NOT cache-coherent on this SoC.
 *
 * T-Head custom instructions (from CV181x/SG2002 TRM):
 *   dcache.cpa  rs1  (0x0295000b)  – Clean (write back) by physical addr
 *   dcache.ipa  rs1  (0x02a5000b)  – Invalidate by physical addr
 *   dcache.cipa rs1  (0x02b5000b)  – Clean + Invalidate by physical addr
 *   sync.s           (0x01b0000b)  – Cache operation sync barrier
 ****************************************************************************/

static void dcache_clean_range(uintptr_t start, uintptr_t end)
{
  register uintptr_t i asm("a0") = start & ~(C906_CACHE_LINE_SIZE - 1);

  for (; i < end; i += C906_CACHE_LINE_SIZE)
    {
      asm volatile (".long 0x0295000b" ::: "memory");  /* dcache.cpa a0 */
    }

  asm volatile (".long 0x01b0000b" ::: "memory");      /* sync.s */
}

static void dcache_invalidate_range(uintptr_t start, uintptr_t end)
{
  register uintptr_t i asm("a0") = start & ~(C906_CACHE_LINE_SIZE - 1);

  for (; i < end; i += C906_CACHE_LINE_SIZE)
    {
      asm volatile (".long 0x02a5000b" ::: "memory");  /* dcache.ipa a0 */
    }

  asm volatile (".long 0x01b0000b" ::: "memory");      /* sync.s */
}

static void dcache_flush_range(uintptr_t start, uintptr_t end)
{
  register uintptr_t i asm("a0") = start & ~(C906_CACHE_LINE_SIZE - 1);

  for (; i < end; i += C906_CACHE_LINE_SIZE)
    {
      asm volatile (".long 0x02b5000b" ::: "memory");  /* dcache.cipa a0 */
    }

  asm volatile (".long 0x01b0000b" ::: "memory");      /* sync.s */
}

/****************************************************************************
 * Private Functions — DMA Software Reset
 *
 * Reference: Manual §4.4 Step 1, §10.3 Phase 2.1
 ****************************************************************************/

static int dma_soft_reset(uintptr_t gmac_base)
{
  uint32_t val;
  int timeout;

  val = getreg32(gmac_base + DMA_BUS_MODE);
  putreg32(val | DMA_SFT_RESET, gmac_base + DMA_BUS_MODE);

  timeout = (DMA_RESET_TIMEOUT_MS * 1000) / 10;  /* 10us per iteration */
  do
    {
      val = getreg32(gmac_base + DMA_BUS_MODE);
      if (!(val & DMA_SFT_RESET))
        {
          return OK;
        }

      up_udelay(10);
    }
  while (--timeout > 0);

  etherr("DMA reset timeout bus_mode=0x%08" PRIX32 " hw_feature=0x%08"
         PRIX32 "\n",
         getreg32(gmac_base + DMA_BUS_MODE),
         getreg32(gmac_base + DMA_HW_FEATURE));
  return -ETIMEDOUT;
}

/****************************************************************************
 * Private Functions — EPHY Initialization
 *
 * CV181x has a built-in EPHY that must be initialized before the GMAC
 * can communicate with it via MDIO.
 * Reference: Manual §2.8, §10.3 Phase 1.2
 ****************************************************************************/

static void ephy_write_seq(uintptr_t base, const struct ephy_regval_s *seq,
                           unsigned int count)
{
  unsigned int i;

  for (i = 0; i < count; i++)
    {
      putreg32(seq[i].value, base + seq[i].offset);
    }
}

static void ephy_led_pinmux(void)
{
  /* Match the CV181x U-Boot board-level EPHY LED/selphy setup.
   * Registers at 0x030010e0, 0x030010e4, 0x050270b0, 0x050270b4.
   */

  putreg32(0x05, 0x030010e0);
  putreg32(0x05, 0x030010e4);
  putreg32(0x11111111, 0x050270b0);
  putreg32(0x11111111, 0x050270b4);
}

static void ephy_full_analog_init(uintptr_t phy_base, uintptr_t phy_top)
{
  static const struct ephy_regval_s page16_100baset[] =
  {
    {0x68, 0x1000}, {0x6c, 0x3020}, {0x70, 0x5040}, {0x74, 0x7060},
    {0x58, 0x1708}, {0x5c, 0x3827}, {0x60, 0x5748}, {0x64, 0x7867},
  };

  static const struct ephy_regval_s page17_100baset[] =
  {
    {0x40, 0x9080}, {0x44, 0xb0a0}, {0x48, 0xd0c0}, {0x4c, 0xf0e0},
    {0x50, 0x9788}, {0x54, 0xb8a7}, {0x58, 0xd7c8}, {0x5c, 0xf8e7},
  };

  static const struct ephy_regval_s page10_link_pulse[] =
  {
    {0x40, 0x3e00}, {0x44, 0x7864}, {0x48, 0x6470}, {0x4c, 0x5f62},
    {0x50, 0x5a5a}, {0x54, 0x5458}, {0x58, 0xb23a}, {0x5c, 0x94a0},
    {0x60, 0x9092}, {0x64, 0x8a8e}, {0x68, 0x8688}, {0x6c, 0x8484},
    {0x70, 0x0082},
  };

  static const struct ephy_regval_s page11_tp_idle[] =
  {
    {0x40, 0x5252}, {0x44, 0x5252}, {0x48, 0x4b52}, {0x4c, 0x3d47},
    {0x50, 0xaa99}, {0x54, 0x989e}, {0x58, 0x9395}, {0x5c, 0x9091},
    {0x60, 0x8e8f}, {0x64, 0x8d8e}, {0x68, 0x8c8c}, {0x6c, 0x8b8b},
    {0x70, 0x008a},
  };

  static const struct ephy_regval_s page13_10baset[] =
  {
    {0x40, 0x1e0a}, {0x44, 0x3862}, {0x48, 0x1e62}, {0x4c, 0x2a08},
    {0x50, 0x244c}, {0x54, 0x1a44}, {0x58, 0x061c},
  };

  static const struct ephy_regval_s page14_10baset[] =
  {
    {0x40, 0x2d30}, {0x44, 0x3470}, {0x48, 0x0648}, {0x4c, 0x261c},
    {0x50, 0x3160}, {0x54, 0x2d5e},
  };

  static const struct ephy_regval_s page15_10baset[] =
  {
    {0x40, 0x2922}, {0x44, 0x366e}, {0x48, 0x0752}, {0x4c, 0x2556},
    {0x50, 0x2348}, {0x54, 0x0c30},
  };

  static const struct ephy_regval_s page16_10baset[] =
  {
    {0x40, 0x1e08}, {0x44, 0x3868}, {0x48, 0x1462}, {0x4c, 0x1a0e},
    {0x50, 0x305e}, {0x54, 0x2f62},
  };

  static const struct ephy_regval_s page18_cv181x[] =
  {
    {0x48, 0x0808}, {0x4c, 0x0808}, {0x50, 0x32f8}, {0x54, 0xf8dc},
  };

  /* Switch to page 0 */
  putreg32(0x0001, phy_top + 0x04);
  putreg32(0x0000, phy_base + 0x7c);

  /* Page 5: Base configuration */
  putreg32(0x0500, phy_base + 0x7c);
  putreg32(0x5a5a, phy_base + 0x64);
  putreg32(0x0000, phy_base + 0x54);
  putreg32(0x0bb0, phy_base + 0x58);
  putreg32(0x0c10, phy_base + 0x5c);
  putreg32(0x0003, phy_base + 0x68);
  putreg32(0x0000, phy_base + 0x54);

  /* Page 16: 100BASE-T configuration */
  putreg32(0x1000, phy_base + 0x7c);
  ephy_write_seq(phy_base, page16_100baset,
                 sizeof(page16_100baset) / sizeof(page16_100baset[0]));

  /* Page 17: more 100BASE-T */
  putreg32(0x1100, phy_base + 0x7c);
  ephy_write_seq(phy_base, page17_100baset,
                 sizeof(page17_100baset) / sizeof(page17_100baset[0]));

  /* Page 5: calibration */
  putreg32(0x0500, phy_base + 0x7c);
  putreg32(getreg32(phy_base + 0x40) | 0x0001, phy_base + 0x40);
  putreg32(getreg32(phy_base + 0x4c) | 0x0820, phy_base + 0x4c);

  /* Page 10: link pulse */
  putreg32(0x0a00, phy_base + 0x7c);
  ephy_write_seq(phy_base, page10_link_pulse,
                 sizeof(page10_link_pulse) / sizeof(page10_link_pulse[0]));

  /* Page 11: TP idle */
  putreg32(0x0b00, phy_base + 0x7c);
  ephy_write_seq(phy_base, page11_tp_idle,
                 sizeof(page11_tp_idle) / sizeof(page11_tp_idle[0]));

  /* Page 13-16: 10BASE-T */
  putreg32(0x0d00, phy_base + 0x7c);
  ephy_write_seq(phy_base, page13_10baset,
                 sizeof(page13_10baset) / sizeof(page13_10baset[0]));

  putreg32(0x0e00, phy_base + 0x7c);
  ephy_write_seq(phy_base, page14_10baset,
                 sizeof(page14_10baset) / sizeof(page14_10baset[0]));

  putreg32(0x0f00, phy_base + 0x7c);
  ephy_write_seq(phy_base, page15_10baset,
                 sizeof(page15_10baset) / sizeof(page15_10baset[0]));

  putreg32(0x1000, phy_base + 0x7c);
  ephy_write_seq(phy_base, page16_10baset,
                 sizeof(page16_10baset) / sizeof(page16_10baset[0]));

  /* Page 1: additional tuning */
  putreg32(0x0100, phy_base + 0x7c);
  putreg32(getreg32(phy_base + 0x68) & ~0x0f00, phy_base + 0x68);

  /* Page 19: misc */
  putreg32(0x1300, phy_base + 0x7c);
  putreg32(0x0012, phy_base + 0x58);
  putreg32(0x6848, phy_base + 0x5c);

  /* Page 18: CV181x specific */
  putreg32(0x1200, phy_base + 0x7c);
  ephy_write_seq(phy_base, page18_cv181x,
                 sizeof(page18_cv181x) / sizeof(page18_cv181x[0]));

  /* Return to page 0, start auto-negotiation */
  putreg32(0x0000, phy_base + 0x7c);
  putreg32(0x090e, phy_top);
  putreg32(getreg32(phy_base) | 0x0100, phy_base);
}

/****************************************************************************
 * Name: ephy_hardware_reset
 *
 * Description:
 *   Minimal EPHY init: release resets, set PHY ID, switch to MDIO control.
 *   Matches Linux bm_eth_reset_phy() in dwmac-cvitek.c.
 *   Does NOT run the full analog tuning sequence — that is done after
 *   MDIO is confirmed operational.
 *
 * Reference: Manual §2.8, §10.3 Phase 1.2
 ****************************************************************************/

static void ephy_hardware_reset(uintptr_t phy_base, uintptr_t phy_top)
{
  ephy_led_pinmux();

  /* Select APB interface to access EPHY registers */

  putreg32(0x0001, phy_top + 0x4);

  /* Release shutdown */

  putreg32(0x0900, phy_top);

  /* Release dig_rst_n — enables MII register access via APB */

  putreg32(0x0904, phy_top);

  /* Switch to Page 5 for ANA initialization */

  putreg32(0x0500, phy_base + 0x7c);

  /* ANA_PD (power-down release) */

  putreg32(0x0c00, phy_base + 0x40);

  /* ANA_EN */

  putreg32(0x0c7e, phy_base + 0x40);
  up_mdelay(1);

  /* Release ana_rst_n */

  putreg32(0x0906, phy_top);
  putreg32(0x0500, phy_base + 0x7c);

  /* Program PHY ID into EPHY registers (so MDIO can discover it) */

  putreg32(0x0043, phy_base + 0x08);  /* PHY_ID_HIGH */
  putreg32(0x5649, phy_base + 0x0c);  /* PHY_ID_LOW */

  ethinfo("EPHY APB PHYID=0x%04" PRIX32 "%04" PRIX32 "\n",
          getreg32(phy_base + 0x08) & 0xffff,
          getreg32(phy_base + 0x0c) & 0xffff);

  /* Switch control to MDIO (GMAC) so the DWMAC1000 MDIO controller can
   * discover and communicate with the PHY.
   */

  putreg32(0x0000, phy_top + 0x4);
  up_mdelay(10);

  ethinfo("EPHY init done top=0x%08" PRIX32 " sel=0x%08" PRIX32 "\n",
          getreg32(phy_top), getreg32(phy_top + 0x4));
}

/****************************************************************************
 * Name: ephy_full_tuning
 *
 * Description:
 *   Run the full CV182XA analog tuning sequence via APB, then switch back
 *   to MDIO control.  Matches Linux cv182xa_phy_config_init().
 *   MUST be called AFTER MDIO is confirmed operational, as it accesses
 *   internal pages and starts auto-negotiation.
 *
 * Reference: Manual §10.3 Phase 1.2
 ****************************************************************************/

static void ephy_full_tuning(uintptr_t phy_base, uintptr_t phy_top)
{
  /* Switch to APB for tuning */

  putreg32(0x0001, phy_top + 0x4);
  ephy_full_analog_init(phy_base, phy_top);

  /* ephy_full_analog_init ends with:
   *   page 0 + autoneg start (0x090e) + force full-duplex
   * Switch control back to MDIO so the GMAC can manage the PHY.
   */

  putreg32(0x0000, phy_top + 0x4);
  up_mdelay(10);

  ethinfo("EPHY full tuning done, top=0x%08" PRIX32 " sel=0x%08" PRIX32 "\n",
          getreg32(phy_top), getreg32(phy_top + 0x4));
}

/****************************************************************************
 * Private Functions — MDIO (MII Management Interface)
 *
 * CV181x uses the TRM Chapter 18 legacy GMAC MDIO layout at offsets
 * 0x10/0x14.  The standard DWMAC4 MDIO controller at 0x200/0x204 is
 * NOT functional on this silicon.
 *
 * Reference: Manual §8.2, §8.3, §10.10
 ****************************************************************************/

static int mdio_wait_ready(uintptr_t gmac_base)
{
  int timeout = 10000;

  do
    {
      if (!(getreg32(gmac_base + GMAC_MII_ADDR) & GMAC_MII_ADDR_BUSY))
        {
          return OK;
        }

      up_udelay(1);
    }
  while (--timeout > 0);

  return -ETIMEDOUT;
}

static uint16_t mdio_read(uintptr_t gmac_base, uint8_t phy_addr,
                          uint8_t reg_addr)
{
  uint32_t cmd;

  if (mdio_wait_ready(gmac_base) < 0)
    {
      return 0xffff;
    }

  putreg32(0, gmac_base + GMAC_MII_DATA);

  cmd = (((uint32_t)phy_addr & 0x1f) << GMAC_MII_ADDR_PHY_SHIFT) |
        (((uint32_t)reg_addr & 0x1f) << GMAC_MII_ADDR_REG_SHIFT) |
        GMAC_MII_ADDR_CR_250_300M | GMAC_MII_ADDR_BUSY;

  putreg32(cmd, gmac_base + GMAC_MII_ADDR);

  if (mdio_wait_ready(gmac_base) < 0)
    {
      return 0xffff;
    }

  return (uint16_t)(getreg32(gmac_base + GMAC_MII_DATA) & 0xffff);
}

static void mdio_write(uintptr_t gmac_base, uint8_t phy_addr,
                       uint8_t reg_addr, uint16_t data)
{
  uint32_t cmd;

  if (mdio_wait_ready(gmac_base) < 0)
    {
      return;
    }

  putreg32(data, gmac_base + GMAC_MII_DATA);

  cmd = (((uint32_t)phy_addr & 0x1f) << GMAC_MII_ADDR_PHY_SHIFT) |
        (((uint32_t)reg_addr & 0x1f) << GMAC_MII_ADDR_REG_SHIFT) |
        GMAC_MII_ADDR_CR_250_300M | GMAC_MII_ADDR_WRITE | GMAC_MII_ADDR_BUSY;

  putreg32(cmd, gmac_base + GMAC_MII_ADDR);
  mdio_wait_ready(gmac_base);
}

/****************************************************************************
 * Name: mdio_verify_phy
 *
 * Description:
 *   Read PHY ID via MDIO to verify the PHY is accessible.
 *
 * Reference: Manual §10.3
 ****************************************************************************/

static int mdio_verify_phy(uintptr_t gmac_base)
{
  uint16_t id1;
  uint16_t id2;
  uint32_t phy_id;

  id1 = mdio_read(gmac_base, 0, 2);  /* PHY ID 1 register */
  id2 = mdio_read(gmac_base, 0, 3);  /* PHY ID 2 register */
  phy_id = ((uint32_t)id1 << 16) | id2;

  ethinfo("MDIO PHY ID=0x%08" PRIX32 " (expected 0x%08" PRIX32 ")\n",
          phy_id, (uint32_t)CVITEK_PHY_ID);

  if (id1 == 0xffff || id2 == 0xffff)
    {
      etherr("MDIO timeout reading PHY ID\n");
      return -ENODEV;
    }

  if (phy_id != CVITEK_PHY_ID)
    {
      ethwarn("Unexpected PHY ID 0x%08" PRIX32 " (expected 0x%08"
              PRIX32 ")\n", phy_id, (uint32_t)CVITEK_PHY_ID);
    }

  return OK;
}

/****************************************************************************
 * Private Functions — PHY Configuration (Auto-Negotiation)
 *
 * Resets the PHY, starts auto-negotiation, waits for link, and reads
 * the negotiated speed and duplex mode.
 *
 * Reference: Manual §10.3 (part of Phase 7)
 ****************************************************************************/

static int phy_config_init(struct sg2000_eth_priv_s *priv)
{
  uintptr_t gmac_base = priv->gmac_base;
  uint16_t bmsr;
  uint16_t bmcr;
  uint16_t anlpar;
  int timeout;

  /* Reset PHY */

  mdio_write(gmac_base, 0, 0, 0x8000);  /* BMCR reset */
  timeout = (PHY_RESET_TIMEOUT_MS * 1000) / 1000;
  do
    {
      up_mdelay(1);
      bmcr = mdio_read(gmac_base, 0, 0);
      if (!(bmcr & 0x8000))
        {
          break;
        }
    }
  while (--timeout > 0);

  ethinfo("PHY reset done, BMCR=0x%04" PRIX16 "\n", bmcr);

  /* Advertise capabilities: 100BASE-TX full/half, 10BASE-T full/half */

  mdio_write(gmac_base, 0, 4, 0x01e1);

  /* Start auto-negotiation */

  bmcr = mdio_read(gmac_base, 0, 0);
  mdio_write(gmac_base, 0, 0, bmcr | 0x1200);  /* AN enable + restart AN */
  ethinfo("Autoneg start, BMCR=0x%04" PRIX16 "\n", bmcr | 0x1200);

  /* Wait for link up */

  timeout = PHY_AN_TIMEOUT_MS / 100;
  do
    {
      up_mdelay(100);
      bmsr = mdio_read(gmac_base, 0, 1);
      if (bmsr & 0x0004)  /* Link Status bit */
        {
          break;
        }
    }
  while (--timeout > 0);

  if (!(bmsr & 0x0004))
    {
      ethwarn("PHY link down, BMSR=0x%04" PRIX16 "\n", bmsr);
      priv->link_up = 0;
      return OK;
    }

  /* Read negotiated speed/duplex from Link Partner Ability */

  anlpar = mdio_read(gmac_base, 0, 5);
  ethinfo("PHY link up, BMSR=0x%04" PRIX16 " ANLPAR=0x%04" PRIX16 "\n",
          bmsr, anlpar);

  /* Determine speed and duplex from auto-negotiation result */

  if (anlpar & 0x0100)          /* 100BASE-TX Full Duplex */
    {
      priv->speed  = 100;
      priv->duplex = 1;
    }
  else if (anlpar & 0x0080)     /* 100BASE-TX Half Duplex */
    {
      priv->speed  = 100;
      priv->duplex = 0;
    }
  else if (anlpar & 0x0040)     /* 10BASE-T Full Duplex */
    {
      priv->speed  = 10;
      priv->duplex = 1;
    }
  else                          /* 10BASE-T Half Duplex (default) */
    {
      priv->speed  = 10;
      priv->duplex = 0;
    }

  priv->link_up = 1;

  ethinfo("Negotiated: %d Mbps %s duplex\n",
          priv->speed, priv->duplex ? "Full" : "Half");

  return OK;
}

/****************************************************************************
 * Private Functions — DMA Descriptor Allocation
 *
 * Allocates TX and RX descriptor rings and RX data buffers.
 * All memory is 64-byte aligned for C906 D-Cache line isolation.
 *
 * Reference: Manual §10.3 Phase 3, §10.7
 ****************************************************************************/

static int alloc_dma(struct sg2000_eth_priv_s *priv)
{
  unsigned int i;
  size_t tx_desc_sz;
  size_t rx_desc_sz;

  if (priv->dma_allocated)
    {
      return OK;
    }

  tx_desc_sz = TX_RING_SIZE * sizeof(struct sg2000_dma_desc_s);
  rx_desc_sz = RX_RING_SIZE * sizeof(struct sg2000_dma_desc_s);

  /* Allocate descriptor rings with cache-line alignment */

  priv->tx_desc = (struct sg2000_dma_desc_s *)
    kmm_memalign(C906_CACHE_LINE_SIZE, tx_desc_sz);
  priv->rx_desc = (struct sg2000_dma_desc_s *)
    kmm_memalign(C906_CACHE_LINE_SIZE, rx_desc_sz);

  if (!priv->tx_desc || !priv->rx_desc)
    {
      etherr("Failed to allocate DMA descriptor rings\n");
      return -ENOMEM;
    }

  memset(priv->tx_desc, 0, tx_desc_sz);
  memset(priv->rx_desc, 0, rx_desc_sz);

  /* For identity-mapped builds, virtual = physical */

  priv->tx_desc_phys = (uintptr_t)priv->tx_desc;
  priv->rx_desc_phys = (uintptr_t)priv->rx_desc;

  /* Allocate TX data buffers */

  for (i = 0; i < TX_RING_SIZE; i++)
    {
      priv->tx_buf[i] = (uint8_t *)
        kmm_memalign(C906_CACHE_LINE_SIZE, TX_BUF_SIZE);
      if (!priv->tx_buf[i])
        {
          etherr("Failed to allocate TX buffer %u\n", i);
          return -ENOMEM;
        }

      priv->tx_buf_phys[i] = (uintptr_t)priv->tx_buf[i];
      priv->tx_pkt[i] = NULL;
    }

  /* Allocate RX data buffers */

  for (i = 0; i < RX_RING_SIZE; i++)
    {
      priv->rx_buf[i] = (uint8_t *)
        kmm_memalign(C906_CACHE_LINE_SIZE, RX_BUF_SIZE);
      if (!priv->rx_buf[i])
        {
          etherr("Failed to allocate RX buffer %u\n", i);
          return -ENOMEM;
        }

      priv->rx_buf_phys[i] = (uintptr_t)priv->rx_buf[i];
    }

  priv->dma_allocated = true;

  ethinfo("DMA: TX ring %u descs @0x%08" PRIX32
          " RX ring %u descs @0x%08" PRIX32 "\n",
          (unsigned int)TX_RING_SIZE, (uint32_t)priv->tx_desc_phys,
          (unsigned int)RX_RING_SIZE, (uint32_t)priv->rx_desc_phys);

  return OK;
}

/****************************************************************************
 * Private Functions — Descriptor Ring Initialization
 *
 * Reference: Manual §3.5, §10.3 Phase 4
 ****************************************************************************/

static void init_desc_rings(struct sg2000_eth_priv_s *priv)
{
  unsigned int i;
  unsigned int last_rx = RX_RING_SIZE - 1;

  /* Initialize TX descriptors: OWN=0 (CPU owns all).
   * CV181x Ring Mode convention: END_RING=1 on ALL TX descriptors.
   * DMA uses DMA_TX_BASE_ADDR + ring length for wrapping, not END_RING.
   * For basic descriptors (ATDS=0), END_RING is in TDES1 bit 25.
   */

  for (i = 0; i < TX_RING_SIZE; i++)
    {
      priv->tx_desc[i].des0 = 0;
      priv->tx_desc[i].des1 = (i == (TX_RING_SIZE - 1)) ? TDES1_END_RING : 0;
      priv->tx_desc[i].des2 = priv->tx_buf_phys[i];
      priv->tx_desc[i].des3 = 0;
      priv->tx_pkt[i] = NULL;
    }
    priv->tx_desc[TX_RING_SIZE - 1].des3 = &priv->tx_desc[0];  /* Ring wrap pointer */

  /* Flush TX descriptor ring so DMA sees initialized state */

  dcache_clean_range((uintptr_t)priv->tx_desc,
                     (uintptr_t)(priv->tx_desc + TX_RING_SIZE));

  /* Initialize RX descriptors: OWN=1 (DMA owns all), END_RING on last.
   * For basic descriptors (ATDS=0), END_RING is in RDES1 bit 25.
   */

  for (i = 0; i < RX_RING_SIZE; i++)
    {
      priv->rx_desc[i].des0 = RDES0_OWN;
      priv->rx_desc[i].des1 = (RX_BUF_SIZE & RDES1_BUFFER1_SIZE_MASK) |
                              ((i == last_rx) ? RDES1_END_RING : 0);
      priv->rx_desc[i].des2 = priv->rx_buf_phys[i];
      priv->rx_desc[i].des3 = 0;
    }
    priv->rx_desc[last_rx].des3 = &priv->rx_desc[0];  /* Ring wrap pointer */

  /* Flush RX descriptor ring and data buffers before DMA touches them */

  dcache_clean_range((uintptr_t)priv->rx_desc,
                     (uintptr_t)(priv->rx_desc + RX_RING_SIZE));

  for (i = 0; i < RX_RING_SIZE; i++)
    {
      dcache_flush_range((uintptr_t)priv->rx_buf[i],
                         (uintptr_t)priv->rx_buf[i] + RX_BUF_SIZE);
    }

  /* Reset ring indices */

  priv->tx_cur   = 0;
  priv->tx_dirty = 0;
  priv->rx_cur   = 0;

  ethinfo("Descriptor rings initialized\n");
}

/****************************************************************************
 * Private Functions — MAC Address Configuration
 *
 * Reference: Manual §4.4 Step 6, §10.3 Phase 5.3
 ****************************************************************************/

static void set_mac_addr(uintptr_t gmac_base, const uint8_t *mac)
{
  uint32_t hi;
  uint32_t lo;

  hi = ((uint32_t)mac[5] << 8) | (uint32_t)mac[4] | GMAC_HI_REG_AE;
  lo = ((uint32_t)mac[3] << 24) | ((uint32_t)mac[2] << 16) |
       ((uint32_t)mac[1] << 8)  | (uint32_t)mac[0];

  putreg32(hi, gmac_base + GMAC_MAC_ADDR0_HIGH);
  putreg32(lo, gmac_base + GMAC_MAC_ADDR0_LOW);

  ethinfo("MAC addr set: %02x:%02x:%02x:%02x:%02x:%02x\n",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/****************************************************************************
 * Private Functions — MAC Core and DMA Initialization
 *
 * Performs the full hardware initialization sequence:
 *   - DMA bus mode and AXI configuration
 *   - MAC core init (GMAC_CONTROL, interrupt mask, frame filter)
 *   - DMA operation mode (Store-and-Forward)
 *   - Interrupt enable
 *   - Write descriptor base addresses
 *   - Start DMA TX and RX
 *
 * Reference: Manual §10.3 Phase 2, 5, 6, 7
 ****************************************************************************/

static void gmac_set_speed_duplex(uintptr_t gmac_base, int speed, int duplex);

static int hw_setup(struct sg2000_eth_priv_s *priv)
{
  uintptr_t gmac_base = priv->gmac_base;
  uint32_t val;

  /* --- Phase 2: DMA software reset ---
   * Note: This resets the entire GMAC block including GMAC_CONTROL.
   * Speed/duplex must be configured AFTER this point.
   */

  if (dma_soft_reset(gmac_base) < 0)
    {
      return -ETIMEDOUT;
    }

  /* --- Phase 2.1: Apply negotiated speed/duplex to GMAC ---
   * Must be done AFTER dma_soft_reset() which clears GMAC_CONTROL.
   */

  gmac_set_speed_duplex(gmac_base, priv->speed, priv->duplex);

  /* --- Phase 2.2: Configure DMA Bus Mode ---
   * PBL=8, RPBL=8, FB=1 (fixed burst), USP=1 (separate PBL),
   * AAL=1 (address-aligned beats).  ATDS is NOT set (basic descriptors).
   * Use explicit constants; (DMA_PBL_8 & 0x3f) would be 0 (PBL field is
   * at bits 8-13, masking with 0x3f gives 0).
   */

  val = DMA_PBL_8 | ((3 << DMA_RPBL_SHIFT) & DMA_RPBL_MASK) |
        DMA_FB | DMA_USP | DMA_AAL;
  putreg32(val, gmac_base + DMA_BUS_MODE);

  /* --- Phase 2.3: Configure AXI bus ---
   * WR_OSR_LMT=1, RD_OSR_LMT=2, BLEN support for 4/8/16 beats.
   */

  putreg32((1 << DMA_AXI_WR_OSR_LMT_SHIFT) |
           (2 << DMA_AXI_RD_OSR_LMT_SHIFT) |
           (0xf << DMA_AXI_BLEN_SHIFT),
           gmac_base + DMA_AXI_BUS_MODE);

  /* --- Phase 5.1: Configure GMAC_CONTROL ---
   * Use read-modify-write to preserve speed/duplex (FES, DM) already
   * set by gmac_set_speed_duplex() before hw_setup() was called.
   */

  val = getreg32(gmac_base + GMAC_CONTROL);
  putreg32(val | GMAC_CORE_INIT, gmac_base + GMAC_CONTROL);

  /* --- Phase 5.2: Configure GMAC_INT_MASK (disable all MAC interrupts,
   *   use DMA interrupts only) ---
   */

  putreg32(GMAC_INT_DEFAULT_MASK, gmac_base + GMAC_INT_MASK);

  /* --- Phase 5.4: Configure frame filter ---
   * Use promiscuous mode (PR) to accept all packets.  This matches the
   * old driver's setup and avoids MAC address filtering issues.
   */

  putreg32(GMAC_FILTER_PR | GMAC_FILTER_HMC | GMAC_FILTER_PM,
           gmac_base + GMAC_FRAME_FILTER);

  /* Clear VLAN tag */

  putreg32(0, gmac_base + GMAC_VLAN_TAG);

  /* Clear pending MAC and DMA interrupts */

  putreg32(0xffffffff, gmac_base + GMAC_INT_STATUS);
  putreg32(DMA_STATUS_W1C_MASK, gmac_base + DMA_STATUS);

  /* --- Phase 5.5: Enable MAC TX and RX --- */

  val = getreg32(gmac_base + GMAC_CONTROL);
  putreg32(val | GMAC_CONTROL_TE | GMAC_CONTROL_RE, gmac_base + GMAC_CONTROL);

  /* --- Phase 6.1: Configure DMA operation mode ---
   * RSF=1 (RX Store-and-Forward), TSF=1 (TX Store-and-Forward),
   * OSF=1 (Operate on Second Frame), RTC=64, TTC=64.
   */

  val = getreg32(gmac_base + DMA_CONTROL);
  val |= DMA_CONTROL_RSF | DMA_CONTROL_TSF | DMA_CONTROL_OSF |
         DMA_CONTROL_RTC_64 | DMA_CONTROL_TTC_64;
  putreg32(val, gmac_base + DMA_CONTROL);

  /* --- Phase 6.2: Enable DMA interrupts --- */

  putreg32(DMA_INTR_DEFAULT_MASK, gmac_base + DMA_INTR_ENA);

  /* --- Phase 7.1: Write descriptor base addresses --- */

  putreg32(priv->tx_desc_phys, gmac_base + DMA_TX_BASE_ADDR);
  putreg32(priv->rx_desc_phys, gmac_base + DMA_RCV_BASE_ADDR);

  /* --- Phase 7.2-7.3: Start DMA TX and RX ---
   * Write ST first, then SR separately with a delay.  The hardware
   * may ignore SR if written simultaneously with ST.
   */

  val = getreg32(gmac_base + DMA_CONTROL);
  putreg32(val | DMA_CONTROL_ST, gmac_base + DMA_CONTROL);
  up_udelay(10);

  val = getreg32(gmac_base + DMA_CONTROL);
  putreg32(val | DMA_CONTROL_SR, gmac_base + DMA_CONTROL);
  up_udelay(10);

  /* Log critical register state for debugging */

  {
    uint32_t dma_status = getreg32(gmac_base + DMA_STATUS);
    uint32_t rs = (dma_status >> 17) & 0x7;

    ethinfo("hw_setup done: gmac_ctrl=0x%08" PRIX32
            " dma_ctrl=0x%08" PRIX32
            " flt=0x%08" PRIX32
            " dma_status=0x%08" PRIX32
            " RS=%s\n",
            getreg32(gmac_base + GMAC_CONTROL),
            getreg32(gmac_base + DMA_CONTROL),
            getreg32(gmac_base + GMAC_FRAME_FILTER),
            dma_status,
            rs == 3 ? "WaitPkt" : rs == 0 ? "Stopped" : "other");
  }

  return OK;
}

/****************************************************************************
 * Name: gmac_set_speed_duplex
 *
 * Description:
 *   Configure GMAC speed and duplex mode based on PHY negotiation result.
 *
 * Reference: Manual §10.9
 ****************************************************************************/

static void gmac_set_speed_duplex(uintptr_t gmac_base, int speed, int duplex)
{
  uint32_t val;

  val = getreg32(gmac_base + GMAC_CONTROL);
  val &= ~(GMAC_CONTROL_FES | GMAC_CONTROL_DM);

  if (speed == 100)
    {
      val |= GMAC_CONTROL_FES;
    }

  if (duplex)
    {
      val |= GMAC_CONTROL_DM;
    }

  putreg32(val, gmac_base + GMAC_CONTROL);
}

/****************************************************************************
 * Private Functions — DMA Doorbell
 *
 * Writing any value to DMA_XMT_POLL_DEMAND / DMA_RCV_POLL_DEMAND
 * triggers the DMA engine to re-fetch descriptors from the ring.
 *
 * Reference: Manual §2.3, §10.4, §10.5
 ****************************************************************************/

static void tx_doorbell(uintptr_t gmac_base)
{
  putreg32(0, gmac_base + DMA_XMT_POLL_DEMAND);
}

static void rx_doorbell(uintptr_t gmac_base)
{
  putreg32(0, gmac_base + DMA_RCV_POLL_DEMAND);
}

/****************************************************************************
 * Private Functions — Hardware Transmit
 *
 * Prepares a TX descriptor, copies data to the DMA buffer, and signals
 * the DMA engine to transmit.
 *
 * Reference: Manual §5.2, §10.4
 ****************************************************************************/

static int hw_transmit(struct sg2000_eth_priv_s *priv,
                       const uint8_t *data, unsigned int len)
{
  uintptr_t gmac_base = priv->gmac_base;
  unsigned int idx = priv->tx_cur;
  struct sg2000_dma_desc_s *desc = &priv->tx_desc[idx];
  irqstate_t flags;

  /* Check if ring is full */

  if (desc->des0 & TDES0_OWN)
    {
      return -EBUSY;
    }

  /* Copy data to pre-allocated TX DMA buffer.
   * Skip if data already points to our DMA buffer (fragmented packets
   * are pre-copied by netdriver_transmit).
   */

  if (data != priv->tx_buf[idx])
    {
      memcpy(priv->tx_buf[idx], data, len);
    }

  /* Write back TX buffer so DMA can see packet data */

  dcache_clean_range((uintptr_t)priv->tx_buf[idx],
                     (uintptr_t)priv->tx_buf[idx] + len);

  /* Fill descriptor (basic format, 16 bytes):
   *   des0: OWN + IC
   *   des1: buffer size + FS + LS + END_RING + IC
   *   des2: buffer physical address
   */

  desc->des2 = priv->tx_buf_phys[idx];
  desc->des1 = (len & TDES1_BUFFER1_SIZE_MASK) |
               TDES1_FIRST_SEGMENT | TDES1_LAST_SEGMENT |
               ((idx == (TX_RING_SIZE - 1)) ? TDES1_END_RING : 0) |
               TDES1_INTERRUPT;

  /* Memory barrier: ensure descriptor writes are visible to DMA
   * before setting OWN=1.
   */

  dcache_clean_range((uintptr_t)desc,
                     (uintptr_t)desc + sizeof(struct sg2000_dma_desc_s));
  __asm__ volatile ("fence w,w" ::: "memory");

  /* Give ownership to DMA.  IC and END_RING are set in des1 above. */

  desc->des0 = TDES0_OWN;

  dcache_clean_range((uintptr_t)desc,
                     (uintptr_t)desc + sizeof(struct sg2000_dma_desc_s));

  /* Advance TX pointer and ring doorbell */

  flags = up_irq_save();
  priv->tx_cur = (idx + 1) & (TX_RING_SIZE - 1);
  tx_doorbell(gmac_base);
  up_irq_restore(flags);

  return OK;
}

/****************************************************************************
 * Private Functions — TX Cleanup
 *
 * Frees completed TX packets by walking from dirty_tx to cur_tx.
 * Called from ISR on TX Complete interrupt.
 *
 * Reference: Manual §5.3, §10.4 step 7
 ****************************************************************************/

static void tx_cleanup(struct sg2000_eth_priv_s *priv)
{
  irqstate_t flags = up_irq_save();

  while (priv->tx_dirty != priv->tx_cur)
    {
      struct sg2000_dma_desc_s *desc = &priv->tx_desc[priv->tx_dirty];

      /* Invalidate descriptor cache line to read DMA-written status */

      dcache_invalidate_range((uintptr_t)desc,
                              (uintptr_t)desc +
                              sizeof(struct sg2000_dma_desc_s));

      if (desc->des0 & TDES0_OWN)
        {
          break;  /* Still owned by DMA */
        }

      /* Check for TX errors */

      if (desc->des0 & TDES0_ERROR_SUMMARY)
        {
          etherr("TX error desc[%u] status=0x%08" PRIX32 "\n",
                 priv->tx_dirty, desc->des0);
        }

      /* Free the tracked netpkt and notify upper half */

      if (priv->tx_pkt[priv->tx_dirty] != NULL)
        {
          netpkt_free(&priv->dev, priv->tx_pkt[priv->tx_dirty], NETPKT_TX);
          priv->tx_pkt[priv->tx_dirty] = NULL;
          netdev_lower_txdone(&priv->dev);
        }

      /* Clear descriptor for reuse, END_RING=1 on all (CV181x convention) */

      desc->des0 = 0;
      desc->des1 = (priv->tx_dirty == (TX_RING_SIZE - 1)) ?
                   TDES1_END_RING : 0;

      dcache_clean_range((uintptr_t)desc,
                         (uintptr_t)desc +
                         sizeof(struct sg2000_dma_desc_s));

      priv->tx_dirty = (priv->tx_dirty + 1) & (TX_RING_SIZE - 1);
    }

  up_irq_restore(flags);
}

/****************************************************************************
 * Private Functions — Interrupt Service Routine
 *
 * Reads DMA_STATUS to determine interrupt cause and dispatches accordingly.
 * All DMA status bits are write-1-to-clear.
 *
 * Reference: Manual §7, §10.6
 ****************************************************************************/

static int sg2000_eth_isr(int irq, void *context, void *arg)
{
  struct sg2000_eth_priv_s *priv = (struct sg2000_eth_priv_s *)arg;
  uintptr_t gmac_base = priv->gmac_base;
  uint32_t status;
  uint32_t mac_status;

  /* Read and clear MAC interrupts (if any) */

  mac_status = getreg32(gmac_base + GMAC_INT_STATUS);
  if (mac_status)
    {
      putreg32(mac_status, gmac_base + GMAC_INT_STATUS);
    }

  /* Read DMA status masked by enabled interrupts */

  status = getreg32(gmac_base + DMA_STATUS) &
           getreg32(gmac_base + DMA_INTR_ENA);

  if (!status)
    {
      return OK;
    }

  /* Write-1-to-clear the status bits */

  putreg32(status & DMA_STATUS_W1C_MASK, gmac_base + DMA_STATUS);

  /* Handle normal interrupts */

  if (status & DMA_STATUS_NIS)
    {
      if (status & DMA_STATUS_RI)
        {
          netdev_lower_rxready(&priv->dev);
        }

      if (status & DMA_STATUS_TI)
        {
          tx_cleanup(priv);
        }
    }

  /* Handle abnormal interrupts */

  if (status & DMA_STATUS_AIS)
    {
      if (status & DMA_STATUS_FBI)
        {
          etherr("Fatal Bus Error! bus_mode=0x%08" PRIX32
                 " dma_status=0x%08" PRIX32 "\n",
                 getreg32(gmac_base + DMA_BUS_MODE), status);
        }

      if (status & DMA_STATUS_TU)
        {
          ethwarn("TX Buffer Unavailable\n");
        }

      if (status & DMA_STATUS_RU)
        {
          ethwarn("RX Buffer Unavailable\n");
        }

      if (status & DMA_STATUS_TPS)
        {
          ethwarn("TX Process Stopped\n");
        }

      if (status & DMA_STATUS_RPS)
        {
          ethwarn("RX Process Stopped\n");
        }

      if (status & DMA_STATUS_OVF)
        {
          ethwarn("RX Overflow\n");
        }

      if (status & DMA_STATUS_UNF)
        {
          ethwarn("TX Underflow\n");
        }
    }

  return OK;
}

/****************************************************************************
 * Name: netdriver_ifup
 *
 * Description:
 *   OpenVela netdev_ops_s ifup callback.  Performs the full hardware
 *   initialization sequence and brings the interface online.
 *
 * Reference: Manual §4.3, §10.3, §10.11
 ****************************************************************************/

static int netdriver_ifup(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct sg2000_eth_priv_s *priv =
      (FAR struct sg2000_eth_priv_s *)dev;
  int ret;

  ethinfo("ifup begin\n");

  /* 1. Enable clocks and deassert resets */

  gmac_clock_enable();

  /* 2. Set GMAC_CORE_INIT (includes PS=MII mode) before PHY operations */

  putreg32(GMAC_CORE_INIT, priv->gmac_base + GMAC_CONTROL);

  /* 3. Initialize EPHY via APB */

  ephy_hardware_reset(priv->phy_base, priv->phy_top);

  /* 4. Verify MDIO communication with PHY */

  ret = mdio_verify_phy(priv->gmac_base);
  if (ret < 0)
    {
      etherr("PHY not responding: %d\n", ret);
      return ret;
    }

  ethinfo("MDIO operational, mdio_addr=0x%08" PRIX32
          " mdio_data=0x%08" PRIX32 "\n",
          getreg32(priv->gmac_base + GMAC_MII_ADDR),
          getreg32(priv->gmac_base + GMAC_MII_DATA));

  /* 5. Run EPHY full analog tuning (requires operational MDIO) */

  ephy_full_tuning(priv->phy_base, priv->phy_top);

  /* 6. Allocate DMA resources (once) */

  ret = alloc_dma(priv);
  if (ret < 0)
    {
      etherr("alloc_dma failed: %d\n", ret);
      return ret;
    }

  /* 7. Initialize descriptor rings */

  init_desc_rings(priv);

  /* 8. Set MAC address */

  set_mac_addr(priv->gmac_base, priv->mac_addr);

  /* 9. Configure PHY (auto-negotiation) BEFORE starting DMA.
   *    Starting DMA while the PHY is in reset causes the TX path to
   *    stall (frames held in FIFO with no RMII link) and the RX path
   *    to pick up garbage from an unstable RMII interface.
   */

  ret = phy_config_init(priv);
  if (ret < 0)
    {
      etherr("PHY config failed: %d\n", ret);
      return ret;
    }

  /* 10. Hardware setup: DMA reset, MAC core, DMA mode, start DMA.
   *    Performed AFTER PHY is ready — clean RMII link and stable speed.
   *    Note: gmac_set_speed_duplex() is called INSIDE hw_setup() after
   *    dma_soft_reset() to avoid the reset clearing FES/DM bits.
   */

  ret = hw_setup(priv);
  if (ret < 0)
    {
      etherr("hw_setup failed: %d\n", ret);
      return ret;
    }

  /* 11. Ring RX doorbell and enable interrupts (last, after all HW init) */

  rx_doorbell(priv->gmac_base);
  up_enable_irq(priv->irq);

  /* 12. Signal carrier on to the network stack (if link detected) */

  if (priv->link_up)
    {
      netdev_lower_carrier_on(dev);
    }

  ethinfo("ifup done: %d Mbps %s duplex, link=%s\n",
          priv->speed, priv->duplex ? "Full" : "Half",
          priv->link_up ? "up" : "down");

  return OK;
}

/****************************************************************************
 * Name: netdriver_ifdown
 *
 * Description:
 *   OpenVela netdev_ops_s ifdown callback.  Stops DMA, disables MAC,
 *   and disables interrupts.
 *
 * Reference: Manual §4.3
 ****************************************************************************/

static int netdriver_ifdown(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct sg2000_eth_priv_s *priv =
      (FAR struct sg2000_eth_priv_s *)dev;
  uintptr_t gmac_base = priv->gmac_base;
  uint32_t val;

  up_disable_irq(priv->irq);

  /* Stop TX and RX DMA */

  val = getreg32(gmac_base + DMA_CONTROL);
  putreg32(val & ~(DMA_CONTROL_ST | DMA_CONTROL_SR), gmac_base + DMA_CONTROL);

  /* Disable MAC TX/RX */

  val = getreg32(gmac_base + GMAC_CONTROL);
  putreg32(val & ~(GMAC_CONTROL_TE | GMAC_CONTROL_RE),
           gmac_base + GMAC_CONTROL);

  /* Disable DMA interrupts */

  putreg32(0, gmac_base + DMA_INTR_ENA);

  /* Clear pending interrupts */

  putreg32(DMA_STATUS_W1C_MASK, gmac_base + DMA_STATUS);

  netdev_lower_carrier_off(dev);

  priv->link_up = 0;

  ethinfo("ifdown: interface stopped\n");

  return OK;
}

/****************************************************************************
 * Name: netdriver_transmit
 *
 * Description:
 *   OpenVela netdev_ops_s transmit callback.  Sends a packet via DMA.
 *   The driver takes ownership of the netpkt and MUST free it after
 *   TX completion (in tx_cleanup) or on error.
 *
 * Reference: Manual §5.1, §10.4
 ****************************************************************************/

static int netdriver_transmit(FAR struct netdev_lowerhalf_s *dev,
                               FAR netpkt_t *pkt)
{
  FAR struct sg2000_eth_priv_s *priv =
      (FAR struct sg2000_eth_priv_s *)dev;
  unsigned int len = netpkt_getdatalen(dev, pkt);
  const uint8_t *data;
  irqstate_t flags;
  int ret;

  if (len > TX_BUF_SIZE)
    {
      etherr("TX packet too large: %u\n", len);
      return -E2BIG;
    }

  /* For non-fragmented packets, use the data pointer directly.
   * hw_transmit() will memcpy it into the DMA TX buffer.
   * For fragmented packets, copy directly into the DMA TX buffer
   * to avoid a stack-allocated temporary buffer.
   */

  if (netpkt_is_fragmented(pkt))
    {
      /* Copy fragmented packet directly into the next DMA TX buffer.
       * hw_transmit() will set up the descriptor referencing this buffer.
       */

      unsigned int idx = priv->tx_cur;
      netpkt_copyout(dev, priv->tx_buf[idx], pkt, len, 0);
      data = priv->tx_buf[idx];
    }
  else
    {
      data = netpkt_getdata(dev, pkt);
      if (!data)
        {
          etherr("netpkt_getdata returned NULL\n");
          return -EINVAL;
        }
    }

  /* Transmit via DMA.  Track the netpkt BEFORE calling hw_transmit
   * (which advances tx_cur) so the ISR can't observe a half-committed
   * slot.  The whole section runs with IRQs off to close the race
   * between pkt store, cur advance, and ISR tx_cleanup.
   *
   * On failure, do NOT free the netpkt — the upper half recycles it.
   */

  flags = up_irq_save();
  priv->tx_pkt[priv->tx_cur] = pkt;
  ret = hw_transmit(priv, data, len);
  if (ret < 0)
    {
      priv->tx_pkt[priv->tx_cur] = NULL;  /* Undo — slot not consumed */
      up_irq_restore(flags);
      etherr("hw_transmit failed: %d\n", ret);
      return ret;
    }

  up_irq_restore(flags);

  return OK;
}

/****************************************************************************
 * Name: netdriver_receive
 *
 * Description:
 *   OpenVela netdev_ops_s receive callback.  Called by upper half when
 *   netdev_lower_rxready() was signaled.  Returns one received packet,
 *   or NULL if no more packets are available.
 *
 * Reference: Manual §6, §10.5
 ****************************************************************************/

static FAR netpkt_t *netdriver_receive(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct sg2000_eth_priv_s *priv =
      (FAR struct sg2000_eth_priv_s *)dev;
  FAR netpkt_t *pkt = NULL;
  struct sg2000_dma_desc_s *desc;
  uintptr_t gmac_base = priv->gmac_base;
  uint32_t status;
  int frame_len;
  int ret;

  desc = &priv->rx_desc[priv->rx_cur];

  /* Invalidate descriptor cache line to read DMA-written status */

  dcache_invalidate_range((uintptr_t)desc,
                          (uintptr_t)desc +
                          sizeof(struct sg2000_dma_desc_s));

  /* Check if DMA still owns the descriptor (no packet ready) */

  if (desc->des0 & RDES0_OWN)
    {
      return NULL;
    }

  status = desc->des0;

  /* Check for receive errors */

  if (status & RDES0_ERROR_SUMMARY)
    {
      etherr("RX error status=0x%08" PRIX32 "\n", status);
      goto refill;
    }

  /* Verify we got a complete frame (first AND last descriptor) */

  if (!(status & RDES0_FIRST_DESCRIPTOR) ||
      !(status & RDES0_LAST_DESCRIPTOR))
    {
      ethwarn("RX incomplete frame, status=0x%08" PRIX32 "\n", status);
      goto refill;
    }

  /* Get frame length (includes 4-byte FCS which we strip) */

  frame_len = (int)((status & RDES0_FRAME_LEN_MASK) >> RDES0_FRAME_LEN_SHIFT)
              - ETH_FCSSLEN;

  if (frame_len <= 0 || frame_len > CONFIG_NET_ETH_PKTSIZE)
    {
      ethwarn("RX invalid frame length: %d\n", frame_len);
      goto refill;
    }

  /* Invalidate RX data buffer to read DMA-written data */

  dcache_invalidate_range((uintptr_t)priv->rx_buf[priv->rx_cur],
                          (uintptr_t)priv->rx_buf[priv->rx_cur] +
                          RX_BUF_SIZE);

  /* Allocate a netpkt for the received data */

  pkt = netpkt_alloc(dev, NETPKT_RX);
  if (!pkt)
    {
      etherr("netpkt_alloc failed\n");
      goto refill;
    }

  /* Copy data from DMA buffer into netpkt */

  ret = netpkt_copyin(dev, pkt, priv->rx_buf[priv->rx_cur], frame_len, 0);
  if (ret < 0)
    {
      etherr("netpkt_copyin failed: %d\n", ret);
      netpkt_free(dev, pkt, NETPKT_RX);
      pkt = NULL;
      goto refill;
    }

refill:
  /* Return descriptor to DMA: set OWN=1, restore buffer address.
   * Preserve END_RING bit for the last descriptor in des1.
   */

  desc->des0 = RDES0_OWN;
  desc->des1 = (RX_BUF_SIZE & RDES1_BUFFER1_SIZE_MASK) |
               ((priv->rx_cur == (RX_RING_SIZE - 1)) ? RDES1_END_RING : 0);
  desc->des2 = priv->rx_buf_phys[priv->rx_cur];

  /* Flush RX buffer before handing it back to DMA */

  dcache_flush_range((uintptr_t)priv->rx_buf[priv->rx_cur],
                     (uintptr_t)priv->rx_buf[priv->rx_cur] + RX_BUF_SIZE);

  /* Write back descriptor so DMA sees OWN=1 */

  dcache_clean_range((uintptr_t)desc,
                     (uintptr_t)desc + sizeof(struct sg2000_dma_desc_s));

  priv->rx_cur = (priv->rx_cur + 1) & (RX_RING_SIZE - 1);
  rx_doorbell(gmac_base);

  return pkt;
}

/****************************************************************************
 * Private Data — Operations Structure
 ****************************************************************************/

static const struct netdev_ops_s g_ops =
{
  netdriver_ifup,      /* ifup */
  netdriver_ifdown,    /* ifdown */
  netdriver_transmit,  /* transmit */
  netdriver_receive    /* receive */
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sg2000_eth_initialize
 *
 * Description:
 *   Initialize the SG2000 Ethernet driver and register it with the OpenVela
 *   network stack as a netdev_lowerhalf_s device.
 *
 *   Called from board_late_initialize() during system bringup.
 *
 ****************************************************************************/

int sg2000_eth_initialize(void)
{
  FAR struct sg2000_eth_priv_s *priv = &g_eth_priv;
  FAR struct netdev_lowerhalf_s *dev = &priv->dev;
  int ret;

  if (g_eth_initialized)
    {
      return OK;
    }

  /* Initialize private data */

  memset(priv, 0, sizeof(*priv));
  priv->gmac_base = SG2000_GMAC_BASE;
  priv->phy_base  = SG2000_EPHY_BASE;
  priv->phy_top   = SG2000_EPHY_TOP;

  /* Set default MAC address (should be replaced with eFuse/OTP value) */

  priv->mac_addr[0] = 0x02;
  priv->mac_addr[1] = 0x00;
  priv->mac_addr[2] = 0x00;
  priv->mac_addr[3] = 0x00;
  priv->mac_addr[4] = 0x00;
  priv->mac_addr[5] = 0x01;

  /* Copy MAC address to the netdev structure */

  memcpy(dev->netdev.d_mac.ether.ether_addr_octet, priv->mac_addr,
         IFHWADDRLEN);

  /* Configure device quotas and operations.
   * Total quota (TX + RX) must not exceed CONFIG_IOB_NBUFFERS.
   * With CONFIG_IOB_NBUFFERS=64, we reserve 16 each (32 total).
   */

  dev->quota[NETPKT_TX] = 16;
  dev->quota[NETPKT_RX] = 16;
  dev->ops              = &g_ops;

  /* Get the GMAC interrupt number */

  priv->irq = SG2000_IRQ_GMAC;

  /* Register with the OpenVela network stack */

  ret = netdev_lower_register(dev, NET_LL_ETHERNET);
  if (ret < 0)
    {
      etherr("netdev_lower_register failed: %d\n", ret);
      return ret;
    }

  /* Initially, carrier is off */

  netdev_lower_carrier_off(dev);
  g_eth_initialized = true;

  ethinfo("SG2000 Ethernet driver registered as eth0\n");

  /* Attach interrupt handler (once).
   * The IRQ is NOT enabled here — it is enabled at the end of
   * netdriver_ifup() only after the hardware is fully configured.
   */

  ret = irq_attach(priv->irq, sg2000_eth_isr, priv);
  if (ret < 0)
    {
      etherr("irq_attach failed: %d\n", ret);
      return OK;  /* Driver is still registered, just no interrupts */
    }

  return OK;
}

/****************************************************************************
 * Name: riscv_netinitialize
 *
 * Description:
 *   Arch-level network initialization called from up_initialize().
 *   SG2000 Ethernet is initialized from board_late_initialize() instead.
 *
 ****************************************************************************/

void riscv_netinitialize(void)
{
  /* SG2000 Ethernet needs board-level bringup ordering.  It is initialized
   * from board_late_initialize() after the core OS initialization path.
   */
}
