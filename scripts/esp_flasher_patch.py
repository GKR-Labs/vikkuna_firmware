PATH = 'src/esp_stubs.c'

# Read.
lines = open(PATH, 'r', encoding='utf8').readlines()

# Patch struct.
segments = []
for i, line in enumerate(lines):
    stripped = line.strip()
    if stripped.startswith('// esp'):
        chip = stripped
    if stripped.startswith('.data'):
        if chip == '// esp32c2.json':
            lines[i] = f'\t\t\t\t.data = c2_segment_{len(segments)},\n'
            segments.append(stripped + '\n')
        else:
            lines[i] = ''  # Remove line of not used stubs.

# Patch set C2 stub in flash.
for i, segment in enumerate(segments):
    segment = segment.replace(',\n', ';\n')
    segment = segment.replace(
        '.data = (uint8_t[])',
        f'uint8_t __in_flash() c2_segment_{i}[] = ',
    )
    segments[i] = segment

# Write.
headers = [
    '#include <stdint.h>\n'
    '#include <pico/platform.h>\n'
]
composed = ''.join(headers + segments + lines)
open(PATH, 'w', encoding='utf8').write(composed)
print('ESP flasher stub patched')
