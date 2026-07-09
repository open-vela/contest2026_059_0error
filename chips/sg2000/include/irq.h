/****************************************************************************
 * vendor/sg2000/chips/sg2000/include/irq.h
 *
 * SPDX-License-Identifier: Apache-2.0
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

#ifndef __VENDOR_SG2000_CHIP_SG2000_INCLUDE_IRQ_H
#define __VENDOR_SG2000_CHIP_SG2000_INCLUDE_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* NR_IRQS: Total number of NuttX IRQs used by this chip port.
 * The SG2000 port follows the RISC-V convention:
 *   NuttX IRQ = RISCV_IRQ_EXT + PLIC source
 * Max PLIC source = 57, so NR_IRQS = RISCV_IRQ_EXT + 57
 */

#define NR_IRQS  (RISCV_IRQ_EXT + 57)

/* NVIC_IRQ_FIRST: The first external interrupt number */

#define NVIC_IRQ_FIRST  16

/* SG2000 Peripheral IRQ numbers *******************************************/

#define SG2000_IRQ_GMAC           (RISCV_IRQ_EXT + 31)

#endif /* __VENDOR_SG2000_CHIP_SG2000_INCLUDE_IRQ_H */
