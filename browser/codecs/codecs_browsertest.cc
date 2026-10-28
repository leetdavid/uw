// Copyright 2026 The uw Authors. All rights reserved.

// uw plays H.264 and AAC through macOS's own decoders, VideoToolbox and
// AudioToolbox, and ships neither FFmpeg's decoders for them nor OpenH264.
// See docs/plans/distribution.md, "Codecs". The files are in test_data/.

#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "media/base/media_switches.h"
#include "media/ffmpeg/ffmpeg_common.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace uw {
namespace {

// Plays a file muted to its end, at four times the speed, and says what the
// page saw.
constexpr char kPlay[] = R"(
  const play = (tag, file) => new Promise((resolve, reject) => {
    const media = document.createElement(tag);
    media.muted = true;
    media.playbackRate = 4;
    media.onended = () => resolve(media);
    media.onerror = () => reject(new Error(`${file}: ${media.error?.code} ` +
                                           `${media.error?.message}`));
    media.src = file;
    document.body.append(media);
    media.play().catch(reject);
  });
)";

// A radio station: `tone` over and over, with no length, in small pieces,
// starting partway through a frame as a station's stream can.
class Station : public net::test_server::HttpResponse {
 public:
  explicit Station(std::string tone) : tone_(std::move(tone)) {}

  void SendResponse(
      base::WeakPtr<net::test_server::HttpResponseDelegate> delegate) override {
    delegate_ = delegate;
    delegate_->SendResponseHeaders(
        net::HTTP_OK, "OK",
        {{"Content-Type", "audio/aac"}, {"Cache-Control", "no-store"}});
    Send();
  }

 private:
  void Send() {
    if (!delegate_) {
      return;
    }
    constexpr size_t kPiece = 2048;
    std::string piece;
    while (piece.size() < kPiece) {
      piece += tone_.substr(at_, kPiece - piece.size());
      at_ = (at_ + piece.size()) % tone_.size();
    }
    delegate_->SendContents(
        piece,
        base::BindOnce(
            [](base::WeakPtr<Station> station) {
              if (station) {
                base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
                    FROM_HERE, base::BindOnce(&Station::Send, station),
                    base::Milliseconds(50));
              }
            },
            weak_factory_.GetWeakPtr()));
  }

  const std::string tone_;
  size_t at_ = 3;
  base::WeakPtr<net::test_server::HttpResponseDelegate> delegate_;
  base::WeakPtrFactory<Station> weak_factory_{this};
};

class CodecsBrowserTest : public headless::HeadlessModeBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    HeadlessModeBrowserTest::SetUpCommandLine(command_line);
    // Tests never make a sound.
    command_line->AppendSwitch(switches::kMuteAudio);
    command_line->AppendSwitchASCII(
        switches::kAutoplayPolicy,
        switches::autoplay::kNoUserGestureRequiredPolicy);
  }

  void SetUpOnMainThread() override {
    HeadlessModeBrowserTest::SetUpOnMainThread();
    embedded_test_server()->ServeFilesFromSourceDirectory(
        "uw/browser/codecs/test_data");
    std::string tone;
    {
      base::ScopedAllowBlockingForTesting blocking;
      base::FilePath source;
      ASSERT_TRUE(
          base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &source));
      ASSERT_TRUE(base::ReadFileToString(
          source.AppendASCII("uw/browser/codecs/test_data/tone.aac"), &tone));
      // Ten minutes of the tone, too long to hold whole.
      ASSERT_TRUE(long_files_.CreateUniqueTempDir());
      ASSERT_TRUE(base::WriteFile(
          long_files_.GetPath().AppendASCII("long.aac"),
          base::JoinString(std::vector<std::string>(300, tone), "")));
    }
    embedded_test_server()->ServeFilesFromDirectory(long_files_.GetPath());
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        [](const std::string& tone,
           const net::test_server::HttpRequest& request)
            -> std::unique_ptr<net::test_server::HttpResponse> {
          if (request.relative_url != "/station") {
            return nullptr;
          }
          return std::make_unique<Station>(tone);
        },
        tone));
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("/page.html")));
  }

  base::ScopedTempDir long_files_;

  content::EvalJsResult Run(const std::string& body) {
    return content::EvalJs(
        browser()->tab_strip_model()->GetActiveWebContents(),
        base::StrCat({"(async () => {", kPlay, body, "})()"}));
  }
};

IN_PROC_BROWSER_TEST_F(CodecsBrowserTest, FFmpegCannotDecodeH264OrAac) {
  EXPECT_EQ(avcodec_find_decoder(AV_CODEC_ID_H264), nullptr);
  EXPECT_EQ(avcodec_find_decoder(AV_CODEC_ID_AAC), nullptr);
  // It still reads MP4 files, which macOS's decoders then play.
  EXPECT_NE(av_find_input_format("mov,mp4,m4a,3gp,3g2,mj2"), nullptr);
}

IN_PROC_BROWSER_TEST_F(CodecsBrowserTest, AacPlaysInEachProfile) {
  for (const char* file : {"lc.m4a", "he.m4a", "he_v2.m4a"}) {
    SCOPED_TRACE(file);
    EXPECT_EQ(true,
              Run(base::StrCat({"const audio = await play('audio', '", file,
                                "');"
                                "return audio.webkitAudioDecodedByteCount"
                                "    > 0 && audio.duration > 1.9;"})));
  }
}

IN_PROC_BROWSER_TEST_F(CodecsBrowserTest, MediaSourcePlaysAac) {
  EXPECT_EQ(true, Run(R"(
    const type = 'audio/mp4; codecs="mp4a.40.2"';
    if (!MediaSource.isTypeSupported(type)) {
      return 'unsupported';
    }
    const source = new MediaSource();
    const audio = document.createElement('audio');
    audio.muted = true;
    audio.playbackRate = 4;
    audio.src = URL.createObjectURL(source);
    document.body.append(audio);
    await new Promise((resolve) => source.onsourceopen = resolve);
    const buffer = source.addSourceBuffer(type);
    const bytes = await (await fetch('mse.mp4')).arrayBuffer();
    buffer.appendBuffer(bytes);
    await new Promise((resolve) => buffer.onupdateend = resolve);
    source.endOfStream();
    await audio.play();
    await new Promise((resolve, reject) => {
      audio.onended = resolve;
      audio.onerror = () => reject(new Error(`${audio.error?.code}`));
    });
    return audio.webkitAudioDecodedByteCount > 0;
  )"));
}

// AAC as ADTS frames, with no description, as a page that reads a .aac file
// itself hands it over.
IN_PROC_BROWSER_TEST_F(CodecsBrowserTest, WebCodecsDecodesAac) {
  EXPECT_EQ("44100 2 true", Run(R"(
    const bytes = new Uint8Array(await (await fetch('tone.aac')).arrayBuffer());
    let frames = 0;
    let format = '';
    const decoder = new AudioDecoder({
      output: (data) => {
        frames += data.numberOfFrames;
        format = `${data.sampleRate} ${data.numberOfChannels}`;
        data.close();
      },
      error: (error) => { format = error.message; },
    });
    decoder.configure(
        {codec: 'mp4a.40.2', sampleRate: 44100, numberOfChannels: 2});
    let timestamp = 0;
    for (let at = 0; at + 7 <= bytes.length;) {
      const length = (bytes[at + 3] & 3) << 11 | bytes[at + 4] << 3 |
                     bytes[at + 5] >> 5;
      decoder.decode(new EncodedAudioChunk({
        type: 'key',
        timestamp,
        data: bytes.subarray(at, at + length),
      }));
      timestamp += 1024 / 44100 * 1e6;
      at += length;
    }
    await decoder.flush();
    return `${format} ${frames > 44100}`;
  )"));
}

IN_PROC_BROWSER_TEST_F(CodecsBrowserTest, WebAudioDecodesAac) {
  EXPECT_EQ("44100 2 true", Run(R"(
    const bytes = await (await fetch('lc.m4a')).arrayBuffer();
    const context = new OfflineAudioContext(2, 44100, 44100);
    const audio = await context.decodeAudioData(bytes);
    return `${audio.sampleRate} ${audio.numberOfChannels} ` +
           `${audio.duration > 1.9}`;
  )"));
}

// FFmpeg in uw has no AAC demuxer, so raw ADTS plays on uw's ADTS engine.
IN_PROC_BROWSER_TEST_F(CodecsBrowserTest, RawAacPlays) {
  EXPECT_EQ(true, Run("const audio = await play('audio', 'tone.aac');"
                      "return audio.webkitAudioDecodedByteCount > 0 &&"
                      "    audio.duration > 1.9 && audio.duration < 2.2;"));
}

// A station can't seek, so it plays and resumes from a pause only because
// the engine seeks to the audio it holds.
IN_PROC_BROWSER_TEST_F(CodecsBrowserTest, AnAacStationPlaysAndResumes) {
  EXPECT_EQ("true true", Run(R"(
    const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
    const audio = document.createElement('audio');
    audio.muted = true;
    audio.src = 'station';
    document.body.append(audio);
    await audio.play();
    await wait(2000);
    const played = audio.currentTime;
    audio.pause();
    await wait(1000);
    await audio.play();
    await wait(2000);
    const result = `${played > 1} ${audio.currentTime > played + 1}`;
    audio.pause();
    audio.src = '';
    return result;
  )"));
}

// ADTS has no timestamps, so a seek away from the buffered audio reads the
// file again from where its bitrate says the time is.
IN_PROC_BROWSER_TEST_F(CodecsBrowserTest, ALongRawAacFileSeeks) {
  EXPECT_EQ("true true true", Run(R"(
    const audio = document.createElement('audio');
    audio.muted = true;
    audio.src = 'long.aac';
    document.body.append(audio);
    await audio.play();
    const playsAfterSeekingTo = async (time) => {
      audio.currentTime = time;
      await new Promise((resolve) => audio.onseeked = resolve);
      const seeked = audio.currentTime;
      await new Promise((resolve) => setTimeout(resolve, 1000));
      return audio.currentTime > seeked + 0.5;
    };
    const result = `${await playsAfterSeekingTo(500)} ` +
                   `${await playsAfterSeekingTo(5)} ` +
                   `${audio.duration > 570 && audio.duration < 630}`;
    audio.pause();
    audio.src = '';
    return result;
  )"));
}

IN_PROC_BROWSER_TEST_F(CodecsBrowserTest, H264PlaysAtEverySize) {
  // Under 360 pixels tall went to FFmpeg before uw dropped its decoder.
  for (const char* file : {"small.mp4", "large.mp4"}) {
    SCOPED_TRACE(file);
    EXPECT_EQ(true,
              Run(base::StrCat({"const video = await play('video', '", file,
                                "');"
                                "return video.getVideoPlaybackQuality()"
                                "    .totalVideoFrames > 30;"})));
  }
}

// With accelerated video decoding off, as after a GPU crash loop, H.264 still
// plays: VideoToolbox decodes it in software in the GPU process, and frames are
// copied for the page.
class CodecsWithoutAcceleratedDecodingBrowserTest : public CodecsBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    CodecsBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(switches::kDisableAcceleratedVideoDecode);
  }
};

IN_PROC_BROWSER_TEST_F(CodecsWithoutAcceleratedDecodingBrowserTest,
                       H264PlaysAtEverySize) {
  for (const char* file : {"small.mp4", "large.mp4"}) {
    SCOPED_TRACE(file);
    EXPECT_EQ(true,
              Run(base::StrCat({"const video = await play('video', '", file,
                                "');"
                                "return video.getVideoPlaybackQuality()"
                                "    .totalVideoFrames > 30;"})));
  }
}

}  // namespace
}  // namespace uw
