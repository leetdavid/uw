# Codec test files

Two seconds each, made on a Mac with its own encoders:

- `lc.m4a`, `he.m4a`, `he_v2.m4a`: a 440 Hz stereo tone as AAC-LC, HE-AAC, and HE-AAC v2, from `afconvert -f m4af -d aac|aach|aacp`.
- `tone.aac`: the same tone as raw ADTS, from `afconvert -f adts -d aac`.
- `mse.mp4`: `lc.m4a` as fragmented MP4 for Media Source.
- `small.mp4`: 160x120 H.264 Main with `lc.m4a`'s audio; `large.mp4`: 640x480 H.264 High. Both from ffmpeg's `testsrc` and `h264_videotoolbox`.
