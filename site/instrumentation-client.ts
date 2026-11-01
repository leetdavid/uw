import posthog from "posthog-js";

const key = process.env.NEXT_PUBLIC_POSTHOG_KEY;
const declined =
  navigator.doNotTrack === "1" ||
  ("globalPrivacyControl" in navigator && navigator.globalPrivacyControl);

if (key && !declined) {
  posthog.init(key, {
    api_host: "https://e.uwbrowser.com",
    ui_host: "https://us.posthog.com",
    defaults: "2026-08-30",
    cookieless_mode: "always",
    autocapture: false,
    enable_heatmaps: false,
    disable_session_recording: true,
    disable_external_dependency_loading: true,
    advanced_disable_flags: true,
  });

  addEventListener("click", ({ target }) => {
    const control =
      target instanceof Element
        ? target.closest<HTMLElement>("[data-event]")
        : null;
    if (control?.dataset.event) {
      posthog.capture(control.dataset.event, { place: control.dataset.place });
    }
  });
}
