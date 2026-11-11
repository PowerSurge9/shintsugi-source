// Adds files to a Lumberyard asset catalog (assetcatalog.xml = AzFramework::AssetRegistry in a binary ObjectStream),
// the same index the developers' Asset Processor wrote. The game finds a slice or model by looking its path up in
// this catalog; a file the catalog doesn't list is not found even when a pak holds it.
// Registry layout (root, version 5), each table a list of key/value pairs:
//   [0] asset id -> AssetInfo {asset id, relative path, size in bytes, asset type}
//   [1] Uuid::CreateName(path) -> asset id
//   [2] legacy asset id -> asset id          [3] asset id -> product dependencies
// A new file gets the asset id {CreateName(path), 0}, the id the engine itself gives a path it registers at run time.
// Only tables 0 and 1 get entries; the other two are copied unchanged.
using System; using System.Collections.Generic; using System.IO; using System.Security.Cryptography; using System.Text;

public class CatalogFile {
    public string Path; public long Size; public Guid Type;
}

public static class AssetCatalog {
    public static readonly Guid DynamicSliceType = new Guid("78802ABF-9595-463A-8D2B-D022F906F9B1");
    public static readonly Guid MeshType = new Guid("C2869E3B-DDA0-4E01-8FE3-6770D788866B");

    const uint PairCrc = 0x41405e39, KeyCrc = 0xa2756c5a, ValueCrc = 0x3b7c3de0;
    const uint InfoIdCrc = 0x2289cf4d, PathCrc = 0x826946c9, SizeCrc = 0x8719f571, TypeCrc = 0xabbf8d5f;
    const uint GuidCrc = 0x2b6fcfb2, SubIdCrc = 0x9102574d;

    // AZ::Uuid::CreateName: SHA-1 name UUID (version 5) of the text
    public static Guid NameUuid(string name) {
        byte[] h = SHA1.Create().ComputeHash(Encoding.ASCII.GetBytes(name));
        h[6] = (byte)((h[6] & 0x0F) | 0x50); h[8] = (byte)((h[8] & 0x3F) | 0x80);
        byte[] raw = new byte[16]; Array.Copy(h, raw, 16);
        return ObjectStream.ReadUuid(raw, 0);
    }

    // asset type for a path, by extension; null = a kind of file we don't register
    public static Guid? TypeOf(string path) {
        string p = path.ToLowerInvariant();
        if (p.EndsWith(".dynamicslice")) return DynamicSliceType;
        if (p.EndsWith(".cgf")) return MeshType;
        return null;
    }

    static byte[] UuidBytes(Guid g) { var s = new MemoryStream(); ObjectStream.WriteUuid(s, g); return s.ToArray(); }
    static byte[] Be(ulong v, int n) { var r = new byte[n]; for (int i = n - 1; i >= 0; i--) { r[i] = (byte)v; v >>= 8; } return r; }

    static OsElement Child(OsElement e, uint crc) {
        foreach (var c in e.Children) if (c.HasName && c.NameCrc == crc) return c;
        throw new Exception(string.Format("catalog entry has no field {0:x8}", crc));
    }

    static void SetAssetId(OsElement id, Guid g, uint subId) {
        Child(id, GuidCrc).Value = UuidBytes(g);
        Child(id, SubIdCrc).Value = Be(subId, 4);
    }

    struct Table { public int Start, ChildrenStart, End; public List<int> Children; }

    // paths (lower case) that the catalog already lists
    public static HashSet<string> Paths(byte[] xml) {
        var set = new HashSet<string>();
        var t = Tables(xml);
        foreach (int c in t[0].Children) {
            int p = c; var pair = ObjectStream.ParseElement(xml, ref p);
            set.Add(Encoding.ASCII.GetString(Child(Child(pair, ValueCrc), PathCrc).Value));
        }
        return set;
    }

    static Table[] Tables(byte[] xml) {
        if (xml[0] != 0) throw new Exception("not a binary object stream");
        int root = 5;
        int p = ObjectStream.ChildrenStart(xml, root);
        var tables = new List<Table>();
        while (xml[p] != 0) {
            var t = new Table { Start = p, ChildrenStart = ObjectStream.ChildrenStart(xml, p), Children = new List<int>() };
            int q = t.ChildrenStart;
            if (tables.Count < 2) while (xml[q] != 0) { t.Children.Add(q); q = ObjectStream.Skip(xml, q); }
            else q = ObjectStream.Skip(xml, p) - 1;
            t.End = q;   // the closing 0 of the table
            tables.Add(t);
            p = q + 1;
        }
        if (tables.Count != 4) throw new Exception("unexpected catalog layout: " + tables.Count + " tables");
        return tables.ToArray();
    }

    // the catalog with the files added (files it already lists are left as they are); added = how many were new
    public static byte[] Add(byte[] xml, IList<CatalogFile> files, out int added) {
        var t = Tables(xml);
        if (t[0].Children.Count == 0 || t[1].Children.Count == 0) throw new Exception("catalog tables are empty");
        var known = Paths(xml);
        byte[] infoTemplate = Slice(xml, t[0].Children[0], ObjectStream.Skip(xml, t[0].Children[0]));
        byte[] pathTemplate = Slice(xml, t[1].Children[0], ObjectStream.Skip(xml, t[1].Children[0]));
        var info = new MemoryStream(); var paths = new MemoryStream();
        var seen = new HashSet<string>(); added = 0;
        foreach (var f in files) {
            string path = f.Path.Replace('\\', '/').ToLowerInvariant();
            if (known.Contains(path) || !seen.Add(path)) continue;
            Guid g = NameUuid(path);
            int p = 0; var pair = ObjectStream.ParseElement(infoTemplate, ref p);
            SetAssetId(Child(pair, KeyCrc), g, 0);
            var value = Child(pair, ValueCrc);
            SetAssetId(Child(value, InfoIdCrc), g, 0);
            Child(value, PathCrc).Value = Encoding.ASCII.GetBytes(path);
            Child(value, SizeCrc).Value = Be((ulong)f.Size, 8);
            Child(value, TypeCrc).Value = UuidBytes(f.Type);
            byte[] e = ObjectStream.ElementBytes(pair); info.Write(e, 0, e.Length);
            p = 0; pair = ObjectStream.ParseElement(pathTemplate, ref p);
            Child(pair, KeyCrc).Value = UuidBytes(g);
            SetAssetId(Child(pair, ValueCrc), g, 0);
            e = ObjectStream.ElementBytes(pair); paths.Write(e, 0, e.Length);
            added++;
        }
        var o = new MemoryStream(xml.Length + (int)info.Length + (int)paths.Length);
        o.Write(xml, 0, t[0].End);
        info.WriteTo(o);
        o.Write(xml, t[0].End, t[1].End - t[0].End);
        paths.WriteTo(o);
        o.Write(xml, t[1].End, xml.Length - t[1].End);
        return o.ToArray();
    }

    static byte[] Slice(byte[] b, int from, int to) { var r = new byte[to - from]; Array.Copy(b, from, r, 0, r.Length); return r; }
}
