/** @file
  Copyright (C) 2020, vit9696. All rights reserved.

  All rights reserved.

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#ifndef OC_SMC_LIB_H
#define OC_SMC_LIB_H

#include <Protocol/AppleSmcIo.h>

/**
  Install and initialise SMC I/O protocol.

  @param[in] Reinstall    Overwrite installed protocol.
  @param[in] AuthRestart  Support AuthRestart protocol via VSMC.

  @retval installed or located protocol or NULL.
**/
APPLE_SMC_IO_PROTOCOL *
OcSmcIoInstallProtocol (
  IN BOOLEAN  Reinstall,
  IN BOOLEAN  AuthRestart
  );

#endif // OC_SMC_LIB_H
