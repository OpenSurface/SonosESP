const icons = {
  MDI_PLAY:        0xF040A,
  MDI_PAUSE:       0xF03E4,
  MDI_SKIP_PREV:   0xF04AE,
  MDI_SKIP_NEXT:   0xF04AD,
  MDI_SHUFFLE:     0xF049D,
  MDI_REPEAT:      0xF0456,
  MDI_VOLUME_HIGH: 0xF057E,
  MDI_MUSIC_NOTE:  0xF0387,
  MDI_MUSIC_BOX:   0xF0384,
  MDI_PLAYLIST:    0xF0411,
  MDI_COG:         0xF0493,
  MDI_ARROW_LEFT:  0xF004D,
  MDI_BROADCAST:   0xF1720,
  MDI_RADIO:       0xF0439,
};

function toUtf8Hex(cp) {
  let bytes;
  if (cp < 0x80) {
    bytes = [cp];
  } else if (cp < 0x800) {
    bytes = [0xC0 | (cp >> 6), 0x80 | (cp & 0x3F)];
  } else if (cp < 0x10000) {
    bytes = [0xE0 | (cp >> 12), 0x80 | ((cp >> 6) & 0x3F), 0x80 | (cp & 0x3F)];
  } else {
    bytes = [
      0xF0 | (cp >> 18),
      0x80 | ((cp >> 12) & 0x3F),
      0x80 | ((cp >> 6)  & 0x3F),
      0x80 | (cp & 0x3F),
    ];
  }
  return bytes.map(b => '\\x' + b.toString(16).toUpperCase().padStart(2, '0')).join('');
}

Object.entries(icons).forEach(([name, cp]) => {
  const utf8 = toUtf8Hex(cp);
  const padded = name.padEnd(22);
  process.stdout.write('#define ' + padded + ' "' + utf8 + '"\n');
});
