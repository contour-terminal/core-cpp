// SPDX-License-Identifier: Apache-2.0
//
// Runs a wasm32-wasip1 command module under Node's WASI preview 1 and exits with its exit code:
// the CMAKE_CROSSCOMPILING_EMULATOR of tests/wasi-unicode.
//
//   node run-wasi.mjs <module.wasm> [args...]

import { readFile } from "node:fs/promises";
import { argv, exit, stderr } from "node:process";
import { WASI } from "node:wasi";

if (argv.length < 3) {
    stderr.write("usage: node run-wasi.mjs <module.wasm> [args...]\n");
    exit(2);
}

const wasi = new WASI({ version: "preview1", args: argv.slice(2), env: {}, returnOnExit: true });
const module = await WebAssembly.compile(await readFile(argv[2]));
const instance = await WebAssembly.instantiate(module, wasi.getImportObject());
exit(wasi.start(instance));
