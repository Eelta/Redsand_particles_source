
from pathlib import Path
import argparse
import json
import math
import struct
import uuid


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def pe_identity(path):
    data = path.read_bytes()
    pe = u32(data, 0x3C)
    assert data[pe:pe+4] == b'PE\0\0'
    optional = pe + 24
    magic = struct.unpack_from('<H', data, optional)[0]
    directories = optional + (112 if magic == 0x20B else 96)
    rva, length = struct.unpack_from('<II', data, directories + 6 * 8)
    sections = optional + struct.unpack_from('<H', data, pe + 20)[0]
    debug = None
    for index in range(struct.unpack_from('<H', data, pe + 6)[0]):
        offset = sections + 40 * index
        virtual_size, address, raw_size, raw = struct.unpack_from('<IIII', data, offset + 8)
        if address <= rva < address + max(virtual_size, raw_size):
            debug = raw + rva - address
            break
    assert debug is not None
    for offset in range(debug, debug + length, 28):
        if u32(data, offset + 12) != 2:
            continue
        pointer = u32(data, offset + 24)
        if data[pointer:pointer+4] == b'RSDS':
            return str(uuid.UUID(bytes_le=data[pointer+4:pointer+20])), u32(data, pointer+20)
    raise ValueError('No RSDS CodeView record')


def pdb_identity(path):
    data = path.read_bytes()
    assert data.startswith(b'Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0')
    block = u32(data, 32)
    directory_size = u32(data, 44)
    block_map = u32(data, 52) * block
    directory_blocks = [u32(data, block_map + 4 * i) for i in range(math.ceil(directory_size / block))]
    directory = b''.join(data[n*block:(n+1)*block] for n in directory_blocks)[:directory_size]
    streams = u32(directory, 0)
    sizes = [u32(directory, 4 + 4*i) for i in range(streams)]
    cursor = 4 + 4 * streams
    for index, size in enumerate(sizes):
        count = 0 if size == 0xFFFFFFFF else math.ceil(size / block)
        blocks = [u32(directory, cursor + 4*i) for i in range(count)]
        cursor += 4 * count
        if index == 1:
            info = b''.join(data[n*block:(n+1)*block] for n in blocks)[:size]
            return str(uuid.UUID(bytes_le=info[12:28])), u32(info, 8)
    raise ValueError('No PDB information stream')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('dll', type=Path)
    parser.add_argument('pdb', type=Path)
    args = parser.parse_args()
    dll = pe_identity(args.dll)
    pdb = pdb_identity(args.pdb)
    assert dll == pdb, 'DLL/PDB GUID or age mismatch'
    print(json.dumps({'matched': True, 'guid': dll[0], 'age': dll[1]}))


if __name__ == '__main__':
    main()
