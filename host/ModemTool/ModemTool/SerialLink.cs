using System.IO;
using System.IO.Ports;
using System.Text;

namespace ModemTool;

/// <summary>
/// Wrapper around the modem's USB CDC console (see Library/Usb/src/hw_config.c's
/// usb_process_line() in the firmware repo). Exposes the byte stream two ways
/// at once from the same open port:
///   - <see cref="LineReceived"/>: reassembled into \r/\n-terminated lines, for
///     console commands and the "status"/"config" dumps (framed as
///     "[STATUS]"/"[CONFIG]" ... "[END]" blocks of "key=value" lines).
///   - <see cref="RawDataReceived"/>: the exact bytes as they arrived, with no
///     buffering or reframing — needed for the AT-bridge relay (see
///     AtBridgeClient), which must forward bytes to/from the modem's "bridge
///     mode" passthrough (hw_config.c's bridgeMode) byte-for-byte and without
///     waiting for a line terminator that may never come (e.g. a bare "AT"
///     prompt echoed character by character).
/// </summary>
public sealed class SerialLink : IDisposable
{
    private SerialPort? _port;
    private readonly StringBuilder _rxBuf = new();

    public event Action<string>? LineReceived;
    public event Action<byte[]>? RawDataReceived;

    public bool IsOpen => _port?.IsOpen == true;

    public void Open(string portName)
    {
        Close();
        var port = new SerialPort(portName, 115200, Parity.None, 8, StopBits.One)
        {
            // The modem is a USB CDC virtual COM port — the actual bit rate
            // is irrelevant (ignored by the device), but SerialPort still
            // requires a value.
            ReadTimeout = SerialPort.InfiniteTimeout,
            WriteTimeout = 2000,
        };
        port.DataReceived += OnDataReceived;
        port.Open();
        _port = port;
    }

    public void Close()
    {
        if (_port == null) return;
        var port = _port;
        _port = null;
        try
        {
            port.DataReceived -= OnDataReceived;
            if (port.IsOpen) port.Close();
        }
        catch (IOException) { /* device already gone — nothing left to clean up */ }
        finally { port.Dispose(); }
    }

    /// <summary>Send one console command line (CRLF appended, as usb_process_line() expects).</summary>
    public void Send(string line)
    {
        var port = _port;
        if (port is not { IsOpen: true }) return;
        try { port.Write(line + "\r\n"); }
        catch (IOException) { /* transient — port likely just unplugged */ }
        catch (TimeoutException) { }
    }

    /// <summary>Write raw bytes with no framing added — for AtBridgeClient's WebSocket-to-serial half.</summary>
    public void SendRaw(byte[] data)
    {
        var port = _port;
        if (port is not { IsOpen: true }) return;
        try { port.Write(data, 0, data.Length); }
        catch (IOException) { }
        catch (TimeoutException) { }
    }

    private void OnDataReceived(object sender, SerialDataReceivedEventArgs e)
    {
        var port = _port;
        if (port == null) return;

        var toRead = port.BytesToRead;
        if (toRead <= 0) return;
        var buf = new byte[toRead];
        int got;
        try { got = port.Read(buf, 0, toRead); }
        catch (IOException) { return; }
        if (got <= 0) return;
        if (got != buf.Length) Array.Resize(ref buf, got);

        RawDataReceived?.Invoke(buf);

        foreach (var b in buf)
        {
            var c = (char)b; // byte value == code point (Latin-1) — lossless for line reassembly
            if (c == '\r') continue;
            if (c == '\n')
            {
                var line = _rxBuf.ToString();
                _rxBuf.Clear();
                LineReceived?.Invoke(line);
            }
            else
            {
                _rxBuf.Append(c);
            }
        }
    }

    public void Dispose() => Close();
}
