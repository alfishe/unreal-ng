// Stand-in for ZXMAK2.Logging (log4net): the same Logger API, printed to stderr.
// Debug and Info are printed only with ZXMAK2_LOG=1.
using System;
using System.Globalization;

namespace ZXMAK2
{
    public static class Logger
    {
        private static readonly bool Verbose = Environment.GetEnvironmentVariable("ZXMAK2_LOG") == "1";

        private static void Write(string level, Exception ex, string fmt, object[] args)
        {
            string text = null;
            if (fmt != null)
            {
                try { text = args != null && args.Length > 0 ? string.Format(CultureInfo.InvariantCulture, fmt, args) : fmt; }
                catch (FormatException) { text = fmt; }
            }
            if (ex != null)
                text = text == null ? ex.ToString() : text + ": " + ex;
            Console.Error.WriteLine("zxmak2 {0}: {1}", level, text);
        }

        public static void Start() { }
        public static void Finish() { }

        public static void Debug(string fmt, params object[] args) { if (Verbose) Write("debug", null, fmt, args); }
        public static void Info(string fmt, params object[] args) { if (Verbose) Write("info", null, fmt, args); }
        public static void Warn(string fmt, params object[] args) { Write("warn", null, fmt, args); }
        public static void Error(string fmt, params object[] args) { Write("error", null, fmt, args); }
        public static void Fatal(string fmt, params object[] args) { Write("fatal", null, fmt, args); }

        public static void Debug(Exception exception, string fmt, params object[] args) { if (Verbose) Write("debug", exception, fmt, args); }
        public static void Info(Exception exception, string fmt, params object[] args) { if (Verbose) Write("info", exception, fmt, args); }
        public static void Warn(Exception exception, string fmt, params object[] args) { Write("warn", exception, fmt, args); }
        public static void Error(Exception exception, string fmt, params object[] args) { Write("error", exception, fmt, args); }
        public static void Fatal(Exception exception, string fmt, params object[] args) { Write("fatal", exception, fmt, args); }

        public static void Debug(Exception exception) { Debug(exception, null); }
        public static void Info(Exception exception) { Info(exception, null); }
        public static void Warn(Exception exception) { Warn(exception, null); }
        public static void Error(Exception exception) { Error(exception, null); }
        public static void Fatal(Exception exception) { Fatal(exception, null); }

        public static void DumpArray<T>(string fileName, T[] array) { }
        public static void DumpAppend(string fileName, string format, params object[] args) { }
    }
}
