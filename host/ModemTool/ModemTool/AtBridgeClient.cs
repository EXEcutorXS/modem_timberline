using System.IO;
using System.Net.WebSockets;

namespace ModemTool;

/// <summary>
/// C# reimplementation of host/at-bridge-relay.py's "relay" role: a raw byte
/// pipe between the modem's own USB CDC console and timberline-web's
/// /at-bridge WebSocket endpoint (see server.js), so someone elsewhere can
/// drive the modem's AT console — after typing "b" there themselves to enter
/// bridge mode, see hw_config.c's bridgeMode — as if plugged in locally, over
/// this computer's own internet connection rather than the modem's cellular
/// link (which may be exactly what's being debugged).
///
/// This class only owns the WebSocket half; MainWindow wires it to
/// SerialLink's raw byte events for the COM port half. Auto-reconnects every
/// 5 seconds on drop, same as the Python script's ws.run_forever(reconnect=5)
/// — only Stop() ends it for good.
/// </summary>
public sealed class AtBridgeClient : IDisposable
{
    private const string ServerUrl = "wss://multihot.duckdns.org/at-bridge";
    private static readonly TimeSpan ReconnectDelay = TimeSpan.FromSeconds(5);

    private CancellationTokenSource? _cts;
    private ClientWebSocket? _socket;

    public event Action<string>? StatusChanged;
    public event Action<byte[]>? DataReceived;

    public bool IsRunning => _cts != null;

    public void Start(string username, string password)
    {
        Stop();
        var cts = new CancellationTokenSource();
        _cts = cts;
        _ = RunLoop(username, password, cts.Token);
    }

    public void Stop()
    {
        var cts = _cts;
        _cts = null;
        if (cts == null) return;
        cts.Cancel();
        try { _socket?.Abort(); } catch { /* best effort */ }
        cts.Dispose();
    }

    /// <summary>Forward one chunk of bytes just read from the serial port to the server.</summary>
    public async void SendFromSerial(byte[] data)
    {
        var socket = _socket;
        if (socket is not { State: WebSocketState.Open }) return; // not connected right now — drop, matches the Python relay
        try { await socket.SendAsync(data, WebSocketMessageType.Binary, true, CancellationToken.None); }
        catch { /* connection likely just dropped — the run loop below will notice and reconnect */ }
    }

    private async Task RunLoop(string username, string password, CancellationToken ct)
    {
        var url = $"{ServerUrl}?role=relay&username={Uri.EscapeDataString(username)}&password={Uri.EscapeDataString(password)}";
        var recvBuffer = new byte[4096];

        while (!ct.IsCancellationRequested)
        {
            var socket = new ClientWebSocket();
            try
            {
                StatusChanged?.Invoke("Connecting...");
                await socket.ConnectAsync(new Uri(url), ct);
                _socket = socket;
                StatusChanged?.Invoke("Connected — relaying");

                using var ms = new MemoryStream();
                while (!ct.IsCancellationRequested)
                {
                    ms.SetLength(0);
                    WebSocketReceiveResult result;
                    do
                    {
                        result = await socket.ReceiveAsync(recvBuffer, ct);
                        if (result.MessageType == WebSocketMessageType.Close) goto closed;
                        ms.Write(recvBuffer, 0, result.Count);
                    } while (!result.EndOfMessage);

                    if (ms.Length > 0) DataReceived?.Invoke(ms.ToArray());
                }

                closed:
                StatusChanged?.Invoke($"Disconnected ({(int?)socket.CloseStatus}: {socket.CloseStatusDescription})");
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                StatusChanged?.Invoke($"Connection error: {ex.Message}");
            }
            finally
            {
                _socket = null;
                socket.Dispose();
            }

            if (ct.IsCancellationRequested) break;
            StatusChanged?.Invoke("Reconnecting in 5s...");
            try { await Task.Delay(ReconnectDelay, ct); }
            catch (OperationCanceledException) { break; }
        }

        StatusChanged?.Invoke("Stopped");
    }

    public void Dispose() => Stop();
}
