// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/media/gpu_audio_decoder.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "media/base/audio_buffer.h"
#include "media/base/decoder_buffer.h"
#include "media/base/media_util.h"
#include "media/mojo/clients/mojo_audio_decoder.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace uw {

struct GpuAudioDecoder::Result {
  media::DecoderStatus status = media::DecoderStatus::Codes::kFailed;
  std::vector<scoped_refptr<media::AudioBuffer>> outputs;
};

// The GPU process's decoder, on a sequence of its own.
class GpuAudioDecoder::Remote {
 public:
  using Done = base::OnceCallback<void(Result)>;

  explicit Remote(mojo::PendingRemote<media::mojom::InterfaceFactory> factory)
      : factory_(std::move(factory)) {}

  void Initialize(const media::AudioDecoderConfig& config, Done done) {
    mojo::PendingRemote<media::mojom::AudioDecoder> decoder;
    factory_->CreateAudioDecoder(decoder.InitWithNewPipeAndPassReceiver());
    decoder_ = std::make_unique<media::MojoAudioDecoder>(
        base::SequencedTaskRunner::GetCurrentDefault(), &media_log_,
        std::move(decoder));
    decoder_->Initialize(
        config, /*cdm_context=*/nullptr, Finish(std::move(done)),
        base::BindRepeating(&Remote::OnOutput, base::Unretained(this)),
        base::DoNothing());
  }

  void Decode(scoped_refptr<media::DecoderBuffer> buffer, Done done) {
    decoder_->Decode(std::move(buffer), Finish(std::move(done)));
  }

  void Reset(Done done) {
    decoder_->Reset(
        base::BindOnce(Finish(std::move(done)),
                       media::DecoderStatus(media::DecoderStatus::Codes::kOk)));
  }

 private:
  // Hands `done` the status and the buffers decoded since the last call.
  base::OnceCallback<void(media::DecoderStatus)> Finish(Done done) {
    return base::BindOnce(
        [](Remote* remote, Done done, media::DecoderStatus status) {
          std::move(done).Run(
              {std::move(status), std::exchange(remote->outputs_, {})});
        },
        base::Unretained(this), std::move(done));
  }

  void OnOutput(scoped_refptr<media::AudioBuffer> buffer) {
    outputs_.push_back(std::move(buffer));
  }

  mojo::Remote<media::mojom::InterfaceFactory> factory_;
  media::NullMediaLog media_log_;
  std::unique_ptr<media::MojoAudioDecoder> decoder_;
  std::vector<scoped_refptr<media::AudioBuffer>> outputs_;
};

GpuAudioDecoder::GpuAudioDecoder(
    mojo::PendingRemote<media::mojom::InterfaceFactory> factory)
    : remote_(base::ThreadPool::CreateSequencedTaskRunner({}),
              std::move(factory)) {}

GpuAudioDecoder::~GpuAudioDecoder() = default;

template <typename Method, typename... Args>
GpuAudioDecoder::Result GpuAudioDecoder::Wait(Method method, Args&&... args) {
  base::WaitableEvent event;
  Result result;
  remote_.AsyncCall(method).WithArgs(
      std::forward<Args>(args)...,
      base::BindOnce(
          [](base::WaitableEvent* event, Result* out, Result result) {
            *out = std::move(result);
            event->Signal();
          },
          base::Unretained(&event), base::Unretained(&result)));
  event.Wait();
  for (auto& output : result.outputs) {
    output_cb_.Run(std::move(output));
  }
  result.outputs.clear();
  return result;
}

media::AudioDecoderType GpuAudioDecoder::GetDecoderType() const {
  return media::AudioDecoderType::kMojo;
}

void GpuAudioDecoder::Initialize(const media::AudioDecoderConfig& config,
                                 media::CdmContext* cdm_context,
                                 InitCB init_cb,
                                 const OutputCB& output_cb,
                                 const media::WaitingCB& waiting_cb) {
  output_cb_ = output_cb;
  std::move(init_cb).Run(Wait(&Remote::Initialize, config).status);
}

void GpuAudioDecoder::Decode(scoped_refptr<media::DecoderBuffer> buffer,
                             DecodeCB decode_cb) {
  std::move(decode_cb).Run(Wait(&Remote::Decode, std::move(buffer)).status);
}

void GpuAudioDecoder::Reset(base::OnceClosure reset_cb) {
  Wait(&Remote::Reset);
  std::move(reset_cb).Run();
}

}  // namespace uw
