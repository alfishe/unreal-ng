// Headless co-emulation harness for Kozynax (see ../README.md and ../../README.md).
//
// usage: kozynax-harness <machine> <media> <sym> <outprefix> [maxframes]
//
// Builds the harness machine from Kozynax's own stock definition (the <Bus> of that name in Kozynax's
// machines.config, as File > New machine does), mounts the tape or disk image through Kozynax's own
// loaders, types the loader keys as a user would, then runs frames until the program's DONE byte is 1.
// Writes <outprefix>.bin (START..PROBEEND-1) and <outprefix>.screen.txt (the screen as text).
// Kozynax is a descendant of ZXMAK2; this file follows ../../zxmak2/harness/Program.cs.
//
// exit: 0 DONE = 1, 1 not done in maxframes, 2 bad arguments / setup, 3 the program crashed
// (the CPU reached #0000 after the program had started)
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using ZXMAK2.Dependency;
using ZXMAK2.Engine;
using ZXMAK2.Engine.Entities;
using ZXMAK2.Engine.Interfaces;
using ZXMAK2.Hardware.Circuits.Sound;
using ZXMAK2.Hardware.Profi;
using ZXMAK2.Host.Entities;
using ZXMAK2.Host.Interfaces;

namespace KozynaxHarness
{
    // a key stroke: keys pressed together (ZX names) for Hold frames, then the frames to wait after they are released
    sealed class Stroke
    {
        public readonly string[] Keys;
        public readonly int Hold;
        public readonly int Gap;
        public Stroke(string keys, int gap, int hold = 4) { Keys = keys.Split('+'); Gap = gap; Hold = hold; }
    }

    sealed class MachineDef
    {
        public string Bus;          // <Bus name> in Kozynax's machines.config
        public bool Disk;           // the .trd in drive A (else the .tap)
        public int Boot;            // frames from reset before the first key
        public Stroke[] Keys;
    }

    sealed class HeldKeys : IKeyboardState
    {
        private readonly HashSet<Key> _keys;
        public HeldKeys(IEnumerable<Key> keys) { _keys = new HashSet<Key>(keys); }
        public bool this[Key key] { get { return _keys.Contains(key); } }
    }

    sealed class UserMessage : IUserMessage
    {
        // straight to stderr: Kozynax's ZXMAK2.Logger has no log behind it (its logger field is never set)
        public void ErrorDetails(Exception ex) { Console.Error.WriteLine("error: " + ex); }
        public void Error(Exception ex) { Console.Error.WriteLine("error: " + ex.Message); }
        public void Error(string fmt, params object[] args) { Console.Error.WriteLine("error: " + string.Format(fmt, args)); }
        public void Warning(Exception ex) { Console.Error.WriteLine("warning: " + ex.Message); }
        public void Warning(string fmt, params object[] args) { Console.Error.WriteLine("warning: " + string.Format(fmt, args)); }
        public void Info(string fmt, params object[] args) { Console.Error.WriteLine("info: " + string.Format(fmt, args)); }
    }

    // no one to ask: every question is answered "no" (e.g. "save the changed disk?")
    sealed class UserQuery : IUserQuery
    {
        public DlgResult Show(string message, string caption, DlgButtonSet buttonSet, DlgIcon icon) { return DlgResult.No; }
        public object ObjectSelector(object[] objArray, string caption) { return null; }
        public bool QueryText(string caption, string text, ref string value) { return false; }
        public bool QueryValue(string caption, string text, string format, ref int value, int min, int max) { return false; }
    }

    static class Program
    {
        const int KeyHold = 4;
        const int KeyGap = 8;

        // LOAD "" in 48 BASIC (keyword mode: J is LOAD)
        static readonly Stroke[] LoadQuote = {
            new Stroke("J", KeyGap), new Stroke("SS+P", KeyGap), new Stroke("SS+P", KeyGap), new Stroke("ENTER", 0) };
        // the first item of a 128 / +3 menu (Tape Loader / Loader)
        static readonly Stroke[] MenuFirst = { new Stroke("ENTER", 0) };
        // TR-DOS's command line: RUN (keyword mode: R is RUN) runs "boot"
        static readonly Stroke[] TrdosRun = { new Stroke("R", KeyGap), new Stroke("ENTER", 0) };
        // the 128 menu's cursor down (CAPS SHIFT + 6)
        static readonly Stroke Down = new Stroke("CS+6", KeyGap);
        const int MenuWait = 150;   // after choosing a menu item that starts TR-DOS or another ROM

        static Stroke[] Seq(params Stroke[][] parts) { return parts.SelectMany(p => p).ToArray(); }
        static Stroke[] Repeat(Stroke s, int n) { return Enumerable.Repeat(s, n).ToArray(); }
        static Stroke[] Press(string keys, int gap) { return new[] { new Stroke(keys, gap) }; }

        static readonly Dictionary<string, MachineDef> Machines = new Dictionary<string, MachineDef>
        {
            // LOAD "", then Play
            { "48k", new MachineDef { Bus = "ZX Spectrum 48", Boot = 150, Keys = LoadQuote } },
            // the 128 menu's "Tape Loader", then Play
            { "128k", new MachineDef { Bus = "ZX Spectrum 128", Boot = 150, Keys = MenuFirst } },
            // the +3 menu's "Loader" (Kozynax's +3 has no floppy controller: the loader reads the tape), then Play
            { "plus3", new MachineDef { Bus = "ZX Spectrum +3", Boot = 150, Keys = MenuFirst } },
            // the 128 menu (Tape Loader, 128 BASIC, Calculator, 48 BASIC, TR-DOS): TR-DOS, then RUN
            { "pentagon", new MachineDef { Bus = "PENTAGON 128K", Disk = true, Boot = 150,
                Keys = Seq(Repeat(Down, 4), Press("ENTER", MenuWait), TrdosRun) } },
            // the Scorpion menu (128 TR-DOS, 128 BASIC, Calculator, 48 BASIC, 48 TR-DOS): 128 TR-DOS, then RUN
            { "scorpion", new MachineDef { Bus = "Scorpion ZS 256", Disk = true, Boot = 150,
                Keys = Seq(Press("ENTER", MenuWait), TrdosRun) } },
            // the ProfROM's Shadow Service Monitor tests the machine first (about 800 frames), then the same menu
            { "profscorp", new MachineDef { Bus = "Scorpion ZS 256 PROF-ROM", Disk = true, Boot = 900,
                Keys = Seq(Press("ENTER", MenuWait), TrdosRun) } },
            // the ATM BIOS menu (CP/M, TR-DOS 48, SPECTRUM 128, SPECTRUM 48): SPECTRUM 128, then as the Pentagon
            { "atm710", new MachineDef { Bus = "ATM Turbo 2+ [V7.10]", Disk = true, Boot = 150,
                Keys = Seq(Repeat(Down, 2), Press("ENTER", MenuWait), Repeat(Down, 4), Press("ENTER", MenuWait), TrdosRun) } },
            // the EVO Reset Service (a fresh CMOS): Y moves EVO-DOS's virtual drive from A to B (else drive A is
            // the virtual one and the floppy is not seen), S starts TR-DOS (EVO-DOS), then RUN
            { "atm3", new MachineDef { Bus = "PENT EVO", Disk = true, Boot = 300,
                Keys = Seq(Press("Y", 60), Press("S", MenuWait), TrdosRun) } },
            // the Profi BIOS boots the disk in drive A by itself
            { "profi", new MachineDef { Bus = "PROFI+ 1024 [V5.XX]", Disk = true, Boot = 0, Keys = new Stroke[0] } },
        };

        static Spectrum _spec;
        static long _frames;
        static bool _frameDone;
        static int _start = -1, _probeEnd = -1, _done = -1;
        static long _startSeen = -1, _crashAt = -1;
        static List<IKeyboardDevice> _keyboards;
        // debugging: KOZYNAX_SHOTS=<frame>,<frame>... writes the rendered picture at those frames (<outprefix>.f<frame>.ppm)
        static HashSet<long> _shots = new HashSet<long>();
        static string _outPrefix;
        // the last instructions (PC, frame T-state) before a crash, printed to the log
        const int TraceLen = 48;
        static readonly int[] _tracePc = new int[TraceLen];
        static readonly int[] _traceT = new int[TraceLen];
        static int _tracePos;

        static int Main(string[] args)
        {
            if (args.Length < 4)
            {
                Console.Error.WriteLine("usage: kozynax-harness <machine> <media> <sym> <outprefix> [maxframes]");
                Console.Error.WriteLine("machines: " + string.Join(" ", Machines.Keys));
                return 2;
            }
            MachineDef mac;
            if (!Machines.TryGetValue(args[0], out mac))
            {
                Console.Error.WriteLine("unknown machine " + args[0]);
                return 2;
            }
            var media = args[1];
            var outPrefix = args[3];
            _outPrefix = outPrefix;
            foreach (var f in (Environment.GetEnvironmentVariable("KOZYNAX_SHOTS") ?? "").Split(new[] { ',' }, StringSplitOptions.RemoveEmptyEntries))
            {
                _shots.Add(long.Parse(f, CultureInfo.InvariantCulture));
            }
            long maxFrames = args.Length > 4 ? long.Parse(args[4], CultureInfo.InvariantCulture) : 60000;
            // debugging: KOZYNAX_KEYS / KOZYNAX_BOOT replace the machine's key strokes / frames before the first key
            // (strokes separated by ',', keys pressed together by '+', '*<frames>' holds, ':<frames>' waits after)
            var keysEnv = Environment.GetEnvironmentVariable("KOZYNAX_KEYS");
            if (keysEnv != null)
            {
                mac.Keys = keysEnv.Split(new[] { ',' }, StringSplitOptions.RemoveEmptyEntries).Select(k =>
                {
                    var p = k.Split(':');
                    var h = p[0].Split('*');
                    return new Stroke(h[0], p.Length > 1 ? int.Parse(p[1], CultureInfo.InvariantCulture) : KeyGap,
                        h.Length > 1 ? int.Parse(h[1], CultureInfo.InvariantCulture) : KeyHold);
                }).ToArray();
            }
            var bootEnv = Environment.GetEnvironmentVariable("KOZYNAX_BOOT");
            if (bootEnv != null)
            {
                mac.Boot = int.Parse(bootEnv, CultureInfo.InvariantCulture);
            }
            ReadSym(args[2]);
            if (_start < 0 || _probeEnd < 0 || _done < 0)
            {
                Console.Error.WriteLine("START / PROBEEND / DONE not in " + args[2]);
                return 2;
            }

            // the zip reader (for a ROMS.PAK, if there is one) wants code page 437, which .NET has only through this provider
            Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
            // the services Kozynax's own host registers that the emulation uses: the PSG chip, and messages
            // and questions for the user (here printed / answered "no")
            var resolver = new ResolverSimple();
            resolver.RegisterType<IPsgChip, PsgChip>();
            resolver.RegisterInstance<IUserMessage>(new UserMessage());
            resolver.RegisterInstance<IUserQuery>(new UserQuery());
            Locator.Init(resolver);

            // Kozynax's machines.config, built into Kozynax.Sdl.dll (the stock machines of File > New)
            var config = new MachinesConfig();
            config.Load();
            var busNode = config.GetConfig(mac.Bus);
            if (busNode == null)
            {
                Console.Error.WriteLine("no machine '" + mac.Bus + "' in Kozynax's machines.config");
                return 2;
            }
            _spec = new Spectrum();
            _spec.Init();                                   // the default machine (as the GUI starts)
            // the machine file the GUI would have opened: devices keep their state files (CMOS, NVRAM) beside it,
            // here <outprefix>.cmos, .vmide etc., fresh on every run
            typeof(BusManager).GetProperty("MachineFile", BindingFlags.Instance | BindingFlags.NonPublic)
                .SetValue(_spec.BusManager, Path.GetFullPath(outPrefix + ".vmz"));
            foreach (var old in Directory.GetFiles(Path.GetDirectoryName(Path.GetFullPath(outPrefix)), Path.GetFileName(outPrefix) + ".*"))
            {
                var ext = Path.GetExtension(old);
                if (ext == ".cmos" || ext == ".nvram" || ext == ".vmide" || ext == ".vmz") File.Delete(old);
            }
            _spec.BusManager.LoadConfigXml(busNode);        // then the chosen one
            _spec.BusManager.FrameReady += () => _frameDone = true;
            _spec.DebugReset();
            var bus = _spec.BusManager;
            Console.WriteLine("machine: {0} ({1} T-states per frame)", mac.Bus, bus.FrameTactCount);
            foreach (var d in bus.FindDevices<BusDeviceBase>())
            {
                Console.WriteLine("  device: {0} [{1}]", d.Name, d.GetType().FullName);
            }

            // the image, through Kozynax's own loaders (as File > Open does)
            var mediaMsg = bus.LoadManager.OpenFileName(Path.GetFullPath(media), true);
            Console.WriteLine("opened {0}: {1}", Path.GetFileName(media), mediaMsg);
            var tape = bus.FindDevice<ITapeDevice>();
            if (tape != null)
            {
                Console.WriteLine("tape: traps {0}, auto play {1}", tape.UseTraps, tape.UseAutoPlay);
            }

            _keyboards = bus.FindDevices<IKeyboardDevice>();
            SetKeys(new string[0]);

            for (var i = 0; i < mac.Boot; i++) RunFrame();
            WriteScreen(outPrefix + ".boot.screen.txt");
            foreach (var s in mac.Keys)
            {
                SetKeys(s.Keys);
                for (var i = 0; i < s.Hold; i++) RunFrame();
                SetKeys(new string[0]);
                for (var i = 0; i < s.Gap; i++) RunFrame();
            }
            var keysFrame = _frames;
            if (!mac.Disk && tape != null && !tape.IsPlay)
            {
                tape.Play();    // the user presses Play
            }
            Console.WriteLine("keys typed at frame {0}{1}", keysFrame, mac.Disk ? ", disk in drive A" : ", tape playing");

            var memory = bus.FindDevice<IMemoryDevice>();
            var rc = 1;
            while (_frames < maxFrames)
            {
                RunFrame();
                if (_crashAt >= 0)
                {
                    Console.WriteLine("the program crashed: the CPU reached #0000 at frame {0}", _crashAt);
                    rc = 3;
                    break;
                }
                if (memory.RDMEM_DBG((ushort)_done) == 1)
                {
                    rc = 0;
                    break;
                }
                if ((_frames % 5000) == 0)
                {
                    Console.WriteLine("  frame {0} PC=#{1:X4}", _frames, _spec.CPU.regs.PC);
                }
            }
            Console.WriteLine("stopped at frame {0} ({1} after the keys, {2} after the program started): {3}",
                _frames, _frames - keysFrame, _startSeen >= 0 ? _frames - _startSeen : -1,
                rc == 0 ? "DONE = 1" : rc == 3 ? "crashed" : "not done");

            var dump = new byte[_probeEnd - _start];
            for (var i = 0; i < dump.Length; i++)
            {
                dump[i] = memory.RDMEM_DBG((ushort)(_start + i));
            }
            if (rc == 0)
            {
                File.WriteAllBytes(outPrefix + ".bin", dump);
            }
            WriteScreen(outPrefix + ".screen.txt");
            return rc;
        }

        static void ReadSym(string path)
        {
            foreach (var line in File.ReadAllLines(path))
            {
                var p = line.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
                if (p.Length != 3 || p[1] != "equ" || !p[2].StartsWith("#")) continue;
                var v = int.Parse(p[2].Substring(1), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
                if (p[0] == "START") _start = v;
                else if (p[0] == "PROBEEND") _probeEnd = v;
                else if (p[0] == "DONE") _done = v;
            }
        }

        // one frame, instruction by instruction, watching the program counter
        static void RunFrame()
        {
            var bus = _spec.BusManager;
            var cpu = _spec.CPU;
            _frameDone = false;
            while (!_frameDone)
            {
                var at = bus.GetFrameTact();
                var from = cpu.regs.PC;
                bus.ExecCycle();
                var pc = cpu.regs.PC;
                _tracePc[_tracePos] = from;
                _traceT[_tracePos] = at;
                _tracePos = (_tracePos + 1) % TraceLen;
                if (_startSeen < 0)
                {
                    if (pc == _start) _startSeen = _frames;
                }
                else if (pc == 0 && _crashAt < 0)
                {
                    _crashAt = _frames;
                    Console.WriteLine("the last instructions before #0000 (PC, T-state in the frame):");
                    for (var i = 0; i < TraceLen; i++)
                    {
                        var j = (_tracePos + i) % TraceLen;
                        Console.WriteLine("  #{0:X4} T{1}", _tracePc[j], _traceT[j]);
                    }
                }
            }
            _frames++;
            if (_shots.Contains(_frames))
            {
                WritePicture(_outPrefix + ".f" + _frames + ".ppm");
            }
        }

        static void WritePicture(string path)
        {
            var ula = _spec.BusManager.FindDevice<IUlaDevice>();
            ula.Flush();
            var v = ula.VideoData;
            using (var fs = File.Create(path))
            {
                var head = Encoding.ASCII.GetBytes(string.Format("P6\n{0} {1}\n255\n", v.Size.Width, v.Size.Height));
                fs.Write(head, 0, head.Length);
                var px = new byte[v.Size.Width * v.Size.Height * 3];
                for (var i = 0; i < v.Size.Width * v.Size.Height; i++)
                {
                    var c = v.Buffer[i];
                    px[i * 3] = (byte)(c >> 16);
                    px[i * 3 + 1] = (byte)(c >> 8);
                    px[i * 3 + 2] = (byte)c;
                }
                fs.Write(px, 0, px.Length);
            }
        }

        // ZX key names to the host keys each Kozynax keyboard device reads
        static void SetKeys(string[] zxKeys)
        {
            foreach (var kbd in _keyboards)
            {
                var profi = kbd is KeyboardProfi;
                var host = new List<Key>();
                foreach (var k in zxKeys)
                {
                    switch (k)
                    {
                        case "CS": host.Add(profi ? Key.LeftControl : Key.LeftShift); break;
                        case "SS": host.Add(profi ? Key.LeftShift : Key.RightShift); break;
                        case "ENTER": host.Add(Key.Return); break;
                        case "SPACE": host.Add(Key.Space); break;
                        default:
                            if (k.Length == 1 && char.IsDigit(k[0])) host.Add((Key)Enum.Parse(typeof(Key), "D" + k));
                            else host.Add((Key)Enum.Parse(typeof(Key), k));
                            break;
                    }
                }
                kbd.KeyboardState = new HeldKeys(host);
            }
        }

        // the screen as text: each character cell matched against the 48K ROM font (#3D00), '?' if none
        static void WriteScreen(string path)
        {
            try
            {
                var memory = _spec.BusManager.FindDevice<IMemoryDevice>();
                byte[] font = null;
                foreach (var rom in memory.RomPages)
                {
                    // the 48K font: the space glyph (8 zeros) then "!" (#00 #10 #10 #10 #10 #00 #10 #00)
                    if (rom != null && rom.Length >= 0x4000 && rom[0x3D08] == 0x00 && rom[0x3D09] == 0x10 &&
                        rom[0x3D0A] == 0x10 && rom[0x3D0E] == 0x10 && rom[0x3D0F] == 0x00)
                    {
                        font = rom.Skip(0x3D00).Take(0x300).ToArray();
                        break;
                    }
                }
                var sb = new StringBuilder();
                for (var row = 0; row < 24; row++)
                {
                    for (var col = 0; col < 32; col++)
                    {
                        var cell = new byte[8];
                        for (var y = 0; y < 8; y++)
                        {
                            var line = row * 8 + y;
                            var addr = 0x4000 | ((line & 0xC0) << 5) | ((line & 0x07) << 8) | ((line & 0x38) << 2) | col;
                            cell[y] = memory.RDMEM_DBG((ushort)addr);
                        }
                        sb.Append(MatchGlyph(font, cell));
                    }
                    sb.AppendLine();
                }
                File.WriteAllText(path, sb.ToString());
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("screen: " + ex.Message);
            }
        }

        static char MatchGlyph(byte[] font, byte[] cell)
        {
            if (font == null) return '?';
            for (var pass = 0; pass < 2; pass++)
            {
                for (var c = 0; c < 96; c++)
                {
                    var ok = true;
                    for (var y = 0; y < 8 && ok; y++)
                    {
                        var v = pass == 0 ? cell[y] : (byte)~cell[y];
                        ok = font[c * 8 + y] == v;
                    }
                    if (ok) return (char)(32 + c);
                }
            }
            return '?';
        }
    }
}
