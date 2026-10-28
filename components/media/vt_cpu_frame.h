// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_MEDIA_VT_CPU_FRAME_H_
#define UW_COMPONENTS_MEDIA_VT_CPU_FRAME_H_

#include <CoreVideo/CoreVideo.h>

#include "base/memory/scoped_refptr.h"

namespace media {

class VideoFrame;
struct VideoToolboxDecodeMetadata;

// Copies a frame VideoToolbox decoded in software into memory the page's
// process can read, for when there is no command buffer to share it through:
// accelerated video decoding is off, or the hardware turned the stream down.
// Takes 8-bit 4:2:0, which is all H.264 here puts out; null for anything
// else. See uw's docs/plans/distribution.md, "Codecs".
scoped_refptr<VideoFrame> CopyToCpuFrame(
    CVImageBufferRef image,
    const VideoToolboxDecodeMetadata& metadata);

}  // namespace media

#endif  // UW_COMPONENTS_MEDIA_VT_CPU_FRAME_H_
