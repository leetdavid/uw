/** One request to uw, and how its result reads in a terminal. */

export interface Call {
  method: string;
  params: object;
  describe: (result: unknown) => string;
}
