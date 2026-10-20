// uw://ai/skills: every skill the agent can use, each with a switch. Turning
// one off keeps it from every chat until it is turned on again. See the frame
// "AI page: skills (Fluid)" and docs/plans/connectors.md.
import { useEffect, useState } from "react";
import { Bot, FolderOpen, User } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Switch } from "@/components/ui/switch";
import { Tooltip } from "@/components/ui/tooltip";
import { cn } from "@/lib/utils";
import { SkillSource, providersHandler, type SkillInfo } from "./providers-api";

const SOURCES: { source: SkillSource | null; label: string }[] = [
  { source: null, label: "All" },
  { source: SkillSource.kUw, label: "uw" },
  { source: SkillSource.kAgents, label: "~/.agents/skills" },
  { source: SkillSource.kChat, label: "Made in chats" },
];

const SOURCE_NAMES: Record<SkillSource, string> = {
  [SkillSource.kUw]: "uw",
  [SkillSource.kAgents]: "~/.agents/skills",
  [SkillSource.kChat]: "Made in a chat",
};

// Who may start a skill, when it is not both the agent and the user.
function WhoStarts({ skill }: { skill: SkillInfo }) {
  if (skill.forAgent === skill.forUser) {
    return null;
  }
  if (skill.forUser) {
    return (
      <Tooltip
        content={`The agent can't use this skill on its own. Type /${skill.name} to start it.`}
      >
        <Badge data-who="you" color="blue" className="gap-1">
          <User aria-hidden className="size-3" />
          Only you
        </Badge>
      </Tooltip>
    );
  }
  return (
    <Badge data-who="agent" className="gap-1">
      <Bot aria-hidden className="size-3" />
      Only the agent
    </Badge>
  );
}

// `onCount` hears how many skills are on, for the section list.
export function SkillsSection({ onCount }: { onCount: (on: number) => void }) {
  const [skills, setSkills] = useState<SkillInfo[] | null>(null);
  const [folder, setFolder] = useState("");
  const [query, setQuery] = useState("");
  const [source, setSource] = useState<SkillSource | null>(null);

  useEffect(() => {
    providersHandler.getSkills().then((answer) => {
      setSkills(answer.skills);
      setFolder(answer.folder);
    });
  }, []);
  const on = skills?.filter((skill) => skill.on).length ?? 0;
  useEffect(() => {
    if (skills) {
      onCount(on);
    }
  }, [skills, on, onCount]);

  if (!skills) {
    return null;
  }
  const toggle = (skill: SkillInfo) => {
    providersHandler.setSkillOn(skill.name, !skill.on);
    setSkills(
      skills.map((kept) =>
        kept.name === skill.name ? { ...kept, on: !kept.on } : kept,
      ),
    );
  };
  const wanted = query.trim().toLowerCase();
  const shown = skills.filter(
    (skill) =>
      (source === null || skill.source === source) &&
      `${skill.name} ${skill.description}`.toLowerCase().includes(wanted),
  );
  return (
    <div className="flex flex-col gap-4">
      <div className="flex items-center gap-2.5">
        <h1 className="text-[20px] font-semibold tracking-[-0.01em]">Skills</h1>
        <span className="pt-0.5 text-muted-foreground">
          {on} on, {skills.length - on} off
        </span>
        <Button
          id="open-skills-folder"
          variant="secondary"
          size="compact"
          leadingIcon={FolderOpen}
          className="ml-auto"
          onClick={() => providersHandler.openSkillsFolder()}
        >
          Open skills folder
        </Button>
      </div>
      <p className="max-w-[640px] text-[13.5px] text-muted-foreground">
        The agent sees each skill's one-line description and reads a skill's
        instructions only when a request calls for it. Type / in a chat to
        start one yourself. Scripts in a skill do not run. Add a skill by
        putting its folder in {folder}, or ask a chat to make one.
      </p>
      <div className="flex flex-wrap items-center gap-2.5">
        <input
          aria-label="Search skills"
          placeholder="Search skills"
          value={query}
          onChange={(event) => setQuery(event.target.value)}
          className="h-9 w-64 rounded-lg bg-accent px-3 outline-none placeholder:text-muted-foreground"
        />
        <div className="flex gap-0.5 rounded-[10px] bg-accent p-[3px]">
          {SOURCES.map((choice) => (
            <button
              key={choice.label}
              type="button"
              onClick={() => setSource(choice.source)}
              className={cn(
                "flex h-7 items-center gap-1.5 rounded-[7px] px-2.5 text-muted-foreground",
                source === choice.source &&
                  "bg-background font-semibold text-foreground shadow-sm",
              )}
            >
              {choice.label}
              <span className="font-normal text-muted-foreground">
                {
                  skills.filter(
                    (skill) =>
                      choice.source === null || skill.source === choice.source,
                  ).length
                }
              </span>
            </button>
          ))}
        </div>
      </div>
      <div className="overflow-hidden rounded-xl border border-border">
        {shown.map((skill) => (
          <div
            key={skill.name}
            data-skill-row={skill.name}
            className="flex items-start gap-4 border-t border-border px-4 py-2.5 first:border-t-0"
          >
            <div className="min-w-0 flex-1">
              <Switch
                data-skill={skill.name}
                label={skill.name}
                checked={skill.on}
                onToggle={() => toggle(skill)}
              />
              <p className="truncate pl-11 text-xs text-muted-foreground">
                {skill.description}
              </p>
            </div>
            <span className="w-32 shrink-0 pt-1.5">
              <WhoStarts skill={skill} />
            </span>
            <span className="w-36 shrink-0 truncate pt-2 text-xs text-muted-foreground">
              {SOURCE_NAMES[skill.source]}
            </span>
          </div>
        ))}
      </div>
    </div>
  );
}
