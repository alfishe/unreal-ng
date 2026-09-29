// Stand-in for ZXMAK2.Resources: the on-screen-display icons the engine asks for. There is no screen, and
// GDI+ (System.Drawing's bitmaps) is not available off Windows, so there are none.
using System.Drawing;

namespace ZXMAK2.Resources
{
    public class ResourceImages
    {
        internal ResourceImages() { }
        public static Bitmap OsdFddRd { get { return null; } }
        public static Bitmap OsdFddWr { get { return null; } }
        public static Bitmap OsdHddRd { get { return null; } }
        public static Bitmap OsdPause { get { return null; } }
        public static Bitmap OsdTapeRd { get { return null; } }
    }
}
