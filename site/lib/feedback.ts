"use server";

export interface Feedback {
  page: string;
  helpful: boolean;
  note?: string;
}

export async function sendFeedback({ page, helpful, note }: Feedback) {
  const key = process.env.NEXT_PUBLIC_POSTHOG_KEY;
  if (!key) return;
  if (!page.startsWith("/help/") || page.length > 200) return;

  await fetch("https://e.uwbrowser.com/i/v0/e/", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      api_key: key,
      event: "help_feedback",
      distinct_id: crypto.randomUUID(),
      properties: {
        page,
        helpful: helpful === true,
        note: note?.slice(0, 2000) || undefined,
        $process_person_profile: false,
      },
    }),
  });
}
