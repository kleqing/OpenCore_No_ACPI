/** @file
  Copyright (C) 2019, vit9696. All rights reserved.

  All rights reserved.

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#ifndef OC_INTERFACE_PROTOCOL_H
#define OC_INTERFACE_PROTOCOL_H

#include <Library/OcBootManagementLib.h>
#include <Library/OcStorageLib.h>

/**
  Current supported interface protocol revision.
  It is changed every time the contract changes.

  WARNING: This protocol is currently undergoing active design.
**/
#define OC_INTERFACE_REVISION  8

/**
  The GUID of the OC_INTERFACE_PROTOCOL.
**/
#define OC_INTERFACE_PROTOCOL_GUID      \
  { 0x53027CDF, 0x3A89, 0x4255,         \
    { 0xAE, 0x29, 0xD6, 0x66, 0x6E, 0xFE, 0x99, 0xEF } }

/**
  The forward declaration for the protocol for the OC_INTERFACE_PROTOCOL.
**/
typedef struct OC_INTERFACE_PROTOCOL_ OC_INTERFACE_PROTOCOL;

/**
  Update context member functions with custom interface overrides.

  @param[in]     This          This protocol.
  @param[in]     Storage       File system access storage.
  @param[in,out] Picker        User interface context to be updated.

  @retval EFI_SUCCESS on successful context update.
**/
typedef
EFI_STATUS
(EFIAPI *OC_POPULATE_CONTEXT)(
  IN     OC_INTERFACE_PROTOCOL  *This,
  IN     OC_STORAGE_CONTEXT     *Storage,
  IN OUT OC_PICKER_CONTEXT      *Picker
  );

/**
  The structure exposed by the OC_INTERFACE_PROTOCOL.
**/
struct OC_INTERFACE_PROTOCOL_ {
  UINT32                 Revision;         ///< The revision of the installed protocol.
  OC_POPULATE_CONTEXT    PopulateContext;  ///< A pointer to the PopulateContext function.
};

/**
  A global variable storing the GUID of the OC_INTERFACE_PROTOCOL.
**/
extern EFI_GUID  gOcInterfaceProtocolGuid;

#endif // OC_INTERFACE_PROTOCOL_H
