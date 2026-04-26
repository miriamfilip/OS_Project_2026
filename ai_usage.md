# AI Usage Documentation — Phase 1

## Tool Used
Claude (Anthropic) — accessed via claude.ai

---

## Overview

The project specification requires two functions for the `filter` command to be
implemented with AI assistance:
- `parse_condition(const char *input, char *field, char *op, char *value)`
- `match_condition(Report *r, const char *field, const char *op, const char *value)`

This file documents the exact prompts given, what was generated, what was changed,
and what was learned.

---

## Prompt 1 — `parse_condition`

### Prompt given to AI:
> I have a C program for a UNIX systems programming project. I have a struct called Report:
>
> ```c
> typedef struct {
>     int    id;
>     char   inspector[64];
>     double latitude;
>     double longitude;
>     char   category[32];
>     int    severity;
>     time_t timestamp;
>     char   description[128];
> } Report;
> ```
>
> I need a function:
>   `int parse_condition(const char *input, char *field, char *op, char *value);`
>
> It receives a string in the format "field:operator:value"
> (e.g. "severity:>=:2" or "category:==:road") and splits it into three separate
> null-terminated strings written into `field`, `op`, and `value`.
> It should return 1 on success and 0 if the format is wrong.
> The operator can be ==, !=, <, <=, >, >=.
> The function must not use strtok because that modifies the original string.

### What the AI generated:
The AI generated a function that:
1. Copied the input into a local buffer with `strncpy`
2. Found the first `:` with `strchr`, null-terminated it, copied the field name
3. Advanced the pointer, found the second `:` for the operator
4. Copied the remainder as the value

The logic was correct and matched what I asked for. The AI correctly avoided `strtok`.

### What I changed:
- Added explicit null-termination after every `strncpy` call
  (e.g. `field[63] = '\0'`) because `strncpy` does not guarantee a null terminator
  if the source is longer than the limit — this is a common C bug
- Enlarged the working buffer from 128 to 256 bytes to be safe

### What I learned:
The AI correctly understood the "no strtok" constraint and produced clean pointer
arithmetic. However it did not add defensive null-termination after `strncpy`,
which is a classic C pitfall. This showed me I always need to add explicit
null-terminators after `strncpy` calls.

---

## Prompt 2 — `match_condition`

### Prompt given to AI:
> Using the same Report struct above, generate a function:
>   `int match_condition(Report *r, const char *field, const char *op, const char *value);`
>
> It returns 1 if the report satisfies the condition and 0 otherwise.
> Supported fields: severity (int), category (string), inspector (string),
> timestamp (time_t — compare as integer).
> Supported operators: ==, !=, <, <=, >, >=.
> For string fields only == and != make sense.
> The value argument is always a string — convert it to the right C type before comparing.

### What the AI generated:
The AI generated a function with a chain of `strcmp` calls on `field`, then nested
`strcmp` calls on `op`. For `severity` it used `atoi()` and for `timestamp` it
also used `atoi()` to convert the value string.

For string fields (`category`, `inspector`) it used `strcmp(r->field, value)` and
checked the result against the operator.

### What I changed:
- The AI used `atoi()` for the `timestamp` field. This is wrong because `time_t`
  is a 64-bit integer on modern Linux — `atoi()` returns a 32-bit `int` and would
  overflow for any timestamp after year 2038. I changed it to `atol()` which
  matches `time_t` on 64-bit systems.
- Added a return 0 for unsupported operators on string fields instead of leaving
  undefined behaviour.

### What I learned:
The AI produced correct logic but used the wrong integer conversion function for
`time_t`. This is a subtle portability issue — it would work fine today but break
on timestamps after 2038. It taught me to always check the type size when
converting strings to integers in C.

---

## Summary Table

| | parse_condition | match_condition |
|---|---|---|
| Overall correctness | Correct logic | Correct logic |
| Issue found | Missing null-termination after strncpy | atoi() instead of atol() for time_t |
| What I changed | Added null-terminators, enlarged buffer | Fixed to atol(), added fallback return |
| Lines reviewed | All (~25 lines) | All (~40 lines) |

---

## Filter Logic (written by me, not AI)

The actual `cmd_filter()` function was written entirely by me. It:
1. Opens `reports.dat` with `open()`
2. Reads one `Report` at a time using `read(fd, &r, sizeof(r))`
3. Calls `parse_condition()` on each command-line condition argument
4. Tests each record against every condition using `match_condition()`
5. Prints records where all conditions return 1 (AND logic)

---

## Key Lesson

AI-generated C code requires careful review of:
1. Buffer safety — null-termination after `strncpy`
2. Integer type sizes — `int` vs `long` vs `time_t`
3. Edge cases — unsupported operators, malformed input

The AI was useful for generating the skeleton quickly, but the review step is where
the real understanding came from.
