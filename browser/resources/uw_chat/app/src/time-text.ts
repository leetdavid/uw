// Times and dates as the pages say them. Each page adds its own words, such
// as "Today" or "tomorrow at".

// A time of day, such as "9:02 AM".
export function clockText(ms: number): string {
  return new Date(ms).toLocaleTimeString([], {
    hour: "numeric",
    minute: "2-digit",
  });
}

// A date, such as "Oct 24".
export function dateText(ms: number): string {
  return new Date(ms).toLocaleDateString([], {
    month: "short",
    day: "numeric",
  });
}

// How many days after today `ms` falls: 0 today, 1 tomorrow, -1 yesterday.
export function daysFromToday(ms: number): number {
  const day = new Date(ms).setHours(0, 0, 0, 0);
  return Math.round((day - new Date().setHours(0, 0, 0, 0)) / 86_400_000);
}
