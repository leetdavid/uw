import Image from "next/image";

export function Folder({ color }: { color: string }) {
  return (
    <svg
      viewBox="0 0 24 24"
      fill={`color-mix(in srgb, ${color} 24%, transparent)`}
      stroke={color}
      strokeWidth="1.75"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      <path d="M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2Z" />
    </svg>
  );
}

export function Favicon({
  domain,
  size = 17,
}: {
  domain: string;
  size?: number;
}) {
  return (
    <Image src={`/icons/${domain}.png`} alt="" width={size} height={size} />
  );
}
