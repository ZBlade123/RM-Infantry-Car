using System.Buffers.Binary;
using System.Text;

namespace RMRemote;

internal sealed record RmFrame(byte Sequence, byte Command, byte[] Payload);

internal sealed class RmProtocol
{
    public const byte Vel = 0x01;
    public const byte Servo = 0x02;
    public const byte Ping = 0x04;
    public const byte Telem = 0x07;
    public const byte Stop = 0x08;
    public const byte RspTelem = 0x81;
    public const byte RspPong = 0x82;

    private readonly List<byte> buffer = [];
    private readonly List<byte> textBuffer = [];
    private byte sequence;

    public event Action<string>? TextReceived;

    public byte[] Build(byte command, ReadOnlySpan<byte> payload)
    {
        if (payload.Length > 32) throw new ArgumentOutOfRangeException(nameof(payload));
        var frame = new byte[payload.Length + 6];
        var seq = sequence++;
        frame[0] = 0xA5;
        frame[1] = 0x5A;
        frame[2] = seq;
        frame[3] = command;
        frame[4] = (byte)payload.Length;
        payload.CopyTo(frame.AsSpan(5));
        frame[^1] = Crc8(frame.AsSpan(2, payload.Length + 3));
        return frame;
    }

    public byte[] BuildVelocity(short vx, short vy, short wz)
    {
        Span<byte> payload = stackalloc byte[6];
        BinaryPrimitives.WriteInt16LittleEndian(payload, vx);
        BinaryPrimitives.WriteInt16LittleEndian(payload[2..], vy);
        BinaryPrimitives.WriteInt16LittleEndian(payload[4..], wz);
        return Build(Vel, payload);
    }

    public byte[] BuildServo(short yaw10, short pitch10)
    {
        Span<byte> payload = stackalloc byte[4];
        BinaryPrimitives.WriteInt16LittleEndian(payload, yaw10);
        BinaryPrimitives.WriteInt16LittleEndian(payload[2..], pitch10);
        return Build(Servo, payload);
    }

    public byte[] BuildToggle(byte command, bool enabled) => Build(command, [enabled ? (byte)1 : (byte)0]);

    public IReadOnlyList<RmFrame> Feed(ReadOnlySpan<byte> input)
    {
        buffer.AddRange(input.ToArray());
        var frames = new List<RmFrame>();
        while (true)
        {
            var start = buffer.FindIndex(b => b == 0xA5);
            if (start < 0)
            {
                textBuffer.AddRange(buffer);
                buffer.Clear();
                DrainText();
                break;
            }
            if (start > 0)
            {
                textBuffer.AddRange(buffer.Take(start));
                buffer.RemoveRange(0, start);
                DrainText();
            }
            if (buffer.Count < 2) break;
            if (buffer[1] != 0x5A)
            {
                buffer.RemoveAt(0);
                continue;
            }
            if (buffer.Count < 5) break;
            var length = buffer[4];
            if (length > 32)
            {
                buffer.RemoveAt(0);
                continue;
            }
            var total = length + 6;
            if (buffer.Count < total) break;
            var candidate = buffer.GetRange(0, total).ToArray();
            if (candidate[^1] == Crc8(candidate.AsSpan(2, length + 3)))
            {
                frames.Add(new RmFrame(candidate[2], candidate[3], candidate[5..^1]));
                buffer.RemoveRange(0, total);
            }
            else
            {
                buffer.RemoveAt(0);
            }
        }
        return frames;
    }

    private void DrainText()
    {
        while (true)
        {
            var newline = textBuffer.IndexOf((byte)'\n');
            if (newline < 0) break;
            var line = Encoding.ASCII.GetString(textBuffer.Take(newline).ToArray()).Trim();
            textBuffer.RemoveRange(0, newline + 1);
            if (line.Length > 0) TextReceived?.Invoke(line);
        }
        if (textBuffer.Count > 512) textBuffer.RemoveRange(0, textBuffer.Count - 512);
    }

    public static byte Crc8(ReadOnlySpan<byte> data)
    {
        byte crc = 0;
        foreach (var value in data)
        {
            crc ^= value;
            for (var bit = 0; bit < 8; bit++)
                crc = (byte)((crc & 0x80) != 0 ? (crc << 1) ^ 0x07 : crc << 1);
        }
        return crc;
    }
}

internal sealed record RmTelemetry(ushort BatteryMv, short Vx, short Vy, short Wz,
    short Pitch10, short Yaw10, byte FireState, byte Flags, uint UptimeMs)
{
    public static RmTelemetry? Parse(byte[] payload)
    {
        if (payload.Length < 18) return null;
        return new RmTelemetry(
            BinaryPrimitives.ReadUInt16LittleEndian(payload),
            BinaryPrimitives.ReadInt16LittleEndian(payload.AsSpan(2)),
            BinaryPrimitives.ReadInt16LittleEndian(payload.AsSpan(4)),
            BinaryPrimitives.ReadInt16LittleEndian(payload.AsSpan(6)),
            BinaryPrimitives.ReadInt16LittleEndian(payload.AsSpan(8)),
            BinaryPrimitives.ReadInt16LittleEndian(payload.AsSpan(16)),
            payload[10], payload[11],
            BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(12)));
    }
}
