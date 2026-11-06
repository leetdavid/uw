// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_MEDIA_GPU_AUDIO_DECODER_H_
#define UW_COMPONENTS_MEDIA_GPU_AUDIO_DECODER_H_

#include "base/threading/sequence_bound.h"
#include "media/base/audio_decoder.h"
#include "media/mojo/mojom/interface_factory.mojom.h"
#include "mojo/public/cpp/bindings/pending_remote.h"

namespace uw {

// Decodes audio in the GPU process and runs every callback before its call
// returns, as media::AudioFileReader needs for Web Audio's decodeAudioData.
// On Windows the page's sandbox keeps Media Foundation out of its process,
// while the GPU process decodes the page's other AAC with it. Blocks the
// calling thread, which must allow waiting. See docs/plans/distribution.md,
// "Codecs".
class GpuAudioDecoder : public media::AudioDecoder {
 public:
  explicit GpuAudioDecoder(
      mojo::PendingRemote<media::mojom::InterfaceFactory> factory);
  GpuAudioDecoder(const GpuAudioDecoder&) = delete;
  GpuAudioDecoder& operator=(const GpuAudioDecoder&) = delete;
  ~GpuAudioDecoder() override;

  // media::AudioDecoder:
  media::AudioDecoderType GetDecoderType() const override;
  void Initialize(const media::AudioDecoderConfig& config,
                  media::CdmContext* cdm_context,
                  InitCB init_cb,
                  const OutputCB& output_cb,
                  const media::WaitingCB& waiting_cb) override;
  void Decode(scoped_refptr<media::DecoderBuffer> buffer,
              DecodeCB decode_cb) override;
  void Reset(base::OnceClosure reset_cb) override;

 private:
  class Remote;
  struct Result;

  // Calls `method` on the GPU process's decoder with `args`, waits for its
  // result, and hands on the buffers it decoded.
  template <typename Method, typename... Args>
  Result Wait(Method method, Args&&... args);

  base::SequenceBound<Remote> remote_;
  OutputCB output_cb_;
};

}  // namespace uw

#endif  // UW_COMPONENTS_MEDIA_GPU_AUDIO_DECODER_H_
