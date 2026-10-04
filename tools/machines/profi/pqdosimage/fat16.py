"""A read-only FAT16 volume inside a partitioned raw disk image: the root directory, directory chains and files."""
import struct


class Fat16:
    def __init__(self, path, partitionLba):
        self.file = open(path, 'rb')
        self.base = partitionLba * 512
        self.file.seek(self.base)
        self.boot = self.file.read(512)
        boot = self.boot
        self.sectorsPerCluster = boot[13]
        self.reserved = struct.unpack('<H', boot[14:16])[0]
        self.fats = boot[16]
        self.rootEntries = struct.unpack('<H', boot[17:19])[0]
        self.fatSectors = struct.unpack('<H', boot[22:24])[0]
        self.rootSector = self.reserved + self.fats * self.fatSectors
        self.dataSector = self.rootSector + self.rootEntries * 32 // 512
        self.file.seek(self.base + self.reserved * 512)
        self.fat = list(struct.unpack('<%dH' % (self.fatSectors * 256), self.file.read(self.fatSectors * 512)))

    @property
    def clusterBytes(self):
        return self.sectorsPerCluster * 512

    def chain(self, cluster):
        out = []
        while 2 <= cluster < 0xFFF8:
            out.append(cluster)
            cluster = self.fat[cluster]
        return out

    def clusterOffset(self, cluster):
        return self.base + (self.dataSector + (cluster - 2) * self.sectorsPerCluster) * 512

    def readCluster(self, cluster):
        self.file.seek(self.clusterOffset(cluster))
        return self.file.read(self.clusterBytes)

    def readRoot(self):
        self.file.seek(self.base + self.rootSector * 512)
        return self.file.read(self.rootEntries * 32)

    def readDirectory(self, cluster):
        return b''.join(self.readCluster(c) for c in self.chain(cluster))


def entries(raw):
    """(offset, name, attributes, first cluster, size) of every live entry; long-name entries are skipped"""
    for offset in range(0, len(raw), 32):
        entry = raw[offset:offset + 32]
        if entry[0] == 0:
            break
        if entry[0] == 0xE5 or entry[11] == 0x0F:
            continue
        name = entry[:8].decode('latin1').rstrip()
        if entry[8:11].strip():
            name += '.' + entry[8:11].decode('latin1').rstrip()
        yield offset, name, entry[11], struct.unpack('<H', entry[26:28])[0], struct.unpack('<I', entry[28:32])[0]
