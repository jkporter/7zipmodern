namespace SevenZip.Core.Compression;

/// <summary>
/// Seekable write stream that spreads data across fixed-size volume files (name.001, name.002…).
/// The 7z writer seeks back to patch the start header, so random access across volumes is required.
/// </summary>
internal sealed class MultiVolumeStream : Stream
{
    private readonly string _basePath;
    private readonly long _volumeSize;
    private readonly List<FileStream> _volumes = [];
    private long _position;
    private long _length;

    public MultiVolumeStream(string basePath, ulong volumeSize)
    {
        if (volumeSize < 1024)
            throw new ArgumentOutOfRangeException(nameof(volumeSize), "Volumes must be at least 1 KB.");
        _basePath = basePath;
        _volumeSize = (long)volumeSize;
    }

    public IReadOnlyList<string> VolumePaths => _volumes.Select(v => v.Name).ToArray();

    public static string GetVolumePath(string basePath, int index) => $"{basePath}.{index + 1:D3}";

    private FileStream GetVolume(int index)
    {
        while (_volumes.Count <= index)
        {
            var path = GetVolumePath(_basePath, _volumes.Count);
            _volumes.Add(new FileStream(path, FileMode.Create, FileAccess.ReadWrite, FileShare.Read, 1 << 16));
        }
        return _volumes[index];
    }

    public override bool CanRead => false;
    public override bool CanSeek => true;
    public override bool CanWrite => true;
    public override long Length => _length;

    public override long Position
    {
        get => _position;
        set => _position = value >= 0 ? value : throw new IOException("Negative seek.");
    }

    public override void Write(byte[] buffer, int offset, int count) => Write(buffer.AsSpan(offset, count));

    public override void Write(ReadOnlySpan<byte> buffer)
    {
        while (buffer.Length > 0)
        {
            int index = (int)(_position / _volumeSize);
            long offset = _position % _volumeSize;
            int n = (int)Math.Min(buffer.Length, _volumeSize - offset);
            var fs = GetVolume(index);
            if (fs.Position != offset)
                fs.Position = offset;
            fs.Write(buffer[..n]);
            buffer = buffer[n..];
            _position += n;
            if (_position > _length)
                _length = _position;
        }
    }

    public override long Seek(long offset, SeekOrigin origin)
    {
        Position = origin switch
        {
            SeekOrigin.Begin => offset,
            SeekOrigin.Current => _position + offset,
            _ => _length + offset,
        };
        return _position;
    }

    public override void SetLength(long value)
    {
        int lastIndex = value == 0 ? 0 : (int)((value - 1) / _volumeSize);
        for (int i = _volumes.Count - 1; i > lastIndex; i--)
        {
            var name = _volumes[i].Name;
            _volumes[i].Dispose();
            File.Delete(name);
            _volumes.RemoveAt(i);
        }
        GetVolume(lastIndex).SetLength(value - (long)lastIndex * _volumeSize);
        _length = value;
        if (_position > _length)
            _position = _length;
    }

    public override void Flush()
    {
        foreach (var v in _volumes)
            v.Flush();
    }

    public override int Read(byte[] buffer, int offset, int count) => throw new NotSupportedException();

    public void DeleteAll()
    {
        foreach (var v in _volumes)
        {
            var name = v.Name;
            v.Dispose();
            try { File.Delete(name); } catch (IOException) { }
        }
        _volumes.Clear();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
            foreach (var v in _volumes)
                v.Dispose();
        base.Dispose(disposing);
    }
}
