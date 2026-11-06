using System.Buffers.Binary;
using RMRemote;

static void Check(string name, bool condition)
{
    if (!condition) throw new InvalidOperationException("FAIL: " + name);
    Console.WriteLine("PASS: " + name);
}

var protocol = new RmProtocol();
var velocity = protocol.BuildVelocity(300, -200, 0);
Check("VEL frame header and fields", velocity[0] == 0xA5 && velocity[1] == 0x5A
      && velocity[3] == RmProtocol.Vel && velocity[4] == 6
      && BinaryPrimitives.ReadInt16LittleEndian(velocity.AsSpan(5)) == 300
      && BinaryPrimitives.ReadInt16LittleEndian(velocity.AsSpan(7)) == -200);
Check("CRC covers sequence through payload", RmProtocol.Crc8(velocity.AsSpan(2, velocity.Length - 3)) == velocity[^1]);

var stream = new RmProtocol();
var ping = stream.Build(RmProtocol.RspPong, [3, 8, 0, 1, 2, 3, 4]);
var fragmented = stream.Feed([0x11, .. velocity[..4]]);
Check("partial frame waits for rest", fragmented.Count == 0);
var parsed = stream.Feed([.. velocity[4..], .. ping]);
Check("fragmented and adjacent frames parse", parsed.Count == 2
      && parsed[0].Command == RmProtocol.Vel && parsed[1].Command == RmProtocol.RspPong);

var bad = (byte[])ping.Clone();
bad[^1] ^= 0xFF;
Check("bad CRC frame is dropped", new RmProtocol().Feed(bad).Count == 0);

var mixed = new RmProtocol();
var lines = new List<string>();
mixed.TextReceived += lines.Add;
var textThenFrame = System.Text.Encoding.ASCII.GetBytes("ready\r\n").Concat(ping).ToArray();
var mixedFrames = mixed.Feed(textThenFrame);
Check("text response and binary frame share stream", lines.SequenceEqual(["ready"])
      && mixedFrames.Count == 1 && mixedFrames[0].Command == RmProtocol.RspPong);

var telemPayload = new byte[20];
BinaryPrimitives.WriteUInt16LittleEndian(telemPayload, 0);
BinaryPrimitives.WriteInt16LittleEndian(telemPayload.AsSpan(2), 100);
BinaryPrimitives.WriteInt16LittleEndian(telemPayload.AsSpan(4), -50);
BinaryPrimitives.WriteInt16LittleEndian(telemPayload.AsSpan(6), 10);
BinaryPrimitives.WriteInt16LittleEndian(telemPayload.AsSpan(8), 900);
BinaryPrimitives.WriteUInt32LittleEndian(telemPayload.AsSpan(12), 1234);
BinaryPrimitives.WriteInt16LittleEndian(telemPayload.AsSpan(16), 910);
var telem = RmTelemetry.Parse(telemPayload);
Check("telemetry fields decode", telem?.Vx == 100 && telem.Vy == -50
      && telem.Wz == 10 && telem.Pitch10 == 900 && telem.Yaw10 == 910
      && telem.UptimeMs == 1234 && telem.BatteryMv == 0);

Console.WriteLine("All protocol tests passed.");
