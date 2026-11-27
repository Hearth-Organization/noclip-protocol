#!/usr/bin/env python3
"""Adds the big line sets to lines.json (owner, Nov 27: 1000 death calls, 300 of each agent group, every line different).

    python3 Tools/Voice/write_lines.py

Keeps the lines already there (they're recorded) first and in order, then adds new ones from death_parts.py and
agent_parts.py: whole lines first, then parts put together (each part used about as often as the others, every
combination once). Seeded, so a rerun gives the same lines. Refuses to write if any line repeats.
"""
import json
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import agent_parts as ap  # noqa: E402
import death_parts as dp  # noqa: E402

TARGET = {"command": 1000, "idle": 300, "spotted": 300, "hurt": 300, "newlevel": 300, "jokes": 300}


def norm(text):
    return " ".join(text.lower().replace(",", "").replace(".", "").replace("!", "").replace("?", "").split())


def fill(existing, whole, makers, count, rng):
    """existing + whole + made lines up to count, none repeating."""
    out = list(existing)
    seen = {norm(t) for t in out}
    for t in whole:
        if len(out) >= count:
            break
        if norm(t) not in seen:
            out.append(t)
            seen.add(norm(t))
    combos = [c for make in makers for c in make()]
    rng.shuffle(combos)
    # Even use: take combos round by round, preferring the least used parts.
    uses = {}
    while len(out) < count and combos:
        combos.sort(key=lambda c: sum(uses.get(p, 0) for p in c))
        parts = combos.pop(0)
        text = " ".join(parts)
        if norm(text) in seen:
            continue
        out.append(text)
        seen.add(norm(text))
        for p in parts:
            uses[p] = uses.get(p, 0) + 1
    if len(out) < count:
        raise SystemExit(f"only {len(out)} of {count} lines")
    return out


def pairs(a, b):
    return lambda: [(x, y) for x in a for y in b]


def triples(a, b, c, rng, n):
    def make():
        got = set()
        while len(got) < n:
            got.add((rng.choice(a), rng.choice(b), rng.choice(c)))
        return list(got)
    return make


def main():
    path = HERE / "lines.json"
    lines = json.loads(path.read_text())
    rng = random.Random(1127)
    lines["command"] = fill(lines["command"], dp.WHOLE, [pairs(dp.LOSS, dp.NEXT), triples(dp.LOSS, dp.AFTER, dp.NEXT, rng, 3000),
                                                         pairs(dp.AFTER, dp.NEXT)], TARGET["command"], rng)
    agent = lines["agent"]
    agent["idle"] = fill(agent["idle"], ap.IDLE, [], TARGET["idle"], rng)
    agent["jokes"] = fill(agent["jokes"], ap.JOKES, [], TARGET["jokes"], rng)
    agent["spotted"] = fill(agent["spotted"], ap.SPOTTED, [pairs(ap.SPOTTED_A, ap.SPOTTED_B)], TARGET["spotted"], rng)
    agent["hurt"] = fill(agent["hurt"], ap.HURT, [pairs(ap.HURT_A, ap.HURT_B)], TARGET["hurt"], rng)
    agent["newlevel"] = fill(agent["newlevel"], ap.NEWLEVEL, [pairs(ap.NEWLEVEL_A, ap.NEWLEVEL_B)], TARGET["newlevel"], rng)
    every = lines["command"] + [t for g in agent.values() for t in g]
    for group, texts in [("command", lines["command"])] + list(agent.items()):
        assert len({norm(t) for t in texts}) == len(texts), group
    path.write_text(json.dumps(lines, indent=2, ensure_ascii=False) + "\n")
    print({"command": len(lines["command"]), **{g: len(t) for g, t in agent.items()}, "total": len(every)})


if __name__ == "__main__":
    main()
