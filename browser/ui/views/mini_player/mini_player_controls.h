// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_CONTROLS_H_
#define UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_CONTROLS_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/view.h"

namespace gfx {
struct VectorIcon;
}

namespace uw {

// How glassy the controls are, from 0, flat, to 100.
using ControlsGlassOf = base::RepeatingCallback<int()>;

// Puts frosted glass behind `view`, a control over the video, as much as the
// theme's Glass slider asks for, clipped to corners of `radius`. The view
// paints its own fill over it with PaintControlFill().
void UpdateControlGlass(views::View* view, int glass, float radius);

// Fills `bounds` of a control over the video: black at 40% while glass is
// off, fading into lit dark glass as it goes up.
void PaintControlFill(gfx::Canvas* canvas,
                      const gfx::RectF& bounds,
                      float radius,
                      int glass,
                      std::optional<SkColor> color = std::nullopt);

// A round button over the video with a white icon.
class MiniPlayerButton : public views::Button {
  METADATA_HEADER(MiniPlayerButton, views::Button)

 public:
  MiniPlayerButton(PressedCallback callback,
                   const gfx::VectorIcon& icon,
                   std::u16string name,
                   ControlsGlassOf glass);
  MiniPlayerButton(const MiniPlayerButton&) = delete;
  MiniPlayerButton& operator=(const MiniPlayerButton&) = delete;
  ~MiniPlayerButton() override;

  void SetIcon(const gfx::VectorIcon& icon, std::u16string name);
  // How big the button and its icon are; the mini player's layout sets them.
  void SetSizes(int diameter, int icon_size);
  // Seek buttons say their seconds inside the icon.
  void set_seconds(std::u16string seconds) { seconds_ = std::move(seconds); }
  // A fill of its own, such as red for hang up, in place of the glass.
  void set_fill(SkColor fill) { fill_ = fill; }
  // Reads the glass again, as the controls show.
  void UpdateGlass();

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  raw_ref<const gfx::VectorIcon> icon_;
  const ControlsGlassOf glass_;
  std::u16string seconds_;
  std::optional<SkColor> fill_;
  int diameter_ = 24;
  int icon_size_ = 14;
};

// "Skip Ad", a pill over the video while an ad plays.
class SkipAdButton : public views::Button {
  METADATA_HEADER(SkipAdButton, views::Button)

 public:
  SkipAdButton(PressedCallback callback, ControlsGlassOf glass);
  SkipAdButton(const SkipAdButton&) = delete;
  SkipAdButton& operator=(const SkipAdButton&) = delete;
  ~SkipAdButton() override;

  void SetHeight(int height);
  void UpdateGlass();

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  const ControlsGlassOf glass_;
  int height_ = 22;
};

// The "2×" pill shown while the video is held.
class FastLabel : public views::View {
  METADATA_HEADER(FastLabel, views::View)

 public:
  explicit FastLabel(ControlsGlassOf glass);
  FastLabel(const FastLabel&) = delete;
  FastLabel& operator=(const FastLabel&) = delete;
  ~FastLabel() override;

  void SetHeight(int height);
  void UpdateGlass();

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  const ControlsGlassOf glass_;
  int height_ = 24;
};

// The progress bar along the bottom of the video: a track, the part played,
// and a knob that can be dragged to seek.
class MiniPlayerProgress : public views::View {
  METADATA_HEADER(MiniPlayerProgress, views::View)

 public:
  // Called with where the user dragged to, from 0 to 1.
  using SeekCallback = base::RepeatingCallback<void(double)>;

  explicit MiniPlayerProgress(SeekCallback seek);
  MiniPlayerProgress(const MiniPlayerProgress&) = delete;
  MiniPlayerProgress& operator=(const MiniPlayerProgress&) = delete;
  ~MiniPlayerProgress() override;

  // How far the video has played, from 0 to 1.
  void SetProgress(double progress);
  double progress() const { return progress_; }
  // The bar's thickness and its knob's diameter; the layout sets them.
  void SetSizes(int bar_height, int knob);
  // Hides the knob and stops seeking, as during an ad or while held at 2×.
  void SetSeekable(bool seekable);
  // An ad's bar is yellow.
  void SetAd(bool ad);
  bool dragging() const { return dragging_; }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;

 private:
  double ProgressAt(int x) const;

  const SeekCallback seek_;
  double progress_ = 0;
  int bar_height_ = 3;
  int knob_ = 9;
  bool seekable_ = true;
  bool ad_ = false;
  bool dragging_ = false;
};

// The shading behind the controls, darker at the top and the bottom.
class ControlsScrim : public views::View {
  METADATA_HEADER(ControlsScrim, views::View)

 public:
  ControlsScrim();
  ControlsScrim(const ControlsScrim&) = delete;
  ControlsScrim& operator=(const ControlsScrim&) = delete;
  ~ControlsScrim() override;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
};

// The mini player's 1px edge, an even `ring` all the way round.
class Rim : public views::View {
  METADATA_HEADER(Rim, views::View)

 public:
  Rim(float radius, SkColor ring);
  Rim(const Rim&) = delete;
  Rim& operator=(const Rim&) = delete;
  ~Rim() override;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  const float radius_;
  const SkColor ring_;
};

}  // namespace uw

#endif  // UW_BROWSER_UI_VIEWS_MINI_PLAYER_MINI_PLAYER_CONTROLS_H_
