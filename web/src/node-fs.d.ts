declare module "node:fs" {
  export function readFileSync(path: URL | string): Uint8Array;
  export function writeFileSync(path: string, data: Uint8Array): void;
  export function mkdtempSync(prefix: string): string;
  export function rmSync(path: string, options?: { recursive?: boolean; force?: boolean }): void;
}

declare module "node:os" {
  export function tmpdir(): string;
}

declare module "node:path" {
  export function join(...parts: string[]): string;
}

declare module "node:child_process" {
  export function execFileSync(file: string, args?: string[], options?: { stdio?: "ignore" }): Uint8Array;
}
