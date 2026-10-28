// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/media/vt_cpu_frame.h"

#include <algorithm>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "media/base/timestamp_constants.h"
#include "media/base/video_frame.h"
#include "media/gpu/mac/video_toolbox_decompression_metadata.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace media {

scoped_refptr<VideoFrame> CopyToCpuFrame(
    CVImageBufferRef image,
    const VideoToolboxDecodeMetadata& metadata) {
  const OSType format = CVPixelBufferGetPixelFormatType(image);
  if (format != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange &&
      format != kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
    return nullptr;
  }
  if (CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly) !=
      kCVReturnSuccess) {
    return nullptr;
  }
  const gfx::Size coded_size(CVPixelBufferGetWidth(image),
                             CVPixelBufferGetHeight(image));
  const gfx::Rect visible_rect(CVImageBufferGetCleanRect(image));
  scoped_refptr<VideoFrame> frame = VideoFrame::CreateFrame(
      PIXEL_FORMAT_NV12, coded_size, visible_rect,
      metadata.aspect_ratio.GetNaturalSize(visible_rect), metadata.timestamp);
  if (frame) {
    // Y, then interleaved U and V at half the height.
    for (size_t plane = 0; plane < 2; ++plane) {
      const size_t rows = CVPixelBufferGetHeightOfPlane(image, plane);
      const size_t source_stride =
          CVPixelBufferGetBytesPerRowOfPlane(image, plane);
      // SAFETY: CoreVideo guarantees a locked plane spans its rows times its
      // bytes per row.
      const auto source = UNSAFE_BUFFERS(
          base::span(static_cast<const uint8_t*>(
                         CVPixelBufferGetBaseAddressOfPlane(image, plane)),
                     rows * source_stride));
      base::span<uint8_t> destination = frame->writable_span(plane);
      const size_t stride = frame->stride(plane);
      const size_t width = std::min(source_stride, stride);
      for (size_t row = 0;
           row < rows && (row + 1) * stride <= destination.size(); ++row) {
        destination.subspan(row * stride, width)
            .copy_from(source.subspan(row * source_stride, width));
      }
    }
    frame->set_color_space(metadata.color_space);
    frame->set_hdr_metadata(metadata.hdr_metadata);
    if (metadata.duration != kNoTimestamp && !metadata.duration.is_zero()) {
      frame->metadata().frame_duration = metadata.duration;
    }
  }
  CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
  return frame;
}

}  // namespace media
