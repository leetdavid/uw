import { helpLlms } from "@/lib/source";

export async function GET() {
  return new Response(await helpLlms.index());
}
