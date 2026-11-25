# Rx compiler test cases

[English](README-EN.md) | [简体中文](README-ZH.md)

Welcome to the Rx Compiler course! This repository contains the test cases for the Rx compiler course. The files are organized by the compiler stage or kind of workload they exercise. A test runner discovers test cases from `manifest.json` files.

### Test families

- **`lexer/`** contains tokenization tests. `accept/` holds inputs that should lex successfully and `reject/` holds malformed inputs that should be rejected.
- **`parser/`** contains syntax tests with the same `accept/` and `reject/` split.
- **`semantic/`** contains feature-oriented programs that are checked through semantic analysis. A manifest can contain both accepted and rejected programs, distinguished by `compilation_success`.
- **`codegen/`** contains programs that must compile and run. Each test run supplies stdin and the expected stdout for one execution.
- **`optimization/`** contains larger or optimization-sensitive runtime workloads.

## Files in a test directory

- **`.rx`** — an Rx source program.
- **`.in`** — stdin for one runtime execution. A manifest may use `null` when a case needs empty stdin instead of a file.
- **`.out`** — the exact stdout expected from the corresponding execution. Use an empty file when the program should print nothing.
- **`manifest.json`** — a nonempty JSON array describing the cases in that directory. Paths in the manifest are relative to the manifest's directory.

The usual runtime naming pattern keeps test input and output files beside their source, for example:

```text
codegen/integer-arithmetic/
├── manifest.json
├── acc-...-i32.rx
├── acc-...-i32.mixed.in
└── acc-...-i32.mixed.out
```

One source can have several `io` pairs, which is useful for smoke, boundary, large, or otherwise distinct input scenarios.

## Manifest format

`manifest.schema.json` is the JSON Schema for `manifest.json`. Each entry contains:

```json
{
  "source": "program.rx",
  "stage": "semantic",
  "compilation_success": true,
  "description": "Optional explanation",
  "metadata": {}
}
```

The fields are:

- **`source`** — one source file for the entry.
- **`stage`** — one of `lex`, `parse`, `semantic`, `codegen`, or `optimization`.
- **`compilation_success`** — whether the source is expected to pass that stage. A normal rejection is a valid negative result; a crash, signal, or timeout is not.
- **`io`** — one or more `{ "input", "output" }` pairs. This is required for `codegen` and `optimization` entries, which must compile successfully before their generated program is run.
- **`description`** — an optional human-readable purpose or explanation for a case, especially a negative case.
- **`metadata`** — optional authoring or provenance information. It can record hashes, upstream origins, tokens, and similar details.
