"use client";

import { Check, ThumbsDown, ThumbsUp } from "lucide-react";
import { useState } from "react";
import { sendFeedback } from "@/lib/feedback";
import { repo } from "@/lib/shared";

export function Feedback({ page }: { page: string }) {
  const [state, setState] = useState<"ask" | "note" | "yes" | "sent">("ask");
  const [note, setNote] = useState("");

  if (state === "yes" || state === "sent") {
    return (
      <div className="help-feedback done">
        <span className="check">
          <Check />
        </span>
        <p>
          <b>
            Thanks,{" "}
            <em>{state === "yes" ? "glad it helped" : "it's on its way"}</em>
          </b>
          {state === "sent" && (
            <>
              For a bug, <a href={`${repo}/issues/new`}>open an issue</a> so you
              can follow it.
            </>
          )}
        </p>
      </div>
    );
  }

  return (
    <div className="help-feedback">
      <div className="ask">
        <h5>
          Was this <em>helpful?</em>
        </h5>
        <div className="choices">
          <button
            type="button"
            onClick={() => {
              setState("yes");
              void sendFeedback({ page, helpful: true });
            }}
          >
            <ThumbsUp />
            Yes
          </button>
          <button
            type="button"
            data-on={state === "note"}
            onClick={() => setState("note")}
          >
            <ThumbsDown />
            No
          </button>
        </div>
      </div>
      {state === "note" && (
        <form
          action={() => {
            setState("sent");
            void sendFeedback({ page, helpful: false, note });
          }}
        >
          <textarea
            value={note}
            onChange={(event) => setNote(event.target.value)}
            maxLength={2000}
            placeholder="What was missing or wrong? (optional)"
            aria-label="What was missing or wrong?"
          />
          <div className="send">
            <small>
              Your answer and note go to uw's team, with this page's address.
            </small>
            <button type="submit">Send</button>
          </div>
        </form>
      )}
    </div>
  );
}
