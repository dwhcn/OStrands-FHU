import pathlib, struct, sys

def blocks(data):
    p = data.index(b'\n') + 1
    version, endian, user, count, stream = struct.unpack_from('<IBIII', data, p)
    if (version, endian, user, stream) != (0x14020007, 1, 12, 100):
        raise ValueError((version, endian, user, stream))
    p += 17
    for _ in range(3):
        p += 1 + data[p]
    kinds, = struct.unpack_from('<H', data, p)
    p += 2
    names = []
    for _ in range(kinds):
        length, = struct.unpack_from('<I', data, p)
        p += 4
        names.append(data[p:p+length].decode('ascii'))
        p += length
    ids = struct.unpack_from(f'<{count}H', data, p)
    p += 2 * count
    sizes = struct.unpack_from(f'<{count}I', data, p)
    p += 4 * count
    strings, _ = struct.unpack_from('<II', data, p)
    p += 8
    for _ in range(strings):
        length, = struct.unpack_from('<I', data, p)
        p += 4 + length
    groups, = struct.unpack_from('<I', data, p)
    p += 4 + 4 * groups
    for kind, size in zip(ids, sizes):
        yield names[kind], p, size
        p += size

if __name__ == '__main__':
    for name in sys.argv[1:]:
        path = pathlib.Path(name)
        data = path.read_bytes()
        for kind, p, size in blocks(data):
            if kind == 'BSLightingShaderProperty':
                print(path, p, size, 'flags', hex(struct.unpack_from('<I', data, p+16)[0]), 'emit', struct.unpack_from('<4f', data, p+44))
