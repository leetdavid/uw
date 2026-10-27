// Copyright 2026 The uw Authors. All rights reserved.

// uw plays H.264 and AAC through macOS's own decoders, VideoToolbox and
// AudioToolbox, and ships neither FFmpeg's decoders for them nor OpenH264.
// See docs/plans/distribution.md, "Codecs". The files are in test_data/.

#include <string>

#include "base/command_line.h"
#include "base/strings/strcat.h"
#include "chrome/browser/headless/test/headless_mode_browsertest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "media/base/media_switches.h"
#include "media/ffmpeg/ffmpeg_common.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
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
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("/page.html")));
  }

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
    EXPECT_EQ(true, Run(base::StrCat({"const audio = await play('audio', '",
                                      file,
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

IN_PROC_BROWSER_TEST_F(CodecsBrowserTest, H264PlaysAtEverySize) {
  // Under 360 pixels tall went to FFmpeg before uw dropped its decoder.
  for (const char* file : {"small.mp4", "large.mp4"}) {
    SCOPED_TRACE(file);
    EXPECT_EQ(true, Run(base::StrCat(
                        {"const video = await play('video', '", file,
                         "');"
                         "return video.getVideoPlaybackQuality()"
                         "    .totalVideoFrames > 30;"})));
  }
}

}  // namespace
}  // namespace uw
