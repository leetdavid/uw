// The color of what an alert, or a row about a chat, says first, by its words.
// Other words, such as "Update" and "Answered", take the text's color.
export const KIND_COLORS: Record<string, string> = {
  "Change alert": "text-primary",
  "No change": "text-muted-foreground",
  "Couldn't check": "text-destructive",
  "Didn't finish": "text-destructive",
  Paused: "text-destructive",
  "Sign in required": "text-[#B4580A] dark:text-warning",
  "Waits for your approval": "text-[#B4580A] dark:text-warning",
  "Waits for your password": "text-[#B4580A] dark:text-warning",
};
