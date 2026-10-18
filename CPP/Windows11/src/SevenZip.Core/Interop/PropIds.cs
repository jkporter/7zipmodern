namespace SevenZip.Core.Interop;

/// <summary>Item / archive property identifiers (CPP/7zip/PropID.h).</summary>
public static class PropId
{
    public const uint NoProperty = 0;
    public const uint MainSubfile = 1;
    public const uint HandlerItemIndex = 2;
    public const uint Path = 3;
    public const uint Name = 4;
    public const uint Extension = 5;
    public const uint IsDir = 6;
    public const uint Size = 7;
    public const uint PackSize = 8;
    public const uint Attrib = 9;
    public const uint CTime = 10;
    public const uint ATime = 11;
    public const uint MTime = 12;
    public const uint Solid = 13;
    public const uint Commented = 14;
    public const uint Encrypted = 15;
    public const uint SplitBefore = 16;
    public const uint SplitAfter = 17;
    public const uint DictionarySize = 18;
    public const uint Crc = 19;
    public const uint Type = 20;
    public const uint IsAnti = 21;
    public const uint Method = 22;
    public const uint HostOS = 23;
    public const uint FileSystem = 24;
    public const uint User = 25;
    public const uint Group = 26;
    public const uint Block = 27;
    public const uint Comment = 28;
    public const uint Position = 29;
    public const uint Prefix = 30;
    public const uint NumSubDirs = 31;
    public const uint NumSubFiles = 32;
    public const uint UnpackVer = 33;
    public const uint Volume = 34;
    public const uint IsVolume = 35;
    public const uint Offset = 36;
    public const uint Links = 37;
    public const uint NumBlocks = 38;
    public const uint NumVolumes = 39;
    public const uint TimeType = 40;
    public const uint Bit64 = 41;
    public const uint BigEndian = 42;
    public const uint Cpu = 43;
    public const uint PhySize = 44;
    public const uint HeadersSize = 45;
    public const uint Checksum = 46;
    public const uint Characts = 47;
    public const uint Va = 48;
    public const uint Id = 49;
    public const uint ShortName = 50;
    public const uint CreatorApp = 51;
    public const uint SectorSize = 52;
    public const uint PosixAttrib = 53;
    public const uint SymLink = 54;
    public const uint Error = 55;
    public const uint TotalSize = 56;
    public const uint FreeSpace = 57;
    public const uint ClusterSize = 58;
    public const uint VolumeName = 59;
    public const uint LocalName = 60;
    public const uint Provider = 61;
    public const uint NtSecure = 62;
    public const uint IsAltStream = 63;
    public const uint IsAux = 64;
    public const uint IsDeleted = 65;
    public const uint IsTree = 66;
    public const uint Sha1 = 67;
    public const uint Sha256 = 68;
    public const uint ErrorType = 69;
    public const uint NumErrors = 70;
    public const uint ErrorFlags = 71;
    public const uint WarningFlags = 72;
    public const uint Warning = 73;
    public const uint NumStreams = 74;
    public const uint NumAltStreams = 75;
    public const uint AltStreamsSize = 76;
    public const uint VirtualSize = 77;
    public const uint UnpackSize = 78;
    public const uint TotalPhySize = 79;
    public const uint VolumeIndex = 80;
    public const uint SubType = 81;
    public const uint ShortComment = 82;
    public const uint CodePage = 83;
    public const uint IsNotArcType = 84;
    public const uint PhySizeCantBeDetected = 85;
    public const uint ZerosTailIsAllowed = 86;
    public const uint TailSize = 87;
    public const uint EmbeddedStubSize = 88;
    public const uint NtReparse = 89;
    public const uint HardLink = 90;
    public const uint INode = 91;
    public const uint StreamId = 92;
    public const uint ReadOnly = 93;
    public const uint OutName = 94;
    public const uint CopyLink = 95;
    public const uint ArcFileName = 96;
    public const uint IsHash = 97;
    public const uint ChangeTime = 98;
    public const uint UserId = 99;
    public const uint GroupId = 100;

    private static readonly Dictionary<uint, string> s_names = new()
    {
        [Path] = "Path", [Name] = "Name", [Extension] = "Extension", [IsDir] = "Folder", [Size] = "Size",
        [PackSize] = "Packed size", [Attrib] = "Attributes", [CTime] = "Created", [ATime] = "Accessed",
        [MTime] = "Modified", [Solid] = "Solid", [Commented] = "Commented", [Encrypted] = "Encrypted",
        [SplitBefore] = "Split before", [SplitAfter] = "Split after", [DictionarySize] = "Dictionary size",
        [Crc] = "CRC", [Type] = "Type", [IsAnti] = "Anti", [Method] = "Method", [HostOS] = "Host OS",
        [FileSystem] = "File system", [User] = "User", [Group] = "Group", [Block] = "Block",
        [Comment] = "Comment", [Position] = "Position", [Prefix] = "Path prefix", [NumSubDirs] = "Folders",
        [NumSubFiles] = "Files", [UnpackVer] = "Version", [Volume] = "Volume", [IsVolume] = "Multivolume",
        [Offset] = "Offset", [Links] = "Links", [NumBlocks] = "Blocks", [NumVolumes] = "Volumes",
        [Bit64] = "64-bit", [BigEndian] = "Big-endian", [Cpu] = "CPU", [PhySize] = "Physical size",
        [HeadersSize] = "Headers size", [Checksum] = "Checksum", [Characts] = "Characteristics",
        [Va] = "Virtual address", [Id] = "ID", [ShortName] = "Short name", [CreatorApp] = "Creator application",
        [SectorSize] = "Sector size", [PosixAttrib] = "Mode", [SymLink] = "Symbolic link", [Error] = "Error",
        [TotalSize] = "Total size", [FreeSpace] = "Free space", [ClusterSize] = "Cluster size",
        [VolumeName] = "Label", [LocalName] = "Local name", [Provider] = "Provider", [NtSecure] = "NT security",
        [IsAltStream] = "Alternate stream", [IsAux] = "Auxiliary", [IsDeleted] = "Deleted", [IsTree] = "Tree",
        [Sha1] = "SHA-1", [Sha256] = "SHA-256", [ErrorType] = "Error type", [NumErrors] = "Errors",
        [Warning] = "Warning", [NumStreams] = "Streams", [NumAltStreams] = "Alternate streams",
        [AltStreamsSize] = "Alternate streams size", [VirtualSize] = "Virtual size", [UnpackSize] = "Unpack size",
        [TotalPhySize] = "Total physical size", [VolumeIndex] = "Volume index", [SubType] = "Subtype",
        [ShortComment] = "Short comment", [CodePage] = "Code page", [TailSize] = "Tail size",
        [EmbeddedStubSize] = "Embedded stub size", [NtReparse] = "Reparse data", [HardLink] = "Hard link",
        [INode] = "iNode", [StreamId] = "Stream ID", [ReadOnly] = "Read-only", [OutName] = "Out name",
        [CopyLink] = "Copy link", [ChangeTime] = "Changed", [UserId] = "User ID", [GroupId] = "Group ID",
    };

    public static string GetDisplayName(uint propId, string? engineName = null)
        => s_names.TryGetValue(propId, out var n) ? n : engineName ?? $"Property {propId}";
}

/// <summary>Handler (format) property identifiers (NArchive::NHandlerPropID).</summary>
internal static class HandlerPropId
{
    public const uint Name = 0;
    public const uint ClassId = 1;
    public const uint Extension = 2;
    public const uint AddExtension = 3;
    public const uint Update = 4;
    public const uint KeepName = 5;
    public const uint Signature = 6;
    public const uint MultiSignature = 7;
    public const uint SignatureOffset = 8;
    public const uint AltStreams = 9;
    public const uint NtSecure = 10;
    public const uint Flags = 11;
    public const uint TimeFlags = 12;
}

/// <summary>NArcInfoFlags.</summary>
[Flags]
public enum ArchiveFormatFlags : uint
{
    None = 0,
    KeepName = 1 << 0,
    AltStreams = 1 << 1,
    NtSecure = 1 << 2,
    FindSignature = 1 << 3,
    MultiSignature = 1 << 4,
    UseGlobalOffset = 1 << 5,
    StartOpen = 1 << 6,
    PureStartOpen = 1 << 7,
    BackwardOpen = 1 << 8,
    PreArc = 1 << 9,
    SymLinks = 1 << 10,
    HardLinks = 1 << 11,
    ByExtOnlyOpen = 1 << 12,
    HashHandler = 1 << 13,
}

internal static class MethodPropId
{
    public const uint Id = 0;
    public const uint Name = 1;
    public const uint DigestSize = 9;
}

internal static class AskMode
{
    public const int Extract = 0;
    public const int Test = 1;
    public const int Skip = 2;
    public const int ReadExternal = 3;
}

/// <summary>NArchive::NExtract::NOperationResult.</summary>
public enum ExtractResultCode
{
    Ok = 0,
    UnsupportedMethod,
    DataError,
    CrcError,
    Unavailable,
    UnexpectedEnd,
    DataAfterEnd,
    IsNotArc,
    HeadersError,
    WrongPassword,
}
