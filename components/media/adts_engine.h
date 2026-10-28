// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_MEDIA_ADTS_ENGINE_H_
#define UW_COMPONENTS_MEDIA_ADTS_ENGINE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "base/time/time.h"
#include "media/base/data_source.h"
#include "media/filters/hls_data_source_provider.h"
#include "media/filters/manifest_demuxer.h"
#include "url/gurl.h"

namespace media {

// Plays a raw ADTS AAC file, or an endless ADTS radio stream, through
// ChunkDemuxer's ADTS parser, since uw's FFmpeg has no AAC demuxer. The
// demuxer manager switches to it when FFmpeg's open spots ADTS. It reads the
// address a chunk at a time, keeping about ten seconds ahead of playback. See
// uw's docs/plans/distribution.md, "Codecs".
class AdtsEngine : public ManifestDemuxer::Engine, public DataSourceInfo {
public:
  // `size` is the file's length in bytes, or none for a stream with no end,
  // such as a radio station, which can't seek.
  AdtsEngine(base::SequenceBound<HlsDataSourceProvider> provider, GURL url,
             bool would_taint_origin, std::optional<int64_t> size);
  AdtsEngine(const AdtsEngine &) = delete;
  AdtsEngine &operator=(const AdtsEngine &) = delete;
  ~AdtsEngine() override;

  // DataSourceInfo:
  int64_t GetMemoryUsage() override;
  bool WouldTaintOrigin() const override;
  bool IsStreaming() const override;

  // ManifestDemuxer::Engine:
  std::string GetName() const override;
  void Initialize(ManifestDemuxerEngineHost *host,
                  PipelineStatusCallback status_cb) override;
  void OnTimeUpdate(base::TimeDelta time, double playback_rate,
                    ManifestDemuxer::DelayCallback cb) override;
  void Seek(base::TimeDelta time, ManifestDemuxer::SeekCallback cb) override;
  void StartWaitingForSeek() override;
  void AbortPendingReads(base::OnceClosure cb) override;
  bool IsSeekable() const override;
  int64_t GetMemoryUsage() const override;
  void Stop() override;
  void SelectAudioTrack(const MediaTrack::Id &track_id) override;
  void SelectVideoTrack(const MediaTrack::Id &track_id) override;
  std::vector<raw_ptr<DemuxerStream>>
  FilterDemuxerStreams(std::vector<raw_ptr<DemuxerStream>> &&streams) override;

private:
  // How far past `time` the buffered audio reaches.
  base::TimeDelta BufferedAhead(base::TimeDelta time);
  void Read(ManifestDemuxer::DelayCallback cb);
  void OnRead(ManifestDemuxer::DelayCallback cb,
              HlsDataSourceProvider::ReadResult result);

  base::SequenceBound<HlsDataSourceProvider> provider_;
  const GURL url_;
  const bool would_taint_origin_;
  const std::optional<int64_t> size_;
  raw_ptr<ManifestDemuxerEngineHost> host_ = nullptr;
  std::unique_ptr<HlsDataSourceStream> stream_;
  bool started_ = false;
  bool ended_ = false;
  // A station has not played since it started or last paused.
  bool paused_ = true;
  base::TimeDelta time_;
  base::TimeDelta parse_offset_;
  // A file's bytes a second, from its first chunk, to find where a seek
  // starts reading.
  double bytes_per_second_ = 0;
  // Where in the file the next connection starts reading.
  int64_t start_ = 0;
  base::WeakPtrFactory<AdtsEngine> weak_factory_{this};
};

} // namespace media

#endif // UW_COMPONENTS_MEDIA_ADTS_ENGINE_H_
