# Native replay diagnosis

`run-replay-probe.py` links the production engine into an isolated, headless Mac
Ninja test executable and advances recorded commands without frame pacing.
It does not modify the supplied replay or the normal game profile.

```sh
python3 tests/replay/run-replay-probe.py --replay /path/to/auto.rpl \
  --workshop-from /path/to/recorded-profile/workshop --cycles 60000 \
  --output-dir /tmp/dunecity-replay-check
```

Supply the recorded Workshop revision cache when a replay pins old content.
`--trace-throws` adds a test-only exception interceptor that prints the original
simulation throw stack before C++ unwinding. It is never linked into the game.
The runner fails unless the requested cycle is reached. Logs stay in the output
directory; a successful result includes a deterministic state digest.
