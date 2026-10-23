#pragma once

// Musical cues in the original registration catalog (port/docs/sound-index.txt).
// Slot purpose is stable even when a mod replaces its WAV. The dedicated-buffer
// flag and the decoded file size control voice concurrency, not mixer category.
// Credits #129 uses flag 0x04; ambience #45 also uses 0x04 but is an effect.
static inline int snd_is_music(int index) {
  switch (index) {
    case 44: case 50: case 51: case 52: case 53: // purchase and romance jingles
    case 110: case 111: case 112:               // tropical music
    case 115: case 116: case 117:               // links music
    case 120: case 121: case 122:               // desert music
    case 125: case 126: case 127: case 128: case 129:
      return 1;
    default:
      return 0;
  }
}
