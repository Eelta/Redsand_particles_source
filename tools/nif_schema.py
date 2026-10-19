from typed_nif import NifFile, Field, Reader, Block, FormatError
import struct
class DirectionalNif(NifFile):


    def fields(self, index):
        b = self.blocks[index]
        if b.type != 'BSTriShape':
            return super().fields(index)
        if self.bs_version != 100:
            raise FormatError('This mesh parser is SSE stream 100 only')
        r = Reader(b.data)
        out = {}

        def field(name, fmt, ref=False):
            offset = r.offset
            value = r.unpack(fmt)
            out[name] = Field(offset, r.offset - offset, fmt, value, ref)
            return value

        def array(name, fmt, count, ref=False):
            if not 0 <= count < 1000000:
                raise FormatError('Implausible mesh count')
            offset = r.offset
            value = tuple((r.unpack(fmt) for _ in range(count)))
            out[name] = Field(offset, r.offset - offset, fmt + '[]', value, ref)
            return value
        field('name', 'I')
        array('extra_data', 'i', field('num_extra_data', 'I'), True)
        field('controller', 'i', True)
        field('flags', 'I')
        field('translation', '3f')
        field('rotation', '9f')
        field('scale', 'f')
        field('collision', 'i', True)
        field('bound', '4f')
        field('skin', 'i', True)
        field('shader', 'i', True)
        field('alpha', 'i', True)
        desc = field('vertex_desc', 'Q')
        nt = field('num_triangles', 'H')
        nv = field('num_vertices', 'H')
        size = field('data_size', 'I')
        attrs = desc >> 44
        fmt = ''
        if attrs & 1:
            fmt += '3ff' if attrs & 16 else '3fI'
        if attrs & 2:
            fmt += '2e'
        if attrs & 8:
            fmt += '4B'
        if attrs & 24 == 24:
            fmt += '4B'
        if attrs & 32:
            fmt += '4B'
        if attrs & 64:
            fmt += '4e4B'
        if attrs & 256:
            fmt += 'f'
        if attrs & ~1403:
            raise FormatError(f'Unsupported vertex attribute combination {attrs:#x}')
        if struct.calcsize('<' + fmt) != (desc & 15) * 4:
            raise FormatError('Vertex stride differs from declared descriptor')
        if size != nv * (desc & 15) * 4 + nt * 6:
            raise FormatError('Invalid BSTriShape data size')
        if size:
            array('vertices', fmt, nv)
            triangles = array('triangles', '3H', nt)
            if any((v >= nv for tri in triangles for v in tri)):
                raise FormatError('Out-of-range triangle index')
        particle_size = field('particle_data_size', 'I')
        if particle_size:
            array('particle_vertices', '3e', nv)
            array('particle_normals', '3e', nv)
            array('particle_triangles', '3H', nt)
        r.finish()
        return out
class CandidateNif(DirectionalNif):

    def fields(self, index):
        if self.blocks[index].type != 'BSPSysLODModifier':
            return super().fields(index)
        r = Reader(self.blocks[index].data)
        out = {}
        for name, fmt, ref in [('name', 'I', False), ('order', 'I', False), ('target', 'i', True), ('active', 'B', False), ('lod_begin', 'f', False), ('lod_end', 'f', False), ('end_emit_scale', 'f', False), ('end_size', 'f', False)]:
            off = r.offset
            value = r.unpack(fmt)
            out[name] = Field(off, r.offset - off, fmt, value, ref)
        r.finish()
        return out
