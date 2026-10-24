/** The part of postject build.ts uses; the package ships no types. */
declare module "postject" {
  export function inject(
    file: string,
    resource: string,
    data: Buffer,
    options: { sentinelFuse: string; machoSegmentName?: string },
  ): Promise<void>;
}
