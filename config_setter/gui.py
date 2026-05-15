#!/usr/bin/env python3
"""
Workload profile generator for throughput simulation.
Writes freq to shared memory at configurable intervals.
Supports composable operations (steady, sharkfin, ramp, etc.)
and both interactive and JSON replay modes.
"""

import sys
import json
import time
import math
import struct
import mmap
import argparse
import signal
from abc import ABC, abstractmethod
from pathlib import Path

SHM_PATH = "/dev/shm/myshm"
DEFAULT_INTERVAL_MS = 200


# ─── Shared Memory Writer ────────────────────────────────────────────────────

class ShmWriter:
    def __init__(self, path=SHM_PATH):
        self.path = path
        with open(self.path, "r+b") as f:
            mm = mmap.mmap(f.fileno(), 0)
            mm.seek(0)
            self.version = struct.unpack('<Q', mm.read(8))[0]
            mm.close()
        print(f"  ShmWriter: read current version={self.version}")

    def write(self, freq: int):
        self.version += 1
        with open(self.path, "r+b") as f:
            mm = mmap.mmap(f.fileno(), 0)
            # Write freq first (offset 8), var=0 (offset 16)
            mm.seek(8)
            mm.write(struct.pack('<QQ', freq, 0))
            mm.flush()
            # Then set version atomically (offset 0)
            mm.seek(0)
            mm.write(struct.pack('<Q', self.version))
            mm.flush()
            mm.close()


# ─── Operation Base & Registry ───────────────────────────────────────────────

OPERATION_REGISTRY = {}


def register_op(name):
    """Decorator to register an operation class by name."""
    def decorator(cls):
        OPERATION_REGISTRY[name] = cls
        return cls
    return decorator


class Operation(ABC):
    """
    Base class for all workload operations.

    Subclasses must implement:
        - duration_ms: total duration of this operation in milliseconds
        - freq_at(t_ms): return the frequency at time t_ms (0 <= t_ms < duration_ms)

    Optionally override:
        - interval_ms: per-operation update interval (default: global)
    """

    interval_ms: int | None = None  # None means use global default

    @property
    @abstractmethod
    def duration_ms(self) -> int:
        ...

    @abstractmethod
    def freq_at(self, t_ms: int, base_freq: int) -> int:
        """Return frequency at time offset t_ms within this operation."""
        ...

    @classmethod
    @abstractmethod
    def from_dict(cls, d: dict) -> "Operation":
        """Construct from a JSON-style dict."""
        ...

    @classmethod
    @abstractmethod
    def from_interactive(cls) -> "Operation":
        """Construct by prompting the user interactively."""
        ...

    @classmethod
    def help_text(cls) -> str:
        return cls.__doc__ or "No description."


# ─── Built-in Operations ─────────────────────────────────────────────────────

@register_op("steady")
class SteadyOp(Operation):
    """Hold a constant frequency for a given duration."""

    def __init__(self, duration_ms: int, freq: int | None = None, interval_ms: int | None = None):
        self._duration_ms = duration_ms
        self._freq = freq  # None means use base_freq
        self.interval_ms = interval_ms

    @property
    def duration_ms(self) -> int:
        return self._duration_ms

    def freq_at(self, t_ms: int, base_freq: int) -> int:
        return self._freq if self._freq is not None else base_freq

    @classmethod
    def from_dict(cls, d: dict) -> "SteadyOp":
        return cls(
            duration_ms=d["duration_ms"],
            freq=d.get("freq"),
            interval_ms=d.get("interval_ms"),
        )

    @classmethod
    def from_interactive(cls) -> "SteadyOp":
        dur = int(input("  duration (ms): "))
        freq_str = input("  freq (empty = use base): ").strip()
        freq = int(freq_str) if freq_str else None
        return cls(duration_ms=dur, freq=freq)


@register_op("sharkfin")
class SharkfinOp(Operation):
    """
    Sharkfin spike: instant jump to peak, then exponential decay back to base.

    Parameters:
        peak        – max frequency at the spike
        decay_ms    – controls how fast it decays (time constant tau).
                      After decay_ms, the spike has decayed ~63%.
                      The operation runs until the freq is within 1% of base
                      or until a hard cap of 5*decay_ms.
        duration_ms – (optional) explicit total duration override
    """

    def __init__(self, peak: int, decay_ms: int, duration_ms: int | None = None,
                 interval_ms: int | None = None):
        self.peak = peak
        self.decay_ms = decay_ms
        # Default duration: run until decay is ~99% done (5 * tau) or explicit
        self._duration_ms = duration_ms or int(5 * decay_ms)
        self.interval_ms = interval_ms

    @property
    def duration_ms(self) -> int:
        return self._duration_ms

    def freq_at(self, t_ms: int, base_freq: int) -> int:
        # f(t) = base + (peak - base) * exp(-t / tau)
        tau = self.decay_ms
        delta = self.peak - base_freq
        freq = base_freq + delta * math.exp(-t_ms / tau)
        return max(0, int(round(freq)))

    @classmethod
    def from_dict(cls, d: dict) -> "SharkfinOp":
        return cls(
            peak=d["peak"],
            decay_ms=d["decay_ms"],
            duration_ms=d.get("duration_ms"),
            interval_ms=d.get("interval_ms"),
        )

    @classmethod
    def from_interactive(cls) -> "SharkfinOp":
        peak = int(input("  peak freq: "))
        decay_ms = int(input("  decay_ms (time constant, ~63% decay): "))
        dur_str = input("  duration_ms (empty = auto ~5*decay): ").strip()
        dur = int(dur_str) if dur_str else None
        return cls(peak=peak, decay_ms=decay_ms, duration_ms=dur)


@register_op("ramp")
class RampOp(Operation):
    """Linear ramp from one frequency to another over a duration."""

    def __init__(self, start_freq: int | None, end_freq: int | None,
                 duration_ms: int, interval_ms: int | None = None):
        self._start = start_freq  # None = base_freq
        self._end = end_freq      # None = base_freq
        self._duration_ms = duration_ms
        self.interval_ms = interval_ms

    @property
    def duration_ms(self) -> int:
        return self._duration_ms

    def freq_at(self, t_ms: int, base_freq: int) -> int:
        s = self._start if self._start is not None else base_freq
        e = self._end if self._end is not None else base_freq
        ratio = t_ms / self._duration_ms if self._duration_ms > 0 else 1.0
        ratio = min(1.0, max(0.0, ratio))
        return int(round(s + (e - s) * ratio))

    @classmethod
    def from_dict(cls, d: dict) -> "RampOp":
        return cls(
            start_freq=d.get("start_freq"),
            end_freq=d.get("end_freq"),
            duration_ms=d["duration_ms"],
            interval_ms=d.get("interval_ms"),
        )

    @classmethod
    def from_interactive(cls) -> "RampOp":
        s = input("  start_freq (empty = base): ").strip()
        e = input("  end_freq (empty = base): ").strip()
        dur = int(input("  duration_ms: "))
        return cls(
            start_freq=int(s) if s else None,
            end_freq=int(e) if e else None,
            duration_ms=dur,
        )


@register_op("square")
class SquareWaveOp(Operation):
    """Alternate between base and high freq. Specify high, period_ms, cycles."""

    def __init__(self, high_freq: int, period_ms: int, cycles: int,
                 interval_ms: int | None = None):
        self.high_freq = high_freq
        self.period_ms = period_ms
        self.cycles = cycles
        self.interval_ms = interval_ms

    @property
    def duration_ms(self) -> int:
        return self.period_ms * self.cycles

    def freq_at(self, t_ms: int, base_freq: int) -> int:
        phase = (t_ms % self.period_ms) / self.period_ms
        return self.high_freq if phase < 0.5 else base_freq

    @classmethod
    def from_dict(cls, d: dict) -> "SquareWaveOp":
        return cls(
            high_freq=d["high_freq"],
            period_ms=d["period_ms"],
            cycles=d["cycles"],
            interval_ms=d.get("interval_ms"),
        )

    @classmethod
    def from_interactive(cls) -> "SquareWaveOp":
        high = int(input("  high_freq: "))
        period = int(input("  period_ms: "))
        cycles = int(input("  cycles: "))
        return cls(high_freq=high, period_ms=period, cycles=cycles)


# ─── Runner ──────────────────────────────────────────────────────────────────

class WorkloadRunner:
    def __init__(self, base_freq: int, ops: list[Operation],
                 global_interval_ms: int = DEFAULT_INTERVAL_MS,
                 shm_path: str = SHM_PATH, dry_run: bool = False):
        self.base_freq = base_freq
        self.ops = ops
        self.global_interval_ms = global_interval_ms
        self.shm_path = shm_path
        self.dry_run = dry_run
        self._stop = False

    def stop(self):
        self._stop = True

    def run(self):
        writer = None if self.dry_run else ShmWriter(self.shm_path)
        total_ms = sum(op.duration_ms for op in self.ops)
        print(f"▶ Running workload: base_freq={self.base_freq}, "
              f"total_duration={total_ms}ms, ops={len(self.ops)}")

        # Set initial base frequency before starting
        if writer:
            writer.write(self.base_freq)
        print(f"    t=       0ms  freq={self.base_freq}  (initial)")

        global_t = 0
        for i, op in enumerate(self.ops):
            interval = op.interval_ms or self.global_interval_ms
            op_name = type(op).__name__
            print(f"  [{i+1}/{len(self.ops)}] {op_name} "
                  f"(duration={op.duration_ms}ms, interval={interval}ms)")

            t = 0
            while t < op.duration_ms and not self._stop:
                freq = op.freq_at(t, self.base_freq)

                if writer:
                    writer.write(freq)

                # Log every update
                print(f"    t={global_t:>8}ms  freq={freq}")

                sleep_s = interval / 1000.0
                time.sleep(sleep_s)
                t += interval
                global_t += interval

            if self._stop:
                print("⏹ Stopped.")
                return

        # Return to base after all ops
        if writer:
            writer.write(self.base_freq)
        print(f"  t={global_t:>8}ms  freq={self.base_freq}  (done, back to base)")
        print("✓ Workload complete.")


# ─── JSON Profile ────────────────────────────────────────────────────────────

def load_profile(path: str) -> tuple[int, int, list[Operation]]:
    """
    Load a workload profile from JSON.

    Expected format:
    {
        "base_freq": 1000,
        "interval_ms": 200,        // optional, default 200
        "ops": [
            {"op": "steady", "duration_ms": 2000},
            {"op": "sharkfin", "peak": 10000, "decay_ms": 500},
            {"op": "steady", "duration_ms": 1000},
            ...
        ]
    }
    """
    with open(path) as f:
        profile = json.load(f)

    base_freq = profile["base_freq"]
    interval_ms = profile.get("interval_ms", DEFAULT_INTERVAL_MS)
    ops = []
    for entry in profile["ops"]:
        op_name = entry["op"]
        if op_name not in OPERATION_REGISTRY:
            raise ValueError(f"Unknown operation: {op_name!r}. "
                             f"Available: {list(OPERATION_REGISTRY.keys())}")
        cls = OPERATION_REGISTRY[op_name]
        params = {k: v for k, v in entry.items() if k != "op"}
        ops.append(cls.from_dict(params))

    return base_freq, interval_ms, ops


def save_profile(path: str, base_freq: int, interval_ms: int, ops: list[Operation]):
    """Save current operation sequence as a replayable JSON profile."""
    profile = {
        "base_freq": base_freq,
        "interval_ms": interval_ms,
        "ops": [],
    }
    # Reverse-engineer dict from each op (simple introspection)
    for op in ops:
        op_name = None
        for name, cls in OPERATION_REGISTRY.items():
            if isinstance(op, cls):
                op_name = name
                break
        entry = {"op": op_name}
        # Pull the from_dict-compatible fields from the object
        if isinstance(op, SteadyOp):
            entry["duration_ms"] = op._duration_ms
            if op._freq is not None:
                entry["freq"] = op._freq
        elif isinstance(op, SharkfinOp):
            entry["peak"] = op.peak
            entry["decay_ms"] = op.decay_ms
            entry["duration_ms"] = op._duration_ms
        elif isinstance(op, RampOp):
            if op._start is not None:
                entry["start_freq"] = op._start
            if op._end is not None:
                entry["end_freq"] = op._end
            entry["duration_ms"] = op._duration_ms
        elif isinstance(op, SquareWaveOp):
            entry["high_freq"] = op.high_freq
            entry["period_ms"] = op.period_ms
            entry["cycles"] = op.cycles
        if op.interval_ms is not None:
            entry["interval_ms"] = op.interval_ms
        profile["ops"].append(entry)

    with open(path, "w") as f:
        json.dump(profile, f, indent=2)
    print(f"Profile saved to {path}")


# ─── Interactive Mode ────────────────────────────────────────────────────────

def interactive_mode(dry_run=False, shm_path=SHM_PATH):
    print("═══ Workload Generator ═══")
    print(f"Available operations: {', '.join(OPERATION_REGISTRY.keys())}")
    print()

    base_freq = int(input("Base frequency: "))
    interval_str = input(f"Global update interval ms [{DEFAULT_INTERVAL_MS}]: ").strip()
    global_interval = int(interval_str) if interval_str else DEFAULT_INTERVAL_MS

    ops: list[Operation] = []

    while True:
        print()
        print(f"  Operations queued: {len(ops)}")
        print("  Commands: <op_name> | list | run | save <file.json> | clear | quit")
        cmd = input("> ").strip().lower()

        if cmd == "quit" or cmd == "q":
            break
        elif cmd == "list":
            if not ops:
                print("  (empty)")
            for i, op in enumerate(ops):
                print(f"  [{i}] {type(op).__name__} duration={op.duration_ms}ms")
        elif cmd == "run":
            if not ops:
                print("  Nothing to run.")
                continue
            runner = WorkloadRunner(base_freq, ops, global_interval,
                                    shm_path=shm_path, dry_run=dry_run)
            signal.signal(signal.SIGINT, lambda *_: runner.stop())
            runner.run()
            signal.signal(signal.SIGINT, signal.default_int_handler)
        elif cmd.startswith("save"):
            parts = cmd.split(maxsplit=1)
            path = parts[1] if len(parts) > 1 else "workload.json"
            save_profile(path, base_freq, global_interval, ops)
        elif cmd == "clear":
            ops.clear()
            print("  Cleared.")
        elif cmd in OPERATION_REGISTRY:
            cls = OPERATION_REGISTRY[cmd]
            print(f"  ── {cmd}: {cls.help_text().strip()}")
            try:
                op = cls.from_interactive()
                ops.append(op)
                print(f"  Added {cmd} (duration={op.duration_ms}ms)")
            except (ValueError, KeyboardInterrupt):
                print("  Cancelled.")
        else:
            print(f"  Unknown command: {cmd!r}")


# ─── CLI ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Workload profile generator for throughput simulation")
    sub = parser.add_subparsers(dest="mode")

    # Interactive mode
    inter = sub.add_parser("interactive", help="Build and run workload interactively")
    inter.add_argument("--dry-run", action="store_true",
                       help="Print freq values without writing to shm")
    inter.add_argument("--shm", default=SHM_PATH, help="Shared memory path")

    # Replay mode
    replay = sub.add_parser("replay", help="Replay a saved JSON workload profile")
    replay.add_argument("profile", help="Path to JSON profile file")
    replay.add_argument("--dry-run", action="store_true")
    replay.add_argument("--shm", default=SHM_PATH, help="Shared memory path")

    # One-shot write (backwards compat)
    write = sub.add_parser("write", help="One-shot: write freq to shm")
    write.add_argument("freq", type=int)
    write.add_argument("--shm", default=SHM_PATH)

    args = parser.parse_args()

    if args.mode == "interactive":
        interactive_mode(dry_run=args.dry_run, shm_path=args.shm)

    elif args.mode == "replay":
        base_freq, interval_ms, ops = load_profile(args.profile)
        runner = WorkloadRunner(base_freq, ops, interval_ms,
                                shm_path=args.shm, dry_run=args.dry_run)
        signal.signal(signal.SIGINT, lambda *_: runner.stop())
        runner.run()

    elif args.mode == "write":
        w = ShmWriter(args.shm)
        w.write(args.freq)
        print(f"Written freq={args.freq}")

    else:
        parser.print_help()


if __name__ == "__main__":
    main()