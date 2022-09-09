/** @file

OcSerializeLib

Copyright (c) 2018, vit9696

All rights reserved.

This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <Library/OcSerializeLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>

#if !defined(MDEPKG_NDEBUG)

STATIC
CONST CHAR8 *
mSchemaTypeNames[] = {
  [OC_SCHEMA_VALUE_BOOLEAN] = "boolean",
  [OC_SCHEMA_VALUE_INTEGER] = "integer",
  [OC_SCHEMA_VALUE_DATA] = "data",
  [OC_SCHEMA_VALUE_STRING] = "string",
  [OC_SCHEMA_VALUE_MDATA] = "mdata"
};

STATIC
CONST CHAR8 *
GetSchemaTypeName (
  IN UINT32 Type
  )
{
  if (Type < ARRAY_SIZE (mSchemaTypeNames)) {
    return mSchemaTypeNames[Type];
  }
  return "custom";
}

#endif

OC_SCHEMA *
LookupConfigSchema (
  IN OC_SCHEMA      *SortedList,
  IN UINT32         Size,
  IN CONST CHAR8    *Name
  )
{
  UINT32  Start;
  UINT32  End;
  UINT32  Curr;
  INTN    Cmp;

  if (Size == 0) {
    return NULL;
  }

  //
  // Classic binary search in a sorted string list.
  //
  Start = 0;
  End   = Size - 1;

  while (Start <= End) {
    Curr = (Start + End) / 2;
    Cmp = AsciiStrCmp (SortedList[Curr].Name, Name);

    if (Cmp == 0) {
      return &SortedList[Curr];
    } else if (Cmp < 0) {
      Start = Curr + 1;
    } else if (Curr > 0) {
      End = Curr - 1;
    } else {
      //
      // Even the first element does not match, required due to unsigned End.
      //
      return NULL;
    }
  }

  return NULL;
}

VOID
ParseSerializedDict (
      OUT  VOID            *Serialized,
  IN       XML_NODE        *Node,
  IN       OC_SCHEMA_INFO  *Info,
  IN       CONST CHAR8     *Context     OPTIONAL,
  IN  OUT  UINT32          *ErrorCount  OPTIONAL
  )
{
  UINT32         DictSize;
  UINT32         Index;
  UINT32         Index2;
  CONST CHAR8    *CurrentKey;
  XML_NODE       *CurrentValue;
  XML_NODE       *OldValue;
  OC_SCHEMA      *NewSchema;

  DictSize = PlistDictChildren (Node);

  for (Index = 0; Index < DictSize; Index++) {
    CurrentKey = PlistKeyValue (PlistDictChild (Node, Index, &CurrentValue));

    if (CurrentKey == NULL) {
      DEBUG ((DEBUG_WARN, "警告: 在索引%u处没有序列化键值, 位置: <%a>!\n", Index, Context));
      if (ErrorCount != NULL) {
        ++*ErrorCount;
      }
      continue;
    }

    //
    // Skip comments.
    //
    if (CurrentKey[0] == '#') {
      continue;
    }

    DEBUG ((DEBUG_VERBOSE, "警告: 在%a索引%u处解析序列化!\n", CurrentKey, Index));

    //
    // We do not protect from duplicating serialized entries.
    //
    NewSchema = LookupConfigSchema (Info->Dict.Schema, Info->Dict.SchemaSize, CurrentKey);

    if (NewSchema == NULL) {
      DEBUG ((DEBUG_WARN, "警告: 发现%a选项在索引%u处，此版本没有这个选项或已移动位置, 位置: <%a>!\n", CurrentKey, Index, Context));
      if (ErrorCount != NULL) {
        ++*ErrorCount;
      }
      continue;
    }

    OldValue = CurrentValue;
    CurrentValue = PlistNodeCast (CurrentValue, NewSchema->Type);
    if (CurrentValue == NULL) {
      DEBUG ((
        DEBUG_WARN,
        "警告:在索引%u处的%a没有类型匹配, 预期类型是%a得到的是%a, 位置: <%a>!\n",
        CurrentKey,
        Index,
        GetSchemaTypeName (NewSchema->Type),
        XmlNodeName (OldValue),
        Context
        ));
      if (ErrorCount != NULL) {
        ++*ErrorCount;
      }
      continue;
    }

    NewSchema->Apply (Serialized, CurrentValue, &NewSchema->Info, CurrentKey, ErrorCount);
  }

  DEBUG_CODE_BEGIN ();

  for (Index = 0; Index < Info->Dict.SchemaSize; ++Index) {
    if (Info->Dict.Schema[Index].Optional) {
      continue;
    }

    for (Index2 = 0; Index2 < DictSize; ++Index2) {
      CurrentKey = PlistKeyValue (PlistDictChild (Node, Index2, NULL));

      if (CurrentKey == NULL) {
        continue;
      }

      if (AsciiStrCmp (CurrentKey, Info->Dict.Schema[Index].Name) == 0) {
        break;
      }
    }

    if (Index2 == DictSize) {
      DEBUG ((
        DEBUG_WARN,
        "警告: 缺少 %a 键值, 位置: <%a>!\n",
        Info->Dict.Schema[Index].Name,
        Context
        ));
      if (ErrorCount != NULL) {
        ++*ErrorCount;
      }
    }
  }

  DEBUG_CODE_END ();
}

VOID
ParseSerializedValue (
      OUT  VOID            *Serialized,
  IN       XML_NODE        *Node,
  IN       OC_SCHEMA_INFO  *Info,
  IN       CONST CHAR8     *Context     OPTIONAL,
  IN  OUT  UINT32          *ErrorCount  OPTIONAL
  )
{
  BOOLEAN  Result;
  VOID     *Field;
  UINT32   Size;

  Result = FALSE;
  Field  = OC_SCHEMA_FIELD (Serialized, VOID, Info->Value.Field);
  Size   = Info->Value.FieldSize;

  switch (Info->Value.Type) {
    case OC_SCHEMA_VALUE_BOOLEAN:
      Result = PlistBooleanValue (Node, (BOOLEAN *) Field);
      break;
    case OC_SCHEMA_VALUE_INTEGER:
      Result = PlistIntegerValue (Node, Field, Size, FALSE);
      break;
    case OC_SCHEMA_VALUE_DATA:
      Result = PlistDataValue (Node, Field, &Size);
      break;
    case OC_SCHEMA_VALUE_STRING:
      Result = PlistStringValue (Node, Field, &Size);
      break;
    case OC_SCHEMA_VALUE_MDATA:
      Result = PlistMetaDataValue (Node, Field, &Size);
      break;
  }

  if (Result == FALSE) {
    DEBUG ((
      DEBUG_WARN,
      "警告: 无法将%a字段解析为类型为%a和<%a>的值, 位置: <%a>!\n",
      XmlNodeName (Node),
      GetSchemaTypeName (Info->Value.Type),
      XmlNodeContent (Node) != NULL ? XmlNodeContent (Node) : "empty",
      Context
      ));
    if (ErrorCount != NULL) {
      ++*ErrorCount;
    }
  }
}

VOID
ParseSerializedBlob (
      OUT  VOID            *Serialized,
  IN       XML_NODE        *Node,
  IN       OC_SCHEMA_INFO  *Info,
  IN       CONST CHAR8     *Context     OPTIONAL,
  IN  OUT  UINT32          *ErrorCount  OPTIONAL
  )
{
  BOOLEAN  Result;
  VOID     *Field;
  UINT32   Size;
  VOID     *BlobMemory;
  UINT32   *BlobSize;

  Result = FALSE;

  switch (Info->Blob.Type) {
    case OC_SCHEMA_BLOB_DATA:
      Result = PlistDataSize (Node, &Size);
      break;
    case OC_SCHEMA_BLOB_STRING:
      Result = PlistStringSize (Node, &Size);
      break;
    case OC_SCHEMA_BLOB_MDATA:
      Result = PlistMetaDataSize (Node, &Size);
      break;
  }

  if (Result == FALSE) {
    DEBUG ((
      DEBUG_WARN,
      "警告: 无法计算类型为%a的包含<%a>的%a字段的大小, 位置: <%a>!\n",
      XmlNodeName (Node),
      XmlNodeContent (Node) != NULL ? XmlNodeContent (Node) : "empty",
      GetSchemaTypeName (Info->Blob.Type),
      Context
      ));
    if (ErrorCount != NULL) {
      ++*ErrorCount;
    }
    return;
  }

  Field  = OC_SCHEMA_FIELD (Serialized, VOID, Info->Blob.Field);
  BlobMemory = OcBlobAllocate (Field, Size, &BlobSize);

  if (BlobMemory == NULL) {
    DEBUG ((
      DEBUG_INFO,
      "警告: 无法分配类型为%a的%u个字节%a字段, 位置: <%a>!\n",
      Size,
      XmlNodeName (Node),
      GetSchemaTypeName (Info->Value.Type),
      Context
      ));
    if (ErrorCount != NULL) {
      ++*ErrorCount;
    }
    return;
  }

  Result = FALSE;

  switch (Info->Blob.Type) {
    case OC_SCHEMA_BLOB_DATA:
      Result = PlistDataValue (Node, (UINT8 *) BlobMemory, BlobSize);
      break;
    case OC_SCHEMA_BLOB_STRING:
      Result = PlistStringValue (Node, (CHAR8 *) BlobMemory, BlobSize);
      break;
    case OC_SCHEMA_BLOB_MDATA:
      Result = PlistMetaDataValue (Node, (UINT8 *) BlobMemory, BlobSize);
      break;
  }

  if (Result == FALSE) {
    DEBUG ((
      DEBUG_WARN,
      "警告: 无法将%a字段解析为类型为%a和<%a>, 位置: <%a>!\n",
      XmlNodeName (Node),
      GetSchemaTypeName (Info->Value.Type),
      XmlNodeContent (Node) != NULL ? XmlNodeContent (Node) : "empty",
      Context
      ));
    if (ErrorCount != NULL) {
      ++*ErrorCount;
    }
  }
}

VOID
ParseSerializedMap (
      OUT  VOID            *Serialized,
  IN       XML_NODE        *Node,
  IN       OC_SCHEMA_INFO  *Info,
  IN       CONST CHAR8     *Context     OPTIONAL,
  IN  OUT  UINT32          *ErrorCount  OPTIONAL
  )
{
  UINT32       DictSize;
  UINT32       Index;
  CONST CHAR8  *CurrentKey;
  UINT32       CurrentKeyLen;
  XML_NODE     *ChildNode;
  VOID         *NewValue;
  VOID         *NewKey;
  VOID         *NewKeyValue;
  BOOLEAN      Success;

  DictSize = PlistDictChildren (Node);

  for (Index = 0; Index < DictSize; Index++) {
    CurrentKey = PlistKeyValue (PlistDictChild (Node, Index, &ChildNode));
    CurrentKeyLen = CurrentKey != NULL ? (UINT32) (AsciiStrLen (CurrentKey) + 1) : 0;

    if (CurrentKeyLen == 0) {
      DEBUG ((DEBUG_INFO, "警告: 在索引%u处没有获取序列化key!\n", Index));
      if (ErrorCount != NULL) {
        ++*ErrorCount;
      }
      continue;
    }

    //
    // Skip comments.
    //
    if (CurrentKey[0] == '#') {
      continue;
    }

    if (PlistNodeCast (ChildNode, Info->List.Schema->Type) == NULL) {
      DEBUG ((DEBUG_INFO, "警告: 索引%u处没有有效的序列化值!\n", Index));
      if (ErrorCount != NULL) {
        ++*ErrorCount;
      }
      continue;
    }

    Success = OcListEntryAllocate (
      OC_SCHEMA_FIELD (Serialized, VOID, Info->List.Field),
      &NewValue,
      &NewKey
      );
    if (Success == FALSE) {
      DEBUG ((DEBUG_INFO, "警告: 无法在索引%u处插入序列化的字典!\n", Index));
      if (ErrorCount != NULL) {
        ++*ErrorCount;
      }
      continue;
    }

    NewKeyValue = OcBlobAllocate (NewKey, CurrentKeyLen, NULL);
    if (NewKeyValue != NULL) {
      AsciiStrnCpyS ((CHAR8 *) NewKeyValue, CurrentKeyLen, CurrentKey, CurrentKeyLen - 1);
    } else {
      DEBUG ((DEBUG_INFO, "警告: 无法在索引%u处分配键名!\n", Index));
      if (ErrorCount != NULL) {
        ++*ErrorCount;
      }
    }

    Info->List.Schema->Apply (NewValue, ChildNode, &Info->List.Schema->Info, CurrentKey, ErrorCount);
  }
}

VOID
ParseSerializedArray (
      OUT  VOID            *Serialized,
  IN       XML_NODE        *Node,
  IN       OC_SCHEMA_INFO  *Info,
  IN       CONST CHAR8     *Context     OPTIONAL,
  IN  OUT  UINT32          *ErrorCount  OPTIONAL
  )
{
  UINT32    ArraySize;
  UINT32    Index;
  XML_NODE  *ChildNode;
  VOID      *NewValue;
  BOOLEAN   Success;

  ArraySize = XmlNodeChildren (Node);

  for (Index = 0; Index < ArraySize; Index++) {
    ChildNode = PlistNodeCast (XmlNodeChild (Node, Index), Info->List.Schema->Type);

    DEBUG ((DEBUG_VERBOSE, "警告: 处理数组%u/%u元素\n", Index + 1, ArraySize));

    if (ChildNode == NULL) {
      DEBUG ((DEBUG_INFO, "警告: 无法在索引%u处序列化数组!\n", Index));
      if (ErrorCount != NULL) {
        ++*ErrorCount;
      }
      continue;
    }

    Success = OcListEntryAllocate (
      OC_SCHEMA_FIELD (Serialized, VOID, Info->List.Field),
      &NewValue,
      NULL
      );
    if (Success == FALSE) {
      DEBUG ((DEBUG_INFO, "警告: 无法在索引%u处插入序列化的数组!\n", Index));
      if (ErrorCount != NULL) {
        ++*ErrorCount;
      }
      continue;
    }

    Info->List.Schema->Apply (NewValue, ChildNode, &Info->List.Schema->Info, Context, ErrorCount);
  }
}

BOOLEAN
ParseSerialized (
      OUT  VOID                *Serialized,
  IN       OC_SCHEMA_INFO      *RootSchema,
  IN       VOID                *PlistBuffer,
  IN       UINT32              PlistSize,
  IN  OUT  UINT32              *ErrorCount  OPTIONAL
  )
{
  XML_DOCUMENT        *Document;
  XML_NODE            *RootDict;

  Document = XmlDocumentParse (PlistBuffer, PlistSize, FALSE);

  if (Document == NULL) {
    DEBUG ((DEBUG_INFO, "警告: 无法解析plist文件!\n"));
    if (ErrorCount != NULL) {
      ++*ErrorCount;
    }
    return FALSE;
  }

  RootDict = PlistNodeCast (PlistDocumentRoot (Document), PLIST_NODE_TYPE_DICT);

  if (RootDict == NULL) {
    DEBUG ((DEBUG_INFO, "警告: 无法获得plist的根!\n"));
    XmlDocumentFree (Document);
    if (ErrorCount != NULL) {
      ++*ErrorCount;
    }
    return FALSE;
  }

  ParseSerializedDict (
    Serialized,
    RootDict,
    RootSchema,
    "root",
    ErrorCount
    );

  XmlDocumentFree (Document);
  return TRUE;
}