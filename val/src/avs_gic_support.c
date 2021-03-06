/** @file
 * Copyright (c) 2016-2021, Arm Limited or its affiliates. All rights reserved.
 * SPDX-License-Identifier : Apache-2.0

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **/

#include "include/sbsa_avs_val.h"
#include "include/sbsa_avs_gic.h"
#include "include/sbsa_avs_gic_support.h"
#include "include/sbsa_avs_common.h"
#include "include/sbsa_avs_pcie.h"
#include "include/sbsa_avs_iovirt.h"
#include "sys_arch_src/gic_its_v3/sbsa_gic_its.h"

extern GIC_INFO_TABLE  *g_gic_info_table;
GIC_INFO_ENTRY  *g_gic_entry = NULL;
GIC_ITS_INFO    *g_gic_its_info;

#ifndef TARGET_LINUX
/**
  @brief   This API provides a 'C' interface to call GIC System register reads
           1. Caller       -  Test Suite
           2. Prerequisite -  None
  @param   reg_id  - the system register index for which data is returned
  @return  the value read from the system register.
**/
uint64_t
val_gic_reg_read(uint32_t reg_id)
{

  switch(reg_id) {
      case ICH_HCR_EL2:
          return GicReadIchHcr();
      case ICH_MISR_EL2:
          return GicReadIchMisr();
      default:
           val_report_status(val_pe_get_index_mpid(val_pe_get_mpid()), RESULT_FAIL(g_sbsa_level, 0, 0x78));
  }

  return 0x0;

}

/**
  @brief   This API provides a 'C' interface to call GIC System register writes
           1. Caller       -  Test Suite
           2. Prerequisite -  None
  @param   reg_id  - the system register index for which data is written
  @param   write_data - the 64-bit data to write to the system register
  @return  None
**/
void
val_gic_reg_write(uint32_t reg_id, uint64_t write_data)
{

  switch(reg_id) {
      case ICH_HCR_EL2:
          GicWriteIchHcr(write_data);
          break;
      case ICC_IGRPEN1_EL1:
          GicWriteIccIgrpen1(write_data);
          break;
      case ICC_BPR1_EL1:
          GicWriteIccBpr1(write_data);
          break;
      case ICC_PMR_EL1:
          GicWriteIccPmr(write_data);
          break;
      default:
           val_report_status(val_pe_get_index_mpid(val_pe_get_mpid()), RESULT_FAIL(g_sbsa_level, 0, 0x78));
  }

}
#endif

uint32_t
val_gic_is_valid_lpi(uint32_t int_id)
{
  uint32_t max_lpi_id = 0;

  max_lpi_id = ArmGicItsGetMaxLpiID();

  if ((int_id < LPI_MIN_ID) || (int_id > max_lpi_id)) {
    /* Not Vaild LPI */
    return 0;
  }

  return 1; /* Valid LPI */
}

/**
  @brief   This function is installs the ISR pointed by the function pointer
           the input Interrupt ID.
           1. Caller       -  Test Suite
           2. Prerequisite -  val_gic_create_info_table
  @param   int_id Interrupt ID to install the ISR
  @param   isr    Function pointer of the ISR
  @return  status
**/
uint32_t
val_gic_install_isr(uint32_t int_id, void (*isr)(void))
{
  uint32_t      ret_val;
#ifndef TARGET_LINUX
  uint32_t      reg_offset = int_id / 32;
  uint32_t      reg_shift  = int_id % 32;

  if (((int_id > val_get_max_intid()) && (!val_gic_is_valid_lpi(int_id))) || (int_id == 0)) {
      val_print(AVS_PRINT_ERR, "\n       Invalid Interrupt ID number 0x%x ", int_id);
      return AVS_STATUS_ERR;
  }
#endif

  ret_val = pal_gic_install_isr(int_id, isr);

#ifndef TARGET_LINUX
  if (int_id > 31 && int_id < 1024) {
      /**** UEFI GIC code is not enabling interrupt in the Distributor ***/
      /**** So, do this here as a fail-safe. Remove if PAL guarantees this ***/
      val_mmio_write(val_get_gicd_base() + GICD_ISENABLER + (4 * reg_offset), 1 << reg_shift);
  }
#endif

  return ret_val;
}

/**
 * @brief   This function registers the specified interrupt
 * @param   irq_num         hardware irq number
 * @param   mapped_irq_num  Mapped irq number
 * @param   isr             Interrupt service routine
 * @return  status
 */
uint32_t val_gic_request_irq(uint32_t irq_num, uint32_t mapped_irq_num, void *isr)
{
  return pal_gic_request_irq(irq_num, mapped_irq_num, isr);
}

/**
 * @brief   This function free the registered interrupt line
 * @param   irq_num         hardware irq number
 * @param   mapped_irq_num  Mapped irq number
 * @return  none
 */
void val_gic_free_irq(uint32_t irq_num, uint32_t mapped_irq_num)
{
    pal_gic_free_irq(irq_num, mapped_irq_num);
}

/**
  @brief   This function writes to end of interrupt register for relevant
           interrupt group.
           1. Caller       -  Test Suite
           2. Prerequisite -  val_gic_create_info_table
  @param   int_id Interrupt ID for which to disable the interrupt
  @return  status
**/
uint32_t val_gic_end_of_interrupt(uint32_t int_id)
{
  pal_gic_end_of_interrupt(int_id);

  return 0;
}

uint32_t val_gic_its_configure()
{
  uint32_t Status;

  g_gic_entry = g_gic_info_table->gic_info;

  if( g_gic_entry == NULL)
    goto its_fail;

  /* Allocate memory to store ITS info */
  g_gic_its_info = (GIC_ITS_INFO *) val_memory_alloc(1024);
  if (!g_gic_its_info) {
      val_print(AVS_PRINT_ERR, "GIC : ITS table memory allocation failed\n", 0);
      goto its_fail;
  }

  g_gic_its_info->GicNumIts = 0;
  g_gic_its_info->GicRdBase = 0;
  g_gic_its_info->GicDBase  = 0;

  while (g_gic_entry->type != 0xFF)
  {
   if (g_gic_entry->type == ENTRY_TYPE_GICD)
    {
        g_gic_its_info->GicDBase = g_gic_entry->base;
    }
    else if ((g_gic_entry->type == ENTRY_TYPE_GICR_GICRD) || (g_gic_entry->type == ENTRY_TYPE_GICC_GICRD))
    {
        /* Calculate Current PE Redistributor Base Address */
        if (g_gic_its_info->GicRdBase == 0)
        {
            if (g_gic_entry->type == ENTRY_TYPE_GICR_GICRD)
                g_gic_its_info->GicRdBase = GetCurrentCpuRDBase(g_gic_entry->base, g_gic_entry->length);
            else
                g_gic_its_info->GicRdBase = GetCurrentCpuRDBase(g_gic_entry->base, 0);
        }
    }
    else if (g_gic_entry->type == ENTRY_TYPE_GICITS)
    {
        g_gic_its_info->GicIts[g_gic_its_info->GicNumIts].Base = g_gic_entry->base;
        g_gic_its_info->GicIts[g_gic_its_info->GicNumIts++].ID = g_gic_entry->its_id;
    }
    g_gic_entry++;

  }

  /* Return if no ITS */
  if (g_gic_its_info->GicNumIts == 0)
  {

    goto its_fail;
  }

  /* Base Address Check. */
  if ((g_gic_its_info->GicRdBase == 0) || (g_gic_its_info->GicDBase == 0))
  {
    val_print(AVS_PRINT_ERR, "Could not get GICD/GICRD Base.\n", 0);
    goto its_fail;
  }

  if (ArmGICDSupportsLPIs(g_gic_its_info->GicDBase) && ArmGICRSupportsLPIs(g_gic_its_info->GicRdBase))
  {

    Status = ArmGicItsConfiguration();

    if ((Status))
    {
      val_print(AVS_PRINT_ERR, "Could Not Configure ITS.\n", 0);
      goto its_fail;
    }
  }
  else
  {
    val_print(AVS_PRINT_ERR, "LPIs not supported in the system.\n", 0);
    goto its_fail;
  }

  return 0;

its_fail:

  val_print(AVS_PRINT_ERR, "GIC ITS Initialization Failed.\n", 0);
  val_print(AVS_PRINT_ERR, "LPI Interrupt related test may not pass.\n", 0);

  return AVS_STATUS_ERR;
}

uint32_t
getItsIndex (
  uint32_t   ItsID
  )
{
  uint32_t  index;

  for (index=0; index<g_gic_its_info->GicNumIts; index++)
  {
    if (ItsID == g_gic_its_info->GicIts[index].ID)
      return index;
  }
  return AVS_INVALID_INDEX;
}

void clear_msi_x_table(uint32_t bdf, uint32_t msi_index)
{

  uint32_t msi_cap_offset, msi_table_bar_index;
  uint32_t table_offset_reg, table_address;
  uint32_t read_value;

  /* Get MSI Capability Offset */
  val_pcie_find_capability(bdf, PCIE_CAP, CID_MSIX, &msi_cap_offset);

  /* Disable MSI-X in MSI-X Capability */
  val_pcie_read_cfg(bdf, msi_cap_offset, &read_value);
  val_pcie_write_cfg(bdf, msi_cap_offset, (read_value & ((1ul << MSI_X_ENABLE_SHIFT) - 1)));

  /* Read MSI-X Table Address from the BAR Register */
  val_pcie_read_cfg(bdf, msi_cap_offset + MSI_X_TOR_OFFSET, &table_offset_reg);
  msi_table_bar_index = table_offset_reg & MSI_X_TABLE_BIR_MASK;
  val_pcie_read_cfg(bdf, TYPE01_BAR + msi_table_bar_index*4, &table_address);

  /* Clear MSI Table */
  val_mmio_write(table_address + msi_index*MSI_X_ENTRY_SIZE + MSI_X_MSG_TBL_ADDR_OFFSET, 0);
  val_mmio_write(table_address + msi_index*MSI_X_ENTRY_SIZE + MSI_X_MSG_TBL_DATA_OFFSET, 0);
  val_mmio_write(table_address + msi_index*MSI_X_ENTRY_SIZE + MSI_X_MSG_TBL_MVC_OFFSET, 0x1);
}

void fill_msi_x_table(uint32_t bdf, uint32_t msi_index, uint32_t msi_addr, uint32_t msi_data)
{

  uint32_t msi_cap_offset, msi_table_bar_index;
  uint32_t table_offset_reg, table_address, command_data;
  uint32_t read_value;

  /* Enable Memory Space, Bus Master */
  val_pcie_read_cfg(bdf, TYPE01_CR, &command_data);
  val_pcie_write_cfg(bdf, TYPE01_CR, (command_data | (1 << CR_MSE_SHIFT) | (1 << CR_BME_SHIFT)));

  /* Get MSI Capability Offset */
  val_pcie_find_capability(bdf, PCIE_CAP, CID_MSIX, &msi_cap_offset);

  /* Enable MSI-X in MSI-X Capability */
  val_pcie_read_cfg(bdf, msi_cap_offset, &read_value);
  val_pcie_write_cfg(bdf, msi_cap_offset, read_value | (1 << MSI_X_ENABLE_SHIFT));

  /* Read MSI-X Table Address from the BAR Register */
  val_pcie_read_cfg(bdf, msi_cap_offset + MSI_X_TOR_OFFSET, &table_offset_reg);
  msi_table_bar_index = table_offset_reg & MSI_X_TABLE_BIR_MASK;
  val_pcie_read_cfg(bdf, TYPE01_BAR + msi_table_bar_index*4, &table_address);

  /* Fill MSI Table with msi_addr, msi_data */
  val_mmio_write(table_address + msi_index*MSI_X_ENTRY_SIZE + MSI_X_MSG_TBL_ADDR_OFFSET, msi_addr);
  val_mmio_write(table_address + msi_index*MSI_X_ENTRY_SIZE + MSI_X_MSG_TBL_DATA_OFFSET, msi_data);
  val_mmio_write(table_address + msi_index*MSI_X_ENTRY_SIZE + MSI_X_MSG_TBL_MVC_OFFSET, 0x0);
}

/**
  @brief   This function clear the MSI related mappings.

  @param   bdf          B:D:F for the device
  @param   IntID        Interrupt ID
  @param   msi_index    msi index in the table

  @return  status
**/
void val_gic_free_msi(uint32_t bdf, uint32_t IntID, uint32_t msi_index)
{
  uint32_t status;
  uint32_t device_id, stream_id, its_id;
  uint32_t req_id;
  uint32_t bus = PCIE_EXTRACT_BDF_BUS(bdf);
  uint32_t dev = PCIE_EXTRACT_BDF_DEV(bdf);
  uint32_t func = PCIE_EXTRACT_BDF_FUNC(bdf);
  uint32_t  ItsIndex;

  req_id = GET_DEVICE_ID(bus, dev, func);

  status = val_iovirt_get_device_info(req_id, PCIE_EXTRACT_BDF_SEG(bdf), &device_id, &stream_id, &its_id);
  if (status) { /* Use Requester-Id if val_iovirt_get_device_info fails.*/
    device_id = req_id;
  }

  ItsIndex = getItsIndex(its_id);
  if (ItsIndex > g_gic_its_info->GicNumIts)
  {
    val_print(AVS_PRINT_ERR, "\n       Could not find ITS block in MADT", 0);
  }
  if ((g_gic_its_info->GicRdBase == 0) || (g_gic_its_info->GicDBase == 0))
  {
    val_print(AVS_PRINT_ERR, "GICD/GICRD Base Invalid value.\n", 0);
  }
  ArmGicItsClearLpiMappings(ItsIndex, device_id, IntID);
  clear_msi_x_table(bdf, msi_index);
}

/**
  @brief   This function creates the MSI mappings, and programs the MSI Table.

  @param   bdf          B:D:F for the device
  @param   IntID        Interrupt ID
  @param   msi_index    msi index in the table

  @return  status
**/
uint32_t val_gic_request_msi(uint32_t bdf, uint32_t IntID, uint32_t msi_index)
{
  uint32_t status;
  uint32_t msi_addr, msi_data;
  uint32_t device_id, stream_id, its_id;
  uint32_t req_id;
  uint32_t bus = PCIE_EXTRACT_BDF_BUS(bdf);
  uint32_t dev = PCIE_EXTRACT_BDF_DEV(bdf);
  uint32_t func = PCIE_EXTRACT_BDF_FUNC(bdf);
  uint32_t  ItsIndex;

  req_id = GET_DEVICE_ID(bus, dev, func);

  status = val_iovirt_get_device_info(req_id, PCIE_EXTRACT_BDF_SEG(bdf), &device_id, &stream_id, &its_id);
  if (status) { /* Use Requester-Id if val_iovirt_get_device_info fails.*/
    device_id = req_id;
  }

   if ((g_gic_its_info == NULL) || (g_gic_its_info->GicNumIts == 0))
    return AVS_STATUS_ERR;

  ItsIndex = getItsIndex(its_id);

  if (ItsIndex > g_gic_its_info->GicNumIts) {
    val_print(AVS_PRINT_ERR, "\n       Could not find ITS block in MADT", 0);
    return AVS_STATUS_ERR;
  }

  if ((g_gic_its_info->GicRdBase == 0) || (g_gic_its_info->GicDBase == 0))
  {
    val_print(AVS_PRINT_DEBUG, "GICD/GICRD Base Invalid value.\n", 0);
    return AVS_STATUS_ERR;
  }

   ArmGicItsCreateLpiMap(ItsIndex, device_id, IntID, LPI_PRIORITY1);

  msi_addr = ArmGicItsGetGITSTranslatorAddress(ItsIndex);
  msi_data = IntID;


  fill_msi_x_table(bdf, msi_index, msi_addr, msi_data);

  return status;
}
