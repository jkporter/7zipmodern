using System.Runtime.InteropServices.Marshalling;

namespace SevenZip.Core.Interop;

/// <summary>Exposes a seekable .NET stream to the engine as IInStream.</summary>
[GeneratedComClass]
internal sealed unsafe partial class InStreamWrapper : IInStream, IStreamGetSize, IDisposable
{
    private readonly Stream _stream;
    private readonly bool _ownsStream;

    public InStreamWrapper(Stream stream, bool ownsStream = true)
    {
        _stream = stream;
        _ownsStream = ownsStream;
    }

    public Stream BaseStream => _stream;

    public int Read(byte* data, uint size, uint* processedSize)
    {
        try
        {
            int read = size == 0 ? 0 : _stream.Read(new Span<byte>(data, (int)Math.Min(size, int.MaxValue)));
            if (processedSize != null)
                *processedSize = (uint)read;
            return HResults.S_OK;
        }
        catch (Exception ex)
        {
            if (processedSize != null)
                *processedSize = 0;
            return HResults.FromException(ex);
        }
    }

    public int Seek(long offset, uint seekOrigin, ulong* newPosition)
    {
        try
        {
            if (seekOrigin > 2)
                return HResults.STG_E_INVALIDFUNCTION;
            long target = seekOrigin switch
            {
                0 => offset,
                1 => _stream.Position + offset,
                _ => _stream.Length + offset,
            };
            if (target < 0)
                return HResults.ERROR_NEGATIVE_SEEK;
            _stream.Position = target;
            if (newPosition != null)
                *newPosition = (ulong)target;
            return HResults.S_OK;
        }
        catch (Exception ex)
        {
            return HResults.FromException(ex);
        }
    }

    public int GetSize(ulong* size)
    {
        try
        {
            *size = (ulong)_stream.Length;
            return HResults.S_OK;
        }
        catch (Exception ex)
        {
            return HResults.FromException(ex);
        }
    }

    public void Dispose()
    {
        if (_ownsStream)
            _stream.Dispose();
    }
}

/// <summary>Exposes a .NET stream to the engine as IOutStream. Seeking/SetSize are used by 7z/zip writers.</summary>
[GeneratedComClass]
internal sealed unsafe partial class OutStreamWrapper : IOutStream, IDisposable
{
    private readonly Stream _stream;
    private readonly bool _ownsStream;
    private readonly Action<long>? _onWrite;

    public OutStreamWrapper(Stream stream, bool ownsStream = true, Action<long>? onWrite = null)
    {
        _stream = stream;
        _ownsStream = ownsStream;
        _onWrite = onWrite;
    }

    public Stream BaseStream => _stream;
    public long BytesWritten { get; private set; }

    public int Write(byte* data, uint size, uint* processedSize)
    {
        try
        {
            if (size != 0)
                _stream.Write(new ReadOnlySpan<byte>(data, (int)Math.Min(size, int.MaxValue)));
            if (processedSize != null)
                *processedSize = size;
            BytesWritten += size;
            _onWrite?.Invoke(size);
            return HResults.S_OK;
        }
        catch (Exception ex)
        {
            if (processedSize != null)
                *processedSize = 0;
            return HResults.FromException(ex);
        }
    }

    public int Seek(long offset, uint seekOrigin, ulong* newPosition)
    {
        try
        {
            if (!_stream.CanSeek)
                return HResults.STG_E_INVALIDFUNCTION;
            long target = seekOrigin switch
            {
                0 => offset,
                1 => _stream.Position + offset,
                2 => _stream.Length + offset,
                _ => -1,
            };
            if (target < 0)
                return HResults.ERROR_NEGATIVE_SEEK;
            _stream.Position = target;
            if (newPosition != null)
                *newPosition = (ulong)target;
            return HResults.S_OK;
        }
        catch (Exception ex)
        {
            return HResults.FromException(ex);
        }
    }

    public int SetSize(ulong newSize)
    {
        try
        {
            _stream.SetLength((long)newSize);
            return HResults.S_OK;
        }
        catch (Exception ex)
        {
            return HResults.FromException(ex);
        }
    }

    public void Dispose()
    {
        if (_ownsStream)
            _stream.Dispose();
    }
}

/// <summary>Write-only sink used when testing/benchmarking (data is discarded, optionally observed).</summary>
[GeneratedComClass]
internal sealed unsafe partial class SinkOutStream : ISequentialOutStream
{
    private readonly Action<ReadOnlySpan<byte>>? _observer;

    public SinkOutStream(Action<ReadOnlySpan<byte>>? observer = null) => _observer = observer;

    public long BytesWritten { get; private set; }

    public int Write(byte* data, uint size, uint* processedSize)
    {
        try
        {
            _observer?.Invoke(new ReadOnlySpan<byte>(data, (int)size));
            BytesWritten += size;
            if (processedSize != null)
                *processedSize = size;
            return HResults.S_OK;
        }
        catch (Exception ex)
        {
            return HResults.FromException(ex);
        }
    }
}
