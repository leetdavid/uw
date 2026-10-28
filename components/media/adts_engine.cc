// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/components/media/adts_engine.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/task/bind_post_task.h"
#include "media/base/pipeline_status.h"
#include "media/base/stream_parser.h"
#include "media/base/timestamp_constants.h"
#include "media/filters/stream_parser_factory.h"
#include "media/formats/hls/types.h"

namespace media {
namespace {

constexpr char kRole[] = "adts";
// Enough to ride out a slow network without holding much of an endless
// stream in memory.
constexpr base::TimeDelta kAhead = base::Seconds(10);
constexpr base::TimeDelta kWhileFull = base::Seconds(1);

}  // namespace

AdtsEngine::AdtsEngine(base::SequenceBound<HlsDataSourceProvider> provider,
                       GURL url,
                       bool would_taint_origin,
                       std::optional<int64_t> size)
    : provider_(std::move(provider)),
      url_(std::move(url)),
      would_taint_origin_(would_taint_origin),
      size_(size) {}

AdtsEngine::~AdtsEngine() = default;

int64_t AdtsEngine::GetMemoryUsage() {
  return stream_ ? static_cast<int64_t>(stream_->memory_usage()) : 0;
}

int64_t AdtsEngine::GetMemoryUsage() const {
  return stream_ ? static_cast<int64_t>(stream_->memory_usage()) : 0;
}

bool AdtsEngine::WouldTaintOrigin() const {
  return would_taint_origin_;
}

bool AdtsEngine::IsStreaming() const {
  return !size_;
}

std::string AdtsEngine::GetName() const {
  return "AdtsEngine";
}

void AdtsEngine::Initialize(ManifestDemuxerEngineHost* host,
                            PipelineStatusCallback status_cb) {
  host_ = host;
  std::move(status_cb).Run(OkStatus());
}

void AdtsEngine::OnTimeUpdate(base::TimeDelta time,
                              double playback_rate,
                              ManifestDemuxer::DelayCallback cb) {
  time_ = time;
  // The first event comes once ChunkDemuxer is open.
  if (!started_) {
    started_ = true;
    if (!host_->AddRole(kRole, RelaxedParserSupportedType::kAAC)) {
      host_->OnError(DEMUXER_ERROR_COULD_NOT_OPEN);
      std::move(cb).Run(kNoTimestamp);
      return;
    }
  }
  // The player never seeks a stream it can't seek, so a station seeks itself,
  // as live HLS does: once playback starts, and again after each pause, to
  // the audio it holds.
  if (!size_ && playback_rate == 0) {
    paused_ = true;
  } else if (!size_ && paused_) {
    Ranges<base::TimeDelta> ranges = host_->GetBufferedRanges(kRole);
    if (ranges.size()) {
      paused_ = false;
      host_->RequestSeek(BufferedAhead(time).is_positive() ? time
                                                           : ranges.start(0));
      std::move(cb).Run(kNoTimestamp);
      return;
    }
  }
  if (ended_) {
    std::move(cb).Run(kNoTimestamp);
    return;
  }
  if (BufferedAhead(time) >= kAhead) {
    std::move(cb).Run(kWhileFull);
    return;
  }
  // A seek that needs audio gets this one event to buffer it, so the event
  // lasts until the read is in.
  Read(std::move(cb));
}

void AdtsEngine::Seek(base::TimeDelta time, ManifestDemuxer::SeekCallback cb) {
  time_ = time;
  if (BufferedAhead(time).is_positive() || (!size_ && ended_)) {
    std::move(cb).Run(ManifestDemuxer::SeekState::kIsReady);
    return;
  }
  if (size_) {
    // ADTS has no timestamps, so a file reads again from where `time` should
    // be by its bitrate, as FFmpeg seeks raw AAC. ChunkDemuxer's parser skips
    // to the next whole frame.
    start_ = std::clamp<int64_t>(time.InSecondsF() * bytes_per_second_, 0,
                                 *size_ - 1);
    stream_.reset();
    provider_.AsyncCall(&HlsDataSourceProvider::AbortPendingReads)
        .WithArgs(base::DoNothing());
    host_->RemoveAndReset(kRole, base::TimeDelta(), kInfiniteDuration,
                          &parse_offset_);
    parse_offset_ = start_ ? time : base::TimeDelta();
    if (ended_) {
      ended_ = false;
      host_->UnsetEndOfStream();
    }
  }
  std::move(cb).Run(ManifestDemuxer::SeekState::kNeedsData);
}

void AdtsEngine::StartWaitingForSeek() {}

// The provider's abort stops the connection the engine reads through for good,
// and a read only ever runs inside an event, which a seek waits for.
void AdtsEngine::AbortPendingReads(base::OnceClosure cb) {
  std::move(cb).Run();
}

bool AdtsEngine::IsSeekable() const {
  return size_.has_value();
}

void AdtsEngine::Stop() {
  weak_factory_.InvalidateWeakPtrs();
  stream_.reset();
  host_ = nullptr;
}

void AdtsEngine::SelectAudioTrack(const MediaTrack::Id& track_id) {}

void AdtsEngine::SelectVideoTrack(const MediaTrack::Id& track_id) {}

std::vector<raw_ptr<DemuxerStream>> AdtsEngine::FilterDemuxerStreams(
    std::vector<raw_ptr<DemuxerStream>>&& streams) {
  return std::move(streams);
}

base::TimeDelta AdtsEngine::BufferedAhead(base::TimeDelta time) {
  Ranges<base::TimeDelta> ranges = host_->GetBufferedRanges(kRole);
  for (size_t i = 0; i < ranges.size(); ++i) {
    if (ranges.start(i) <= time && time < ranges.end(i)) {
      return ranges.end(i) - time;
    }
  }
  return base::TimeDelta();
}

void AdtsEngine::Read(ManifestDemuxer::DelayCallback cb) {
  auto on_read = base::BindPostTaskToCurrentDefault(base::BindOnce(
      &AdtsEngine::OnRead, weak_factory_.GetWeakPtr(), std::move(cb)));
  if (stream_) {
    provider_.AsyncCall(&HlsDataSourceProvider::ReadFromExistingStream)
        .WithArgs(std::move(stream_), std::move(on_read));
    return;
  }
  // A station's bytes are always new; a file's may already be cached from
  // FFmpeg's look at it.
  provider_.AsyncCall(&HlsDataSourceProvider::ReadFromUrl)
      .WithArgs(
          HlsDataSourceProvider::UrlDataSegment{
              url_,
              start_ ? hls::types::ByteRange::Validate(*size_ - start_, start_)
                     : std::nullopt,
              size_ ? DataSource::CacheMode::kHitCache
                    : DataSource::CacheMode::kBypassCache},
          std::move(on_read));
}

void AdtsEngine::OnRead(ManifestDemuxer::DelayCallback cb,
                        HlsDataSourceProvider::ReadResult result) {
  if (!result.has_value()) {
    host_->OnError(PIPELINE_ERROR_NETWORK);
    std::move(cb).Run(kNoTimestamp);
    return;
  }
  stream_ = std::move(result).value();
  if (stream_->buffer_size()) {
    host_->EvictCodedFrames(kRole, time_, stream_->buffer_size());
    if (!host_->AppendAndParseData(kRole, kInfiniteDuration, &parse_offset_,
                                   stream_->data())) {
      host_->OnError(CHUNK_DEMUXER_ERROR_APPEND_FAILED);
      std::move(cb).Run(kNoTimestamp);
      return;
    }
    stream_->Clear();
    if (size_ && !start_ && !bytes_per_second_ && parse_offset_.is_positive()) {
      bytes_per_second_ = stream_->read_position() / parse_offset_.InSecondsF();
      host_->SetDuration(*size_ / bytes_per_second_);
    }
  }
  if (!stream_->CanReadMore()) {
    ended_ = true;
    stream_.reset();
    host_->SetEndOfStream();
  }
  if (!ended_ && !BufferedAhead(time_).is_positive()) {
    Read(std::move(cb));
    return;
  }
  std::move(cb).Run(BufferedAhead(time_) < kAhead ? base::TimeDelta()
                                                  : kWhileFull);
}

}  // namespace media
