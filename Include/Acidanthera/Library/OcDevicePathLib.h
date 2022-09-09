/** @file
  Copyright (C) 2016, The HermitCrabs Lab. All rights reserved.

  All rights reserved.

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#ifndef OC_DEVICE_PATH_LIB_H
#define OC_DEVICE_PATH_LIB_H

/**
  Apple MacPro5,1 includes NVMe driver, however, it contains a typo in MSG_SASEX_DP.
  Instead of 0x16 aka 22 (SasEx) it uses 0x22 aka 34 (Unspecified).
  Here we replace it with the "right" value.
  Reference: https://forums.macrumors.com/posts/28169441.
**/
#define MSG_APPLE_NVME_NAMESPACE_DP  0x22

/**
  Append file name to device path.

  @param[in] DevicePath  The device path which to append the file path.
  @param[in] FileName    The file name to append to the device path.

  @retval EFI_SUCCESS            The defaults were initialized successfully.
  @retval EFI_INVALID_PARAMETER  The parameters passed were invalid.
  @retval EFI_OUT_OF_RESOURCES   The system ran out of memory.
**/
EFI_DEVICE_PATH_PROTOCOL *
AppendFileNameDevicePath (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevicePath,
  IN CHAR16                    *FileName
  );

/**
  Locate the terminating node inside the device path.

  @param[in] DevicePath  The device path used in the search.

  @return  Returned is the last Device Path Node.
**/
EFI_DEVICE_PATH_PROTOCOL *
FindDevicePathEndNode (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  );

/**
  Locate the node inside the device path specified by Type an SubType values.

  @param[in] DevicePath  The device path used in the search.
  @param[in] Type        The Type field of the device path node specified by Node.
  @param[in] SubType     The SubType field of the device path node specified by Node.

  @return  Returned is the first Device Path Node with the given type.
**/
EFI_DEVICE_PATH_PROTOCOL *
FindDevicePathNodeWithType (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevicePath,
  IN UINT8                     Type,
  IN UINT8                     SubType OPTIONAL
  );

/**
  Check whether device paths are equal.

  @param[in] DevicePath1  The first device path protocol to compare.
  @param[in] DevicePath2  The second device path protocol to compare.

  @retval TRUE         The device paths matched
  @retval FALSE        The device paths were different
**/
BOOLEAN
EFIAPI
IsDevicePathEqual (
  IN  EFI_DEVICE_PATH_PROTOCOL  *DevicePath1,
  IN  EFI_DEVICE_PATH_PROTOCOL  *DevicePath2
  );

/**
  Check whether File Device Paths are equal.

  @param[in] FilePath1  The first device path protocol to compare.
  @param[in] FilePath2  The second device path protocol to compare.

  @retval TRUE         The device paths matched
  @retval FALSE        The device paths were different
**/
BOOLEAN
FileDevicePathsEqual (
  IN FILEPATH_DEVICE_PATH  *FilePath1,
  IN FILEPATH_DEVICE_PATH  *FilePath2
  );

/**
  Check whether one device path exists in the other.

  @param[in] ParentPath  The parent device path protocol to check against.
  @param[in] ChildPath   The device path protocol of the child device to compare.

  @retval TRUE         The child device path contains the parent device path.
  @retval FALSE        The device paths were different
**/
BOOLEAN
EFIAPI
IsDevicePathChild (
  IN  EFI_DEVICE_PATH_PROTOCOL  *ParentPath,
  IN  EFI_DEVICE_PATH_PROTOCOL  *ChildPath
  );

/**
  Get absolute device path.

  @param[in] Handle        Device handle.
  @param[in] RelativePath  Relative device path to handle, optional.

  @retval  New device path or NULL.
**/
EFI_DEVICE_PATH_PROTOCOL *
AbsoluteDevicePath (
  IN EFI_HANDLE                Handle,
  IN EFI_DEVICE_PATH_PROTOCOL  *RelativePath OPTIONAL
  );

/**
  Get trailed (slash-appended) device path for booter paths.
  This way \\smth.efi gets NULL and \\smth gives \\smth\\.

  @param[in] DevicePath    Device path.

  @retval  New device path or NULL.
**/
EFI_DEVICE_PATH_PROTOCOL *
TrailedBooterDevicePath (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  );

/**
  Returns the size of PathName.

  @param[in] FilePath  The file Device Path node to inspect.

  @retval  Name size in bytes.
**/
UINTN
OcFileDevicePathNameSize (
  IN CONST FILEPATH_DEVICE_PATH  *FilePath
  );

/**
  Returns the length of PathName.

  @param[in] FilePath  The file Device Path node to inspect.

  @retval  Name length in characters (without trailing zero).
**/
UINTN
OcFileDevicePathNameLen (
  IN CONST FILEPATH_DEVICE_PATH  *FilePath
  );

/**
  Retrieve the length of the full file path described by DevicePath.

  @param[in] DevicePath  The Device Path to inspect.

  @returns   The length of the full file path.
  @retval 0  DevicePath does not start with a File Path node or contains
             non-terminating nodes that are not File Path nodes.

**/
UINTN
OcFileDevicePathFullNameLen (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  );

/**
  Retrieve the size of the full file path described by DevicePath.

  @param[in] DevicePath  The Device Path to inspect.

  @returns   The size of the full file path.
  @retval 0  DevicePath does not start with a File Path node or contains
             non-terminating nodes that are not File Path nodes.

**/
UINTN
OcFileDevicePathFullNameSize (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  );

/**
  Retrieve the full file path described by FilePath.
  The caller is expected to call OcFileDevicePathFullNameSize() or ensure its
  guarantees are met.

  @param[out] PathName      On output, the full file path of FilePath.
  @param[in]  FilePath      The File Device Path to inspect.
  @param[in]  PathNameSize  The size, in bytes, of PathnName.  Must equal the
                            actual fill file path size.

**/
VOID
OcFileDevicePathFullName (
  OUT CHAR16                      *PathName,
  IN  CONST FILEPATH_DEVICE_PATH  *FilePath,
  IN  UINTN                       PathNameSize
  );

/**
  Retrieves full file path from device path allocating it on pool.

  @param[in]  DevicePath      Device path to extract file path from.
  @param[out] FileDevicePath  Pointer to file path part of the device path, optional.

  @returns Path allocated from pool.
  @retval NULL when missing.
**/
CHAR16 *
OcCopyDevicePathFullName (
  IN   EFI_DEVICE_PATH_PROTOCOL  *DevicePath,
  OUT  EFI_DEVICE_PATH_PROTOCOL  **FileDevicePath  OPTIONAL
  );

/**
  Duplicate device path with DevicePathInstance appended if it is not present.

  @param[in] DevicePath          Device Path to append new instance to, optional.
  @param[in] DevicePathInstance  Device Path instance to append.

  @retval  New Device Path or NULL.
**/
EFI_DEVICE_PATH_PROTOCOL *
OcAppendDevicePathInstanceDedupe (
  IN EFI_DEVICE_PATH_PROTOCOL        *DevicePath OPTIONAL,
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePathInstance
  );

/**
  Calculate number of device path instances.

  @param[in]  DevicePath  Device Path to calculate instances in.

  @retval  Number of instances in device path.
**/
UINTN
OcGetNumDevicePathInstances (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  );

///
/// Used to store information on how to restore a node patched by
/// OcFixAppleBootDevicePathNode().
///
typedef struct {
  //
  // Stores the old device path prior to expansion.
  //
  EFI_DEVICE_PATH_PROTOCOL    *OldPath;
  //
  // Valid iff OldPath == NULL.
  //
  union {
    struct {
      UINT16    PortMultiplierPortNumber;
    } Sata;

    struct {
      UINT8    SubType;
    } SasExNvme;

    struct {
      UINT32    HID;
      UINT32    UID;
    } Acpi;

    struct {
      UINT32    HID;
      UINT32    CID;
    } ExtendedAcpi;
  } Types;
} APPLE_BOOT_DP_PATCH_CONTEXT;

/**
  Fix Apple Boot Device Path node to be compatible with conventional UEFI
  implementations.

  @param[in,out] DevicePath      A pointer to the device path to fix a node of.
                                 It must be a pool memory buffer.
                                 On success, may be updated with a reallocated
                                 pool memory buffer.
  @param[in,out] DevicePathNode  A pointer to the device path node to fix. It
                                 must be a node of *DevicePath.
                                 On success, may be updated with the
                                 corresponding node of *DevicePath.
  @param[out]    RestoreContext  A pointer to a context that can be used to
                                 restore DevicePathNode's original content in
                                 the case of failure.
                                 On success, data may need to be freed.

  @retval -1  DevicePathNode could not be fixed.
  @retval 0   DevicePathNode was not modified and may be valid.
  @retval 1   DevicePathNode was fixed and may be valid.

**/
INTN
OcFixAppleBootDevicePathNode (
  IN OUT EFI_DEVICE_PATH_PROTOCOL     **DevicePath,
  IN OUT EFI_DEVICE_PATH_PROTOCOL     **DevicePathNode,
  OUT    APPLE_BOOT_DP_PATCH_CONTEXT  *RestoreContext OPTIONAL
  );

/**
  Restore the original content of DevicePathNode before calling
  OcFixAppleBootDevicePathNode() with RestoreContext.
  Modifying a device path and then restoring a node may lead to lost updates.

  @param[in,out] DevicePath      A pointer to the device path to restore a node
                                 of.
                                 May be updated with the previous pool memory
                                 buffer.
  @param[in,out] DevicePathNode  A pointer to the device path node to restore.
                                 It must be a node of *DevicePath.
                                 May be updated with the corresponding
                                 node of *DevicePath.
  @param[out]    RestoreContext  A pointer to a context that was used to call
                                 OcFixAppleBootDevicePathNode(). All resources
                                 are freed.

**/
VOID
OcFixAppleBootDevicePathNodeRestore (
  IN OUT EFI_DEVICE_PATH_PROTOCOL           **DevicePath,
  IN OUT EFI_DEVICE_PATH_PROTOCOL           **DevicePathNode,
  IN     CONST APPLE_BOOT_DP_PATCH_CONTEXT  *RestoreContext
  );

/*
  Frees any resources allocated by OcFixAppleBootDevicePathNode().

  @param[in]     DevicePath      A pointer to the device path associated with
                                 RestoreContext.
  @param[in,out] RestoreContext  A pointer to a context that was used to call
                                 OcFixAppleBootDevicePathNode(). All resources
                                 are freed.
*/
VOID
OcFixAppleBootDevicePathNodeRestoreFree (
  IN     CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePathNode,
  IN OUT APPLE_BOOT_DP_PATCH_CONTEXT     *RestoreContext
  );

/**
  Fix Apple Boot Device Path to be compatible with conventional UEFI
  implementations. In case -1 is returned and the first node could not be
  located or fixed, DevicePath's original state will be preserved. In all other
  cases, it may be destroyed.

  @param[in,out] DevicePath           A pointer to the device path to fix. It
                                      must be a pool memory buffer.
                                      On success, may be updated with a
                                      reallocated pool memory buffer.
  @param[in,out] RemainingDevicePath  A pointer the remaining part of the device
                                      path is returned into.

  Note, RemainingDevicePath will always point into DevicePath after this function
  completes. RemainingDevicePath being equal to DevicePath means that we failed to
  advance, and this device path is likely a short form device path or something
  completely not valid.

  @retval -1  *DevicePath could not be fixed.
  @retval 0   *DevicePath was not modified and may be valid.
  @retval 1   *DevicePath was fixed and may be valid.

**/
INTN
OcFixAppleBootDevicePath (
  IN OUT EFI_DEVICE_PATH_PROTOCOL  **DevicePath,
  OUT    EFI_DEVICE_PATH_PROTOCOL  **RemainingDevicePath
  );

/**
  Get the next possible full path pointing to the load option.
  The routine doesn't guarantee the returned full path points to an existing
  file, and it also doesn't guarantee the existing file is a valid load option.

  @param FilePath  The device path pointing to a load option.
                   It could be a short-form device path.
  @param FullPath  The full path returned by the routine in last call.
                   Set to NULL in first call.

  @return The next possible full path pointing to the load option.
          Caller is responsible to free the memory.
**/
EFI_DEVICE_PATH_PROTOCOL *
OcGetNextLoadOptionDevicePath (
  IN EFI_DEVICE_PATH_PROTOCOL  *FilePath,
  IN EFI_DEVICE_PATH_PROTOCOL  *FullPath
  );

/*
  Checks DevicePath for whether it ends with file path Suffix.

  @param[in] DevicePath    The Device Path to check.
  @param[in] Suffix        The suffix to check for.
  @param[in] SuffixLen  Must be equal to StrLen(Suffix).

  @returns  Whether DevicePath ends with Suffix.
*/
BOOLEAN
OcDevicePathHasFilePathSuffix (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevicePath,
  IN CONST CHAR16              *Suffix,
  IN UINTN                     SuffixLen
  );

#endif // OC_DEVICE_PATH_LIB_H
