import pathlib
import struct
import sys

from nif_material import blocks

source, output = map(pathlib.Path, sys.argv[1:])
for name in ('strand.nif', 'semen_blob.nif', 'semen_rope.nif'):
    original = (source / name).read_bytes()
    shader = [(offset, size) for kind, offset, size in blocks(original)
              if kind == 'BSLightingShaderProperty']
    if len(shader) != 1 or shader[0][1] != 100:
        raise ValueError(f'{name}: unexpected shader layout {shader}')
    offset, _ = shader[0]
    flags, = struct.unpack_from('<I', original, offset + 16)
    if struct.unpack_from('<4f', original, offset + 44) != (0.0, 0.0, 0.0, 1.0):
        raise ValueError(f'{name}: unexpected original emissive values')
    modified = bytearray(original)
    struct.pack_into('<I', modified, offset + 16, flags | 0x00400000)
    struct.pack_into('<4f', modified, offset + 44, 0.14, 0.14, 0.14, 0.0)
    path = output / name
    path.write_bytes(modified)
    actual = path.read_bytes()
    if len(actual) != len(original) or list(blocks(actual)) != list(blocks(original)):
        raise ValueError(f'{name}: block layout changed')
    original_fields = bytearray(actual)
    struct.pack_into('<I', original_fields, offset + 16, flags)
    struct.pack_into('<4f', original_fields, offset + 44, 0.0, 0.0, 0.0, 1.0)
    if original_fields != original:
        raise ValueError(f'{name}: changes outside the emissive fields')
    print(f'{name}: verified all non-emissive bytes match the original')
