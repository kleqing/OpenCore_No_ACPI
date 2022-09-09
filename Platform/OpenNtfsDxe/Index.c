/** @file
  Copyright (c) 2022, Mikhail Krichanov. All rights reserved.
  SPDX-License-Identifier: BSD-3-Clause

  Functional and structural descriptions follow NTFS Documentation
  by Richard Russon and Yuval Fledel
**/

#include "NTFS.h"
#include "Helper.h"

STATIC UINT64  mBufferSize;
STATIC UINT8   mDaysPerMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31, 0 };
INT64          mIndexCounter;

STATIC
VOID
FreeNode (
  IN OUT NTFS_FILE   *Node    OPTIONAL,
  IN     FSHELP_CTX  *Context
  )
{
  ASSERT (Context != NULL);

  if ((Node != NULL) && (Node != Context->RootNode)) {
    FreeFile (Node);
    FreePool (Node);
  }
}

STATIC
VOID
PopElement (
  IN OUT FSHELP_CTX  *Context
  )
{
  STACK_ELEMENT  *Element;

  ASSERT (Context != NULL);

  Element = Context->CurrentNode;
  if (Element != NULL) {
    Context->CurrentNode = Element->Parent;
    FreeNode (Element->Node, Context);
    FreePool (Element);
  }
}

STATIC
VOID
FreeStack (
  IN OUT FSHELP_CTX  *Context
  )
{
  ASSERT (Context != NULL);

  while (Context->CurrentNode != NULL) {
    PopElement (Context);
  }
}

STATIC
VOID
GoUpALevel (
  IN OUT FSHELP_CTX  *Context
  )
{
  ASSERT (Context != NULL);

  if (Context->CurrentNode->Parent == NULL) {
    return;
  }

  PopElement (Context);
}

STATIC
EFI_STATUS
PushNode (
  IN OUT FSHELP_CTX       *Context,
  IN     NTFS_FILE        *Node,
  IN     FSHELP_FILETYPE  FileType
  )
{
  STACK_ELEMENT  *Next;

  ASSERT (Context != NULL);
  ASSERT (Node != NULL);

  Next = AllocateZeroPool (sizeof (*Next));
  if (Next == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Next->Node           = Node;
  Next->Type           = FileType & ~FSHELP_CASE_INSENSITIVE;
  Next->Parent         = Context->CurrentNode;
  Context->CurrentNode = Next;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
GoToRoot (
  IN OUT FSHELP_CTX  *Context
  )
{
  ASSERT (Context != NULL);

  FreeStack (Context);
  return PushNode (Context, Context->RootNode, FSHELP_DIR);
}

STATIC
EFI_STATUS
FindFileIter (
  IN CHAR16           *Name,
  IN FSHELP_FILETYPE  FileType,
  IN NTFS_FILE        *Node,
  IN FSHELP_ITER_CTX  *Context
  )
{
  INTN  Result;

  ASSERT (Name != NULL);
  ASSERT (Node != NULL);
  ASSERT (Context != NULL);

  if (FileType == FSHELP_UNKNOWN) {
    FreePool (Node);
    return EFI_NOT_FOUND;
  }

  if ((FileType & FSHELP_CASE_INSENSITIVE) != 0) {
    Result = OcStriCmp (Context->Name, Name);
  } else {
    Result = StrCmp (Context->Name, Name);
  }

  if (Result != 0) {
    FreePool (Node);
    return EFI_NOT_FOUND;
  }

  *Context->FoundNode = Node;
  *Context->FoundType = FileType;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
NtfsDirHook (
  IN     CHAR16           *Name,
  IN     FSHELP_FILETYPE  FileType,
  IN     NTFS_FILE        *Node,
  IN OUT EFI_FILE_INFO    *Info
  )
{
  EFI_STATUS  Status;

  ASSERT (Name != NULL);
  ASSERT (Node != NULL);
  ASSERT (Info != NULL);

  if (  (Name[0] == L'.')
     && ((Name[1] == 0) || (  (Name[1] == L'.')
                           && (Name[2] == 0))))
  {
    FreeFile (Node);
    return EFI_NOT_FOUND;
  }

  if ((mIndexCounter)-- != 0) {
    FreeFile (Node);
    return EFI_NOT_FOUND;
  }

  Status = StrnCpyS (
             Info->FileName,
             MAX_PATH,
             Name,
             (UINTN)((Info->Size - sizeof (*Info)) / sizeof (CHAR16))
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "NTFS: Could not copy string.\n"));
    FreeFile (Node);
    return Status;
  }

  Info->Size = sizeof (*Info) + StrnLenS (Info->FileName, MAX_PATH) * sizeof (CHAR16);

  NtfsToEfiTime (&Info->CreateTime, Node->CreationTime);
  NtfsToEfiTime (&Info->LastAccessTime, Node->ReadTime);
  NtfsToEfiTime (&Info->ModificationTime, Node->AlteredTime);

  Info->Attribute = EFI_FILE_READ_ONLY;
  if ((FileType & FSHELP_TYPE_MASK) == FSHELP_DIR) {
    Info->Attribute |= EFI_FILE_DIRECTORY;
  }

  FreeFile (Node);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
NtfsDirIter (
  IN CHAR16           *FileName,
  IN FSHELP_FILETYPE  FileType,
  IN NTFS_FILE        *Node,
  IN EFI_NTFS_FILE    *File
  )
{
  ASSERT (FileName != NULL);
  ASSERT (Node != NULL);
  ASSERT (File != NULL);

  if (StrCmp (FileName, File->BaseName) != 0) {
    FreeFile (Node);
    return EFI_NOT_FOUND;
  }

  File->IsDir = (BOOLEAN)((FileType & FSHELP_TYPE_MASK) == FSHELP_DIR);
  FreeFile (Node);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ListFile (
  IN  NTFS_FILE      *Dir,
  IN  UINT8          *Position,
  OUT VOID           *FileOrCtx,
  IN  FUNCTION_TYPE  FunctionType
  )
{
  EFI_STATUS       Status;
  CHAR16           *Filename;
  FSHELP_FILETYPE  Type;
  NTFS_FILE        *DirFile;
  INDEX_ENTRY      *IndexEntry;
  ATTR_FILE_NAME   *AttrFileName;

  ASSERT (Dir != NULL);
  ASSERT (Position != NULL);
  ASSERT (FileOrCtx != NULL);

  IndexEntry = (INDEX_ENTRY *)Position;

  while (TRUE) {
    if (mBufferSize < sizeof (*IndexEntry)) {
      DEBUG ((DEBUG_INFO, "NTFS: (ListFile #1) INDEX_ENTRY is corrupted.\n"));
      return EFI_VOLUME_CORRUPTED;
    }

    if ((IndexEntry->Flags & LAST_INDEX_ENTRY) != 0) {
      break;
    }

    if (mBufferSize < (sizeof (*IndexEntry) + sizeof (*AttrFileName))) {
      DEBUG ((DEBUG_INFO, "NTFS: (ListFile #2) INDEX_ENTRY is corrupted.\n"));
      return EFI_VOLUME_CORRUPTED;
    }

    AttrFileName = (ATTR_FILE_NAME *)((UINT8 *)IndexEntry + sizeof (*IndexEntry));

    //
    // Ignore files in DOS namespace, as they will reappear as Win32 names.
    //
    if ((AttrFileName->FilenameLen != 0) && (AttrFileName->Namespace != DOS)) {
      if ((AttrFileName->Flags & ATTR_REPARSE) != 0) {
        Type = FSHELP_SYMLINK;
      } else if ((AttrFileName->Flags & ATTR_DIRECTORY) != 0) {
        Type = FSHELP_DIR;
      } else {
        Type = FSHELP_REG;
      }

      DirFile = AllocateZeroPool (sizeof (*DirFile));
      if (DirFile == NULL) {
        DEBUG ((DEBUG_INFO, "NTFS: Could not allocate space for DirFile\n"));
        return EFI_OUT_OF_RESOURCES;
      }

      DirFile->File = Dir->File;
      CopyMem (&DirFile->Inode, IndexEntry->FileRecordNumber, 6U);
      DirFile->CreationTime = AttrFileName->CreationTime;
      DirFile->AlteredTime  = AttrFileName->AlteredTime;
      DirFile->ReadTime     = AttrFileName->ReadTime;

      if (mBufferSize < (sizeof (*IndexEntry) + sizeof (*AttrFileName) + AttrFileName->FilenameLen * sizeof (CHAR16))) {
        DEBUG ((DEBUG_INFO, "NTFS: (ListFile #3) INDEX_ENTRY is corrupted.\n"));
        FreePool (DirFile);
        return EFI_VOLUME_CORRUPTED;
      }

      Filename = AllocateZeroPool ((AttrFileName->FilenameLen + 1U) * sizeof (CHAR16));
      if (Filename == NULL) {
        DEBUG ((DEBUG_INFO, "NTFS: Failed to allocate buffer for Filename\n"));
        FreePool (DirFile);
        return EFI_OUT_OF_RESOURCES;
      }

      CopyMem (
        Filename,
        (UINT8 *)AttrFileName + sizeof (*AttrFileName),
        AttrFileName->FilenameLen * sizeof (CHAR16)
        );

      if (AttrFileName->Namespace != POSIX) {
        Type |= FSHELP_CASE_INSENSITIVE;
      }

      switch (FunctionType) {
        case INFO_HOOK:
          Status = NtfsDirIter (Filename, Type, DirFile, FileOrCtx);
          FreePool (DirFile);
          break;
        case DIR_HOOK:
          Status = NtfsDirHook (Filename, Type, DirFile, FileOrCtx);
          FreePool (DirFile);
          break;
        case FILE_ITER:
          Status = FindFileIter (Filename, Type, DirFile, FileOrCtx);
          break;
      }

      FreePool (Filename);

      if (Status == EFI_SUCCESS) {
        return EFI_SUCCESS;
      }
    }

    if (  (mBufferSize < IndexEntry->IndexEntryLength)
       || (IndexEntry->IndexEntryLength == 0))
    {
      DEBUG ((DEBUG_INFO, "NTFS: (ListFile #4) INDEX_ENTRY is corrupted.\n"));
      return EFI_VOLUME_CORRUPTED;
    }

    mBufferSize -= IndexEntry->IndexEntryLength;
    IndexEntry   = (INDEX_ENTRY *)((UINT8 *)IndexEntry + IndexEntry->IndexEntryLength);
  }

  return EFI_NOT_FOUND;
}

STATIC
EFI_STATUS
FindFile (
  IN CHAR16      *CurrentPath,
  IN FSHELP_CTX  *Context
  )
{
  EFI_STATUS       Status;
  CHAR16           *Name;
  CHAR16           *Next;
  NTFS_FILE        *FoundNode;
  FSHELP_FILETYPE  FoundType;
  FSHELP_ITER_CTX  IterCtx;
  CHAR16           *Symlink;
  CHAR16           *PathPart;

  ASSERT (CurrentPath != NULL);
  ASSERT (Context != NULL);

  for (Name = CurrentPath; ; Name = Next) {
    FoundNode = NULL;
    FoundType = FSHELP_UNKNOWN;

    while (*Name == L'/') {
      ++Name;
    }

    if (*Name == L'\0') {
      return EFI_SUCCESS;
    }

    for (Next = Name; (*Next != L'\0') && (*Next != L'/'); ++Next) {
      //
      // Search for L'\0' or L'/'.
      //
    }

    if (Context->CurrentNode->Type != FSHELP_DIR) {
      DEBUG ((DEBUG_INFO, "NTFS: Not a directory\n"));
      return EFI_INVALID_PARAMETER;
    }

    if ((Next - Name == 1U) && (Name[0] == L'.')) {
      continue;
    }

    if ((Next - Name == 2U) && (Name[0] == L'.') && (Name[1] == L'.')) {
      GoUpALevel (Context);
      continue;
    }

    PathPart = AllocateZeroPool ((Next - Name + 1U) * sizeof (CHAR16));
    if (PathPart == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    CopyMem (PathPart, Name, (Next - Name) * sizeof (CHAR16));

    IterCtx.Name      = PathPart;
    IterCtx.FoundNode = &FoundNode;
    IterCtx.FoundType = &FoundType;

    Status = IterateDir (Context->CurrentNode->Node, &IterCtx, FILE_ITER);
    FreePool (PathPart);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (FoundNode == NULL) {
      break;
    }

    Status = PushNode (Context, FoundNode, FoundType);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (Context->CurrentNode->Type == FSHELP_SYMLINK) {
      if (++Context->SymlinkDepth == 8U) {
        DEBUG ((DEBUG_INFO, "NTFS: Too deep nesting of symlinks\n"));
        return EFI_INVALID_PARAMETER;
      }

      Symlink = ReadSymlink (Context->CurrentNode->Node);
      if (Symlink == NULL) {
        DEBUG ((DEBUG_INFO, "NTFS: Symlink leeds nowhere\n"));
        return EFI_INVALID_PARAMETER;
      }

      if (Symlink[0] == L'/') {
        //
        // Symlink is an absolute path
        //
        Status = GoToRoot (Context);
        if (EFI_ERROR (Status)) {
          return Status;
        }
      } else {
        GoUpALevel (Context);
      }

      Status = FindFile (Symlink, Context);
      FreePool (Symlink);
      if (EFI_ERROR (Status)) {
        return Status;
      }
    }
  }

  DEBUG ((DEBUG_INFO, "NTFS: File `%s' not found\n", IterCtx.Name));
  return EFI_NOT_FOUND;
}

EFI_STATUS
FsHelpFindFile (
  IN  CONST CHAR16     *Path,
  IN  NTFS_FILE        *RootNode,
  OUT NTFS_FILE        **FoundNode,
  IN  FSHELP_FILETYPE  Type
  )
{
  EFI_STATUS       Status;
  FSHELP_CTX       Context;
  FSHELP_FILETYPE  FoundType;

  ASSERT (Path != NULL);
  ASSERT (RootNode != NULL);
  ASSERT (FoundNode != NULL);

  Context.Path         = Path;
  Context.RootNode     = RootNode;
  Context.SymlinkDepth = 0;
  Context.CurrentNode  = NULL;

  if (Path[0] != L'/') {
    DEBUG ((DEBUG_INFO, "NTFS: Invalid file name `%s'\n", Path));
    return EFI_INVALID_PARAMETER;
  }

  Status = GoToRoot (&Context);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = FindFile ((CHAR16 *)Path, &Context);
  if (EFI_ERROR (Status)) {
    FreeStack (&Context);
    return Status;
  }

  *FoundNode = Context.CurrentNode->Node;
  FoundType  = Context.CurrentNode->Type;
  //
  // Do not free the node
  //
  Context.CurrentNode->Node = NULL;
  FreeStack (&Context);

  if (FoundType != Type) {
    if (*FoundNode != RootNode) {
      FreeFile (*FoundNode);
      FreePool (*FoundNode);
    }

    if (Type == FSHELP_REG) {
      DEBUG ((DEBUG_INFO, "NTFS: Not a regular file\n"));
    } else if (Type == FSHELP_DIR) {
      DEBUG ((DEBUG_INFO, "NTFS: Not a directory\n"));
    }

    Status = EFI_VOLUME_CORRUPTED;
  }

  return Status;
}

EFI_STATUS
IterateDir (
  IN NTFS_FILE      *Dir,
  IN VOID           *FileOrCtx,
  IN FUNCTION_TYPE  FunctionType
  )
{
  EFI_STATUS           Status;
  NTFS_ATTR            Attr;
  ATTR_HEADER_RES      *Res;
  ATTR_HEADER_NONRES   *Non;
  ATTR_INDEX_ROOT      *Index;
  INDEX_RECORD_HEADER  *IndexRecord;
  UINT8                *BitIndex;
  UINT8                *BitMap;
  UINTN                BitMapLen;
  UINT8                Bit;
  UINTN                Number;
  UINTN                FileRecordSize;
  UINTN                IndexRecordSize;

  ASSERT (Dir != NULL);
  ASSERT (FileOrCtx != NULL);

  FileRecordSize  = Dir->File->FileSystem->FileRecordSize;
  IndexRecordSize = Dir->File->FileSystem->IndexRecordSize;

  if (!Dir->InodeRead) {
    Status = InitFile (Dir, Dir->Inode);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  IndexRecord = NULL;
  BitMap      = NULL;

  Status = InitAttr (&Attr, Dir);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Search in $INDEX_ROOT
  //
  while (TRUE) {
    Res = (ATTR_HEADER_RES *)FindAttr (&Attr, AT_INDEX_ROOT);
    if (Res == NULL) {
      DEBUG ((DEBUG_INFO, "NTFS: no $INDEX_ROOT\n"));
      FreeAttr (&Attr);
      return EFI_VOLUME_CORRUPTED;
    }

    mBufferSize = FileRecordSize - (Attr.Current - Attr.BaseMftRecord->FileRecord);

    if (  (mBufferSize < sizeof (*Res))
       || (mBufferSize < (Res->NameOffset + 8U))
       || (mBufferSize < Res->InfoOffset))
    {
      DEBUG ((DEBUG_INFO, "NTFS: (IterateDir #1) $INDEX_ROOT is corrupted.\n"));
      FreeAttr (&Attr);
      return EFI_VOLUME_CORRUPTED;
    }

    mBufferSize -= Res->InfoOffset;

    if (  (Res->NonResFlag != 0)
       || (Res->NameLength != 4U)
       || (Res->NameOffset != sizeof (*Res))
       || (CompareMem ((UINT8 *)Res + Res->NameOffset, L"$I30", 8U) != 0))
    {
      continue;
    }

    if (mBufferSize < sizeof (*Index)) {
      DEBUG ((DEBUG_INFO, "NTFS: (IterateDir #1.1) $INDEX_ROOT is corrupted.\n"));
      FreeAttr (&Attr);
      return EFI_VOLUME_CORRUPTED;
    }

    Index = (ATTR_INDEX_ROOT *)((UINT8 *)Res + Res->InfoOffset);
    if (Index->Root.Type != AT_FILENAME) {
      continue;
    }

    break;
  }

  if (mBufferSize < (sizeof (INDEX_ROOT) + Index->FirstEntryOffset)) {
    DEBUG ((DEBUG_INFO, "NTFS: (IterateDir #2) $INDEX_ROOT is corrupted.\n"));
    FreeAttr (&Attr);
    return EFI_VOLUME_CORRUPTED;
  }

  mBufferSize -= sizeof (INDEX_ROOT) + Index->FirstEntryOffset;

  Status = ListFile (
             Dir,
             (UINT8 *)Index + sizeof (INDEX_ROOT) + Index->FirstEntryOffset,
             FileOrCtx,
             FunctionType
             );
  if (!EFI_ERROR (Status)) {
    FreeAttr (&Attr);
    return EFI_SUCCESS;
  }

  //
  // Search in $INDEX_ALLOCATION
  //
  BitIndex  = NULL;
  BitMapLen = 0;
  FreeAttr (&Attr);

  Status = InitAttr (&Attr, Dir);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  while ((Non = (ATTR_HEADER_NONRES *)FindAttr (&Attr, AT_BITMAP)) != NULL) {
    mBufferSize = FileRecordSize - (Attr.Current - Attr.BaseMftRecord->FileRecord);

    if (  (mBufferSize < sizeof (*Non))
       || (mBufferSize < (Non->NameOffset + 8U)))
    {
      DEBUG ((DEBUG_INFO, "NTFS: (IterateDir #3) $INDEX_ROOT is corrupted.\n"));
      FreeAttr (&Attr);
      return EFI_VOLUME_CORRUPTED;
    }

    if (  (Non->NameLength == 4U)
       && (CompareMem ((UINT8 *)Non + Non->NameOffset, L"$I30", 8U) == 0))
    {
      BitMapLen = (Non->NonResFlag == 0) ?
                  ((ATTR_HEADER_RES *)Non)->InfoLength :
                  (UINTN)Non->AllocatedSize;

      if (BitMapLen > MAX_FILE_SIZE) {
        DEBUG ((DEBUG_INFO, "NTFS: (IterateDir) File is too huge.\n"));
        return EFI_OUT_OF_RESOURCES;
      }

      BitMap = AllocateZeroPool (BitMapLen);
      if (BitMap == NULL) {
        FreeAttr (&Attr);
        return EFI_OUT_OF_RESOURCES;
      }

      if (Non->NonResFlag == 0) {
        if (mBufferSize < (((ATTR_HEADER_RES *)Non)->InfoOffset + BitMapLen)) {
          DEBUG ((DEBUG_INFO, "NTFS: (IterateDir #4) $INDEX_ROOT is corrupted.\n"));
          FreeAttr (&Attr);
          FreePool (BitMap);
          return EFI_VOLUME_CORRUPTED;
        }

        CopyMem (
          BitMap,
          (UINT8 *)Non + ((ATTR_HEADER_RES *)Non)->InfoOffset,
          BitMapLen
          );
      } else {
        Status = ReadData (&Attr, (UINT8 *)Non, BitMap, 0, BitMapLen);
        if (EFI_ERROR (Status)) {
          DEBUG ((DEBUG_INFO, "NTFS: Failed to read non-resident $BITMAP\n"));
          FreeAttr (&Attr);
          FreePool (BitMap);
          return Status;
        }

        BitMapLen = (UINTN)Non->RealSize;
      }

      BitIndex = BitMap;
      break;
    }
  }

  FreeAttr (&Attr);
  Non = (ATTR_HEADER_NONRES *)LocateAttr (&Attr, Dir, AT_INDEX_ALLOCATION);

  while (Non != NULL) {
    mBufferSize = FileRecordSize - (Attr.Current - Attr.BaseMftRecord->FileRecord);

    if (  (mBufferSize < sizeof (*Non))
       || (mBufferSize < (Non->NameOffset + 8U)))
    {
      DEBUG ((DEBUG_INFO, "NTFS: (IterateDir #5) $INDEX_ROOT is corrupted.\n"));
      FreeAttr (&Attr);
      if (BitIndex != NULL) {
        FreePool (BitMap);
      }

      return EFI_VOLUME_CORRUPTED;
    }

    if (  (Non->NonResFlag == 1U)
       && (Non->NameLength == 4U)
       && (Non->NameOffset == sizeof (*Non))
       && (CompareMem ((UINT8 *)Non + Non->NameOffset, L"$I30", 8U) == 0))
    {
      break;
    }

    Non = (ATTR_HEADER_NONRES *)FindAttr (&Attr, AT_INDEX_ALLOCATION);
  }

  if ((Non == NULL) && (BitIndex != NULL)) {
    DEBUG ((DEBUG_INFO, "NTFS: $BITMAP without $INDEX_ALLOCATION\n"));
    FreeAttr (&Attr);
    FreePool (BitMap);
    return EFI_VOLUME_CORRUPTED;
  }

  if (BitIndex != NULL) {
    IndexRecord = AllocateZeroPool (IndexRecordSize);
    if (IndexRecord == NULL) {
      FreeAttr (&Attr);
      FreePool (BitMap);
      return EFI_OUT_OF_RESOURCES;
    }

    Bit = 1U;
    for (Number = 0; Number < (BitMapLen * 8U); Number++) {
      if ((*BitIndex & Bit) != 0) {
        Status = ReadAttr (
                   &Attr,
                   (UINT8 *)IndexRecord,
                   Number * IndexRecordSize,
                   IndexRecordSize
                   );
        if (EFI_ERROR (Status)) {
          FreeAttr (&Attr);
          FreePool (BitMap);
          FreePool (IndexRecord);
          return Status;
        }

        Status = Fixup (
                   (UINT8 *)IndexRecord,
                   IndexRecordSize,
                   SIGNATURE_32 ('I', 'N', 'D', 'X'),
                   Dir->File->FileSystem->SectorSize
                   );
        if (EFI_ERROR (Status)) {
          FreeAttr (&Attr);
          FreePool (BitMap);
          FreePool (IndexRecord);
          return Status;
        }

        if (  (IndexRecordSize < sizeof (*IndexRecord))
           || (IndexRecordSize < (sizeof (INDEX_HEADER) + IndexRecord->IndexEntriesOffset)))
        {
          DEBUG ((DEBUG_INFO, "NTFS: $INDEX_ALLOCATION is corrupted.\n"));
          FreeAttr (&Attr);
          FreePool (BitMap);
          FreePool (IndexRecord);
          return EFI_VOLUME_CORRUPTED;
        }

        mBufferSize = IndexRecordSize - (sizeof (INDEX_HEADER) + IndexRecord->IndexEntriesOffset);

        Status = ListFile (
                   Dir,
                   (UINT8 *)IndexRecord + sizeof (INDEX_HEADER) + IndexRecord->IndexEntriesOffset,
                   FileOrCtx,
                   FunctionType
                   );
        if (!EFI_ERROR (Status)) {
          FreeAttr (&Attr);
          FreePool (BitMap);
          FreePool (IndexRecord);
          return Status;
        }
      }

      Bit <<= 1U;
      if (Bit == 0) {
        Bit = 1U;
        ++BitIndex;
      }
    }

    FreeAttr (&Attr);
    FreePool (BitMap);
    FreePool (IndexRecord);
  }

  return Status;
}

EFI_STATUS
RelativeToAbsolute (
  OUT CHAR16  *Dest,
  IN  CHAR16  *Source
  )
{
  CHAR16  *Buffer;
  CHAR16  *BPointer;
  CHAR16  *Start;
  CHAR16  *End;
  UINT32  Skip;

  ASSERT (Dest != NULL);
  ASSERT (Source != NULL);

  Skip = 0;

  Buffer   = AllocateZeroPool (StrSize (Source));
  BPointer = Buffer;

  End   = Source + StrLen (Source);
  Start = End;
  while (Start > Source) {
    while (*Start != L'/') {
      --Start;
    }

    if ((Start[1] == L'.') && ((Start[2] == L'/') || (Start[2] == L'\0'))) {
      End = Start;
      --Start;
      continue;
    }

    if ((Start[1] == L'.') && (Start[2] == L'.') && ((Start[3] == L'/') || (Start[3] == L'\0'))) {
      End = Start;
      --Start;
      ++Skip;
      continue;
    }

    if (Skip > 0) {
      End = Start;
      --Start;
      --Skip;
      continue;
    }

    CopyMem (
      BPointer,
      Start + 1U,
      (End - Start - 1U) * sizeof (CHAR16)
      );
    BPointer += End - Start - 1U;

    *BPointer = L'/';
    ++BPointer;

    End = Start;
    --Start;
  }

  if (Skip > 0) {
    DEBUG ((DEBUG_INFO, "NTFS: Invalid path: root has no parent.\n"));
    FreePool (Buffer);
    return EFI_DEVICE_ERROR;
  }

  End   = BPointer - 1U;
  Start = End - 1U;
  while (Start > Buffer) {
    while ((Start >= Buffer) && (*Start != L'/')) {
      --Start;
    }

    *Dest = L'/';
    ++Dest;

    CopyMem (
      Dest,
      Start + 1U,
      (End - Start - 1U) * sizeof (CHAR16)
      );
    Dest += End - Start - 1U;

    End = Start;
    --Start;
  }

  FreePool (Buffer);

  return EFI_SUCCESS;
}

/**
    NTFS Time is the number of 100ns units since Jan 1, 1601.
    The signifigance of this date is that it is the beginning
    of the first full century of the Gregorian Calendar.
    The time displayed in Shell will only match the time
    displayed in Windows if the Windows time zone is set to
    Coordinated Universal Time (UTC). Otherwise, it will be
    skewed by the time zone setting.
**/
VOID
NtfsToEfiTime (
  EFI_TIME  *EfiTime,
  UINT64    NtfsTime
  )
{
  UINT64  Remainder64;
  UINT32  Remainder32;
  UINT16  Year;
  UINT8   Month;
  UINT32  Day;
  UINT32  LastDay;
  UINT32  PrevLD;
  UINT8   Index;
  UINT64  Temp;

  ASSERT (EfiTime != NULL);

  EfiTime->TimeZone = EFI_UNSPECIFIED_TIMEZONE;
  EfiTime->Pad1     = 0;
  EfiTime->Daylight = 0;
  EfiTime->Pad2     = 0;
  //
  // Because calendars are 1-based (there is no day 0), we have to add
  // a day's worth of 100-ns units to make these calculations come out correct.
  //
  Year = GREGORIAN_START;
  for (Temp = NtfsTime + DAY_IN_100NS; Temp > YEAR_IN_100NS; Temp -= YEAR_IN_100NS) {
    if (LEAP_YEAR) {
      //
      // Subtract an extra day for leap year
      //
      Temp -= DAY_IN_100NS;
    }

    ++Year;
  }

  //
  // From what's left, get the day, hour, minute, second and nanosecond.
  //
  Day = (UINT32)DivU64x64Remainder (Temp, DAY_IN_100NS, &Remainder64);
  if (Day == 0) {
    //
    // Special handling for last day of year
    //
    Year -= 1U;
    Day   = LEAP_YEAR ? 366U : 365U;
  }

  EfiTime->Year       = Year;
  EfiTime->Hour       = (UINT8)DivU64x64Remainder (Remainder64, HOUR_IN_100NS, &Remainder64);
  EfiTime->Minute     = (UINT8)DivU64x32Remainder (Remainder64, MINUTE_IN_100NS, &Remainder32);
  EfiTime->Second     = (UINT8)(Remainder32 / SECOND_IN_100NS);
  Remainder32        %= SECOND_IN_100NS;
  EfiTime->Nanosecond = Remainder32 * UNIT_IN_NS;
  //
  // "Day" now contains the ordinal date. We have to convert that to month and day.
  //
  Month   = 1U;
  LastDay = 31U;
  PrevLD  = 0;
  for (Index = 1U; Index < 13U; ++Index) {
    if (Day > LastDay) {
      Month   += 1U;
      PrevLD   = LastDay;
      LastDay += mDaysPerMonth[Index];
      if ((Index == 1U) && LEAP_YEAR) {
        ++LastDay;
      }
    }
  }

  EfiTime->Month = Month;
  EfiTime->Day   = (UINT8)(Day - PrevLD);
}
