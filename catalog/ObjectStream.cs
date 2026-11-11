// Lumberyard AZ::ObjectStream binary reader/writer (version 3, as in shipped mission0.entities_xml / .dynamicslice).
// Element: flags byte (0 = end of the parent's children), [name crc u32 BE], [version u8], type uuid (16 bytes),
// [value: size in the low 3 flag bits, or in a 1/2/4-byte size field when 0x20 is set], then children up to a 0 byte.
using System; using System.Collections.Generic; using System.IO; using System.Text;

public class OsElement {
    public uint NameCrc; public bool HasName; public int Version = -1; public Guid Type; public byte[] Value;
    public List<OsElement> Children = new List<OsElement>();
    public long ValueOffset = -1;   // where Value starts in the source buffer (for in-place patching)
}

public static class ObjectStream {
    const int HEADER = 0x08, HAS_VALUE = 0x10, EXTRA_SIZE = 0x20, HAS_NAME = 0x40, HAS_VERSION = 0x80;

    public static Guid ReadUuid(byte[] b, int p) {
        // AZ::Uuid bytes are in string order; System.Guid wants the first three groups little-endian
        byte[] g = new byte[16];
        g[0] = b[p + 3]; g[1] = b[p + 2]; g[2] = b[p + 1]; g[3] = b[p];
        g[4] = b[p + 5]; g[5] = b[p + 4]; g[6] = b[p + 7]; g[7] = b[p + 6];
        Array.Copy(b, p + 8, g, 8, 8);
        return new Guid(g);
    }

    public static void WriteUuid(Stream s, Guid id) {
        byte[] g = id.ToByteArray();
        s.WriteByte(g[3]); s.WriteByte(g[2]); s.WriteByte(g[1]); s.WriteByte(g[0]);
        s.WriteByte(g[5]); s.WriteByte(g[4]); s.WriteByte(g[7]); s.WriteByte(g[6]);
        s.Write(g, 8, 8);
    }

    public static List<OsElement> Parse(byte[] b, out uint version) {
        if (b[0] != 0) throw new Exception("not a binary object stream");
        version = (uint)(b[1] << 24 | b[2] << 16 | b[3] << 8 | b[4]);
        int p = 5;
        var roots = new List<OsElement>();
        while (p < b.Length && b[p] != 0) roots.Add(ReadElement(b, ref p));
        return roots;
    }

    // one element (with its children) starting at p; p ends after it
    public static OsElement ParseElement(byte[] b, ref int p) { return ReadElement(b, ref p); }

    // the element's bytes as they would appear inside a stream
    public static byte[] ElementBytes(OsElement e) { var s = new MemoryStream(); WriteElement(s, e); return s.ToArray(); }

    // offset just after the element that starts at p, without building anything
    public static int Skip(byte[] b, int p) {
        p = ChildrenStart(b, p);
        while (b[p] != 0) p = Skip(b, p);
        return p + 1;
    }

    // offset of the first child (or of the closing 0) of the element that starts at p
    public static int ChildrenStart(byte[] b, int p) {
        int flags = b[p++];
        if ((flags & HEADER) == 0) throw new Exception("bad element flags at " + (p - 1));
        if ((flags & HAS_NAME) != 0) p += 4;
        if ((flags & HAS_VERSION) != 0) p++;
        p += 16;
        if ((flags & HAS_VALUE) != 0) {
            int size = flags & 7;
            if ((flags & EXTRA_SIZE) != 0) { int n = size; size = 0; for (int i = 0; i < n; i++) size = size << 8 | b[p++]; }
            p += size;
        }
        return p;
    }

    static OsElement ReadElement(byte[] b, ref int p) {
        int flags = b[p++];
        if ((flags & HEADER) == 0) throw new Exception("bad element flags at " + (p - 1));
        var e = new OsElement();
        if ((flags & HAS_NAME) != 0) { e.HasName = true; e.NameCrc = (uint)(b[p] << 24 | b[p + 1] << 16 | b[p + 2] << 8 | b[p + 3]); p += 4; }
        if ((flags & HAS_VERSION) != 0) e.Version = b[p++];
        e.Type = ReadUuid(b, p); p += 16;
        if ((flags & HAS_VALUE) != 0) {
            int size = flags & 7;
            if ((flags & EXTRA_SIZE) != 0) {
                int n = size; size = 0;
                for (int i = 0; i < n; i++) size = size << 8 | b[p++];
            }
            e.ValueOffset = p;
            e.Value = new byte[size]; Array.Copy(b, p, e.Value, 0, size); p += size;
        }
        while (b[p] != 0) e.Children.Add(ReadElement(b, ref p));
        p++;
        return e;
    }

    public static byte[] Write(List<OsElement> roots, uint version) {
        var s = new MemoryStream();
        s.WriteByte(0); s.WriteByte((byte)(version >> 24)); s.WriteByte((byte)(version >> 16)); s.WriteByte((byte)(version >> 8)); s.WriteByte((byte)version);
        foreach (var e in roots) WriteElement(s, e);
        s.WriteByte(0);
        return s.ToArray();
    }

    static void WriteElement(Stream s, OsElement e) {
        int flags = HEADER;
        if (e.HasName) flags |= HAS_NAME;
        if (e.Version >= 0) flags |= HAS_VERSION;
        byte[] sizeField = null;
        if (e.Value != null) {
            flags |= HAS_VALUE;
            int n = e.Value.Length;
            if (n < 8) flags |= n;
            else {
                flags |= EXTRA_SIZE;
                sizeField = n < 0x100 ? new[] { (byte)n } : n < 0x10000 ? new[] { (byte)(n >> 8), (byte)n }
                          : new[] { (byte)(n >> 24), (byte)(n >> 16), (byte)(n >> 8), (byte)n };
                flags |= sizeField.Length;
            }
        }
        s.WriteByte((byte)flags);
        if (e.HasName) { s.WriteByte((byte)(e.NameCrc >> 24)); s.WriteByte((byte)(e.NameCrc >> 16)); s.WriteByte((byte)(e.NameCrc >> 8)); s.WriteByte((byte)e.NameCrc); }
        if (e.Version >= 0) s.WriteByte((byte)e.Version);
        WriteUuid(s, e.Type);
        if (e.Value != null) { if (sizeField != null) s.Write(sizeField, 0, sizeField.Length); s.Write(e.Value, 0, e.Value.Length); }
        foreach (var c in e.Children) WriteElement(s, c);
        s.WriteByte(0);
    }

    // AZ::Crc32 of a field name (lower-cased), as the serializer stores names
    public static uint Crc(string name) {
        uint crc = 0xFFFFFFFF;
        foreach (byte ch in Encoding.ASCII.GetBytes(name.ToLowerInvariant())) {
            crc ^= ch;
            for (int k = 0; k < 8; k++) crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
        }
        return ~crc;
    }

    public static float BeFloat(byte[] v, int o) {
        byte[] t = { v[o + 3], v[o + 2], v[o + 1], v[o] };
        return BitConverter.ToSingle(t, 0);
    }

    public static IEnumerable<OsElement> Walk(OsElement e) {
        yield return e;
        foreach (var c in e.Children) foreach (var d in Walk(c)) yield return d;
    }
}
