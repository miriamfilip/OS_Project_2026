## Tool Used
Claude (Anthropic) — accessed via claude.ai

AI Usage — Phase 1

Prompt 1 — parse_condition
What I asked:

I have a C program for a UNIX systems project. I have this Report struct:

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

 I need a function: int parse_condition(const char *input, char *field, char *op, char *value); It takes a string like "severity:>=:2" or "category:==:road" and splits it into three separate null-terminated strings. Return 1 on success, 0 if the format is wrong. Do not use strtok — it modifies the original string.

What came back: A function that copied the input into a local buffer, found the first : with strchr, null-terminated it to extract the field name, then repeated for the operator and value. The logic was right and it avoided strtok as requested.

What I changed: After every strncpy call I added an explicit null-terminator (e.g. field[63] = '\0'). strncpy does not guarantee a null terminator if the source is longer than the limit — leaving it out is a classic C bug that causes silent memory corruption. I also widened the working buffer from 128 to 256 bytes.

What I learned: The "no strtok" constraint was handled correctly. The missing null-termination was something I caught on review — it reminded me that strncpy is not as safe as it looks and always needs that extra line after it.


Prompt 2 — match_condition
What I asked:

Using the same Report struct, generate: int match_condition(Report *r, const char *field, const char *op, const char *value); Returns 1 if the report satisfies the condition, 0 otherwise. Supported fields: severity (int), category (string), inspector (string), timestamp (time_t — treat as integer). Operators: ==, !=, <, <=, >, >=. For string fields only == and != make sense. The value is always a string — convert it to the right C type before comparing.

What came back: A chain of strcmp calls on field, with nested strcmp calls on op inside each branch. For severity it used atoi(). For timestamp it also used atoi(). For string fields it used strcmp(r->field, value).

What I changed: The timestamp field uses time_t, which is 64-bit on modern Linux. atoi() returns a plain int (32-bit) and would overflow for any timestamp past 2038. I replaced it with atol(). I also added a return 0 for unsupported operators on string fields — the original left that path without a return value which is undefined behaviour in C.

What I learned: The logic was correct but the type mismatch for time_t was a real bug, not just a style issue. It would have compiled without warnings and worked fine until 2038. This made me more careful about checking what type a field actually is before picking a conversion function.

What I wrote myself
cmd_filter() — the function that actually uses the two above — was written entirely by me. It opens reports.dat, reads one Report at a time with read(fd, &r, sizeof(r)), calls parse_condition() on each command-line argument, checks every record against every condition with match_condition(), and prints the ones where all conditions return 1 (AND logic).

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


AI Usage — Phase 3

Concept Question 1 — How does a pipe actually work between two processes?
What I asked:

I understand what a pipe is in theory but I don't understand the mechanics. If I call pipe(pipefd) I get two file descriptors. One process writes into pipefd[1] and another reads from pipefd[0]. But what happens to the unused ends — do I have to close them and why?

What I learned: When all write ends of a pipe are closed, read() on the read end returns 0 (EOF) — that is how the reader knows the writer is done. If the parent keeps pipefd[1] open after forking, read() blocks forever even after the child exits, because the kernel still sees an open writer.

This explained a bug I was trying to understand in hub_mon_run(): forgetting to close(pipefd[1]) in the parent after forking the monitor meant the reading loop never ended even after the monitor process died. The rule is always close every pipe end you are not using, in every process.

Concept Question 2 — What does dup2() do and why before exec()?
What I asked:

In the code I need to redirect the monitor's stdout into a pipe. I see that dup2(pipefd[1], STDOUT_FILENO) is used. What exactly does dup2 do and why does it have to happen before exec()?

What I learned: dup2(oldfd, newfd) makes newfd point to the same underlying file as oldfd. After dup2(pipefd[1], STDOUT_FILENO), file descriptor 1 (stdout) and pipefd[1] both refer to the same pipe write end. Any write to stdout now goes into the pipe instead of the terminal.

It must happen before exec() because exec replaces all of the process's code — there is no way to set up the redirection after that. The file descriptor table survives exec, which is exactly why this works: the new program inherits the wired-up descriptors and writes to the pipe without knowing anything about it. After dup2(), the original pipefd[1] must also be closed — otherwise there are two open references to the write end and the pipe stays open longer than it should.

Code Prompt — hub_mon_run()
What I asked:

I am writing city_hub.c for a UNIX systems project in C. I need a static function hub_mon_run() that runs inside a child process called hub_mon. It must:

Create a pipe with pipe()
Fork a second child that will exec ./monitor_reports
In the monitor child: dup2() stdout to the pipe write end, then exec
In hub_mon: close the write end, read from the read end one character at a time, build complete lines
Each line has the format TYPE:message where TYPE is INFO, REPORT, STOP or ERROR — print different output for each type
Stop reading when STOP: or ERROR: is received
Call waitpid() on the monitor child before exiting Use only POSIX calls: pipe(), fork(), dup2(), execl(), read(), close(), waitpid(). No printf inside signal handlers.

What came back: A complete hub_mon_run() with correct pipe setup, correct dup2 usage, correct close order, character-by-character reading into a buffer, strncmp dispatching on the TYPE prefix, and waitpid() at the end.

What I changed:

Added fflush(stdout) after every printf — not in the generated code. Because hub_mon and city_hub share the same terminal, without flushing the monitor messages sometimes only appeared after the next hub> prompt was printed.
Added a fallback else branch for unrecognised message types. The generated code only handled the four known types — anything unexpected would be silently dropped. The fallback prints the raw line so nothing is lost.
Added comments explaining why pipefd[1] must be closed in the parent. The generated code closed it correctly but with no explanation. After working through the concept questions above I understood why it is critical, so I documented it directly in the code.

What I learned: The skeleton was structurally correct. What was missing was any explanation of pipe lifetime — which end to close, where, and why. That understanding came from the concept questions first. Once you know why each close() call is there the code almost writes itself.

