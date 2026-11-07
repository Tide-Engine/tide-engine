using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using NUnit.Framework;
using Tide;

public unsafe class EntityPacketTests
{
    const ulong Birth = 0x12345678abcdef01ul, Lineage = 0xfedcba9876543210ul;
    static void Put(byte[] bytes, int at, uint value) => Buffer.BlockCopy(BitConverter.GetBytes(value), 0, bytes, at, 4);
    static void Put64(byte[] bytes, int at, ulong value) => Buffer.BlockCopy(BitConverter.GetBytes(value), 0, bytes, at, 8);

    [Test]
    public void EntityIdsKeepEveryBitAndOnlyBothZeroIsNull()
    {
        Assert.AreEqual(16, Marshal.SizeOf<TideEntityId>());
        Assert.AreEqual(8, Marshal.OffsetOf<TideEntityId>(nameof(TideEntityId.lineage)).ToInt32());
        var a = new TideEntityId { birth = Birth, lineage = Lineage };
        var b = new TideEntityId { birth = Birth ^ (1ul << 63), lineage = Lineage };
        var map = new Dictionary<TideEntityId, int> { [a] = 1, [b] = 2 };
        Assert.AreEqual(1, map[a]); Assert.AreEqual(2, map[b]);
        Assert.IsFalse(new TideEntityId { lineage = Lineage }.IsNull);
        Assert.IsTrue(default(TideEntityId).IsNull);
        Assert.AreEqual("Entity(12345678abcdef01:fedcba9876543210)", a.ToString());
    }

    [Test]
    public void PacketFiveReadsFullEntityAndEventTargetsAndRejectsTruncation()
    {
        var bytes = new byte[144]; // header48, entity32, section24, heard40
        Put(bytes, 0, TidePacket.Magic); Put(bytes, 4, TidePacket.Version);
        Put(bytes, 16, (uint)bytes.Length); Put(bytes, 20, 1); Put(bytes, 36, 1); Put(bytes, 40, 1);
        Put64(bytes, 48, Birth); Put64(bytes, 56, Lineage); Put(bytes, 64, 3);
        Put(bytes, 68, uint.MaxValue); Put(bytes, 72, uint.MaxValue);
        Put(bytes, 80, (uint)TideSectionKind.Events); Put(bytes, 88, 1); Put(bytes, 96, 40);
        Put(bytes, 104, 40); Put(bytes, 108, 7); Put(bytes, 112, 1); Put(bytes, 116, 12);
        Put64(bytes, 120, Birth); Put64(bytes, 128, Lineage); Put(bytes, 136, 0);
        var packet = new TidePacket();
        fixed (byte* data = bytes)
        {
            for (uint cut = 0; cut < bytes.Length; cut++) Assert.IsFalse(packet.Read(data, cut));
            Assert.IsTrue(packet.Read(data, (uint)bytes.Length));
            Assert.AreEqual(Birth, packet.Entity(0).Birth); Assert.AreEqual(Lineage, packet.Entity(0).Lineage);
            Assert.AreEqual(new TideEntityId { birth = Birth, lineage = Lineage }, packet.Entity(0).Id);
            Assert.AreEqual(Birth, packet.Heard(0).TargetBirth); Assert.AreEqual(Lineage, packet.Heard(0).TargetLineage);
            Assert.IsTrue(packet.Heard(0).HasTarget);
        }
    }
}
