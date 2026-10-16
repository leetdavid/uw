// The chat's approval mode, beside its model and effort: Ask first, Auto, or
// Full auto. See the agent chats plan's agreed scope.
import { ChevronDown, Shield, ShieldCheck, Zap } from "lucide-react";
import {
  DropdownContent,
  DropdownMenu,
  DropdownTrigger,
} from "@/components/ui/dropdown";
import { MenuItem } from "@/components/ui/menu-item";
import { cn } from "@/lib/utils";
import { ApprovalMode } from "./chat-api";
import { CHIP } from "./providers/model-chip";

const MODES = [
  {
    mode: ApprovalMode.kAskFirst,
    name: "Ask first",
    note: "Asks every step",
    icon: ShieldCheck,
  },
  { mode: ApprovalMode.kAuto, name: "Auto", note: "Asks for changes", icon: Shield },
  { mode: ApprovalMode.kFullAuto, name: "Full auto", note: "Never asks", icon: Zap },
];

export function ModeChip({
  mode,
  className,
  onPick,
}: {
  mode: ApprovalMode;
  className?: string;
  onPick: (mode: ApprovalMode) => void;
}) {
  const checkedIndex = Math.max(
    0,
    MODES.findIndex((each) => each.mode === mode),
  );
  const { name, icon: Icon } = MODES[checkedIndex]!;
  const full = mode === ApprovalMode.kFullAuto;
  return (
    <DropdownMenu>
      <DropdownTrigger
        render={
          <button
            type="button"
            aria-label="Approval mode"
            className={cn(CHIP, full && "bg-warning/15 text-warning", className)}
          >
            <Icon
              aria-hidden
              className={cn("size-3.5", !full && "text-muted-foreground")}
            />
            <span className="truncate">{name}</span>
            <ChevronDown aria-hidden className="size-3 text-muted-foreground" />
          </button>
        }
      />
      <DropdownContent checkedIndex={checkedIndex} className="w-[236px]">
        {MODES.map((each, index) => (
          <MenuItem
            key={each.name}
            index={index}
            label={each.name}
            checked={index === checkedIndex}
            data-note={each.note}
            className="after:ml-auto after:pl-2 after:text-[11.5px] after:text-muted-foreground after:content-[attr(data-note)]"
            onSelect={() => onPick(each.mode)}
          />
        ))}
      </DropdownContent>
    </DropdownMenu>
  );
}
