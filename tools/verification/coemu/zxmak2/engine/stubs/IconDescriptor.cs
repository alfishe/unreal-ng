// Stand-in for ZXMAK2.Host's IconDescriptor (an on-screen-display icon): the same API without the image,
// which would need GDI+.
using System.Drawing;
using System.IO;
using ZXMAK2.Host.Interfaces;

namespace ZXMAK2.Host.Entities
{
    public class IconDescriptor : IIconDescriptor
    {
        public string Name { get; private set; }
        public Size Size { get; private set; }
        public bool Visible { get; set; }

        public IconDescriptor(string iconName, Image iconImage) { Name = iconName; }
        public IconDescriptor(string iconName, Stream iconStream) { Name = iconName; }

        public Stream GetImageStream() { return new MemoryStream(); }
    }
}
