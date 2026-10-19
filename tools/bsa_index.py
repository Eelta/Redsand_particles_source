from pathlib import Path
import json
import struct

def normalize(value):
    return value.replace('\\', '/').lower().strip('\x00 /')

def names(path):
    with path.open('rb') as stream:
        header = stream.read(36)
        magic, version, offset, flags, folders, files, folder_bytes, name_bytes, file_flags = struct.unpack('<4s8I', header)
        if magic != b'BSA\x00' or version not in (103, 104, 105):
            raise ValueError(f'unsupported header: {magic!r}, {version}')
        if flags & 3 != 3:
            raise ValueError('name tables absent')
        if folders > 1000000 or files > 10000000:
            raise ValueError('unreasonable counts')
        stream.seek(offset)
        counts = []
        for _ in range(folders):
            record = stream.read(24 if version == 105 else 16)
            counts.append(struct.unpack_from('<I', record, 8)[0])
        if sum(counts) != files:
            raise ValueError('folder/file counts disagree')
        directories = []
        for count in counts:
            size = stream.read(1)[0]
            name = stream.read(size).rstrip(b'\x00').decode('cp1252')
            directories.append((name, count))
            stream.seek(count * 16, 1)
        file_names = stream.read(name_bytes).split(b'\x00')
        if len(file_names) != files + 1 or file_names[-1] != b'':
            raise ValueError('file-name table did not match header')
        index = 0
        for directory, count in directories:
            for _ in range(count):
                yield normalize(directory + '/' + file_names[index].decode('cp1252'))
                index += 1
