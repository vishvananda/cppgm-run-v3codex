#!/usr/bin/env python3
"""Run reproducible, interleaved compiler A/B measurements with GNU time."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import time


ABBA = ("a", "b", "b", "a")
TIME_FORMAT = "%e\t%U\t%S\t%M"


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def cpu_pressure():
    path = Path("/proc/pressure/cpu")
    if not path.exists():
        return {}
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if not fields:
            continue
        prefix = fields[0]
        for field in fields[1:]:
            if "=" not in field:
                continue
            key, value = field.split("=", 1)
            try:
                values[prefix + "_" + key] = float(value)
            except ValueError:
                pass
    return values


def host_load():
    try:
        load1, load5, load15 = os.getloadavg()
    except OSError:
        return {}
    return {"load1": load1, "load5": load5, "load15": load15}


def load_snapshot():
    result = host_load()
    result.update(cpu_pressure())
    return result


def screen_allows_run(snapshot, args):
    if args.max_load1 is not None and snapshot.get("load1", 0.0) > args.max_load1:
        return False
    pressure = snapshot.get("some_avg10")
    if (args.max_cpu_some_avg10 is not None and pressure is not None and
            pressure > args.max_cpu_some_avg10):
        return False
    return True


def wait_for_screen(args):
    started = time.monotonic()
    next_notice = started
    while True:
        snapshot = load_snapshot()
        if screen_allows_run(snapshot, args):
            return snapshot
        now = time.monotonic()
        if now >= next_notice:
            print("waiting for load screen: load1=%s cpu-some-avg10=%s" % (
                format_number(snapshot.get("load1")),
                format_number(snapshot.get("some_avg10")),
            ), flush=True)
            next_notice = now + 30.0
        if args.screen_timeout_sec <= 0:
            raise RuntimeError("host load screen rejected the benchmark block")
        if now - started >= args.screen_timeout_sec:
            raise RuntimeError("timed out waiting for the host load screen")
        time.sleep(min(5.0, args.screen_timeout_sec))


def parse_time_file(path):
    lines = [line for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
    fields = lines[-1].split("\t") if lines else []
    if len(fields) != 4:
        raise RuntimeError("unexpected GNU time output in %s" % path)
    return {
        "wall_seconds": float(fields[0]),
        "user_seconds": float(fields[1]),
        "system_seconds": float(fields[2]),
        "max_rss_kib": int(fields[3]),
    }


def compiler_identity(path):
    resolved = path.resolve()
    return {
        "path": str(resolved),
        "size": resolved.stat().st_size,
        "sha256": file_sha256(resolved),
    }


def run_one(args, repo_root, compiler, label, block, position, output_dir):
    index = block * len(ABBA) + position + 1
    object_path = output_dir / ("%02d-%s.o" % (index, label))
    time_path = output_dir / ("%02d-%s.time" % (index, label))
    for stale_path in (object_path, time_path):
        if stale_path.exists():
            stale_path.unlink()
    command = [str(compiler), *args.compiler_arg]
    for include in args.include:
        command.extend(("-I", include))
    command.extend(("-c", "-o", str(object_path), args.source))
    timed_command = [args.time_binary, "-f", TIME_FORMAT, "-o", str(time_path), *command]
    before = load_snapshot()
    try:
        proc = subprocess.run(
            timed_command,
            cwd=str(repo_root),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=None if args.timeout_sec <= 0 else args.timeout_sec,
        )
        timed_out = False
    except subprocess.TimeoutExpired as exc:
        proc = exc
        timed_out = True
    after = load_snapshot()

    run = {
        "block": block + 1,
        "position": position + 1,
        "sequence_index": index,
        "label": label,
        "command": command,
        "load_before": before,
        "load_after": after,
        "timed_out": timed_out,
    }
    if timed_out:
        run.update({
            "returncode": None,
            "stdout": (proc.stdout or "") if isinstance(proc.stdout, str) else "",
            "stderr": (proc.stderr or "") if isinstance(proc.stderr, str) else "",
            "status": "timeout",
        })
        return run

    run.update({
        "returncode": proc.returncode,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
        "status": "ok" if proc.returncode == 0 else "error",
    })
    if time_path.exists():
        run.update(parse_time_file(time_path))
    if object_path.exists():
        run["output_size"] = object_path.stat().st_size
        run["output_sha256"] = file_sha256(object_path)
    return run


def metric_summary(runs, key):
    values = [run[key] for run in runs if key in run]
    if not values:
        return None
    return {
        "median": statistics.median(values),
        "minimum": min(values),
        "maximum": max(values),
    }


def summarize_label(runs, label):
    selected = [run for run in runs if run["label"] == label]
    result = {
        "runs": len(selected),
        "successful_runs": sum(run["status"] == "ok" for run in selected),
    }
    for key in ("wall_seconds", "user_seconds", "system_seconds", "max_rss_kib"):
        result[key] = metric_summary(selected, key)
    return result


def paired_ratios(runs, metric):
    ratios = []
    blocks = sorted({run["block"] for run in runs})
    for block in blocks:
        block_runs = [run for run in runs if run["block"] == block]
        a_values = [run[metric] for run in block_runs
                    if run["label"] == "a" and metric in run]
        b_values = [run[metric] for run in block_runs
                    if run["label"] == "b" and metric in run]
        if len(a_values) == 2 and len(b_values) == 2:
            a_average = statistics.mean(a_values)
            b_average = statistics.mean(b_values)
            if a_average:
                ratios.append(b_average / a_average)
    return ratios


def validate_runs(runs, output_mode, compiler_identities, compilers):
    errors = []
    if any(run["status"] != "ok" for run in runs):
        errors.append("one or more compiler invocations failed")
    labels = sorted({run["label"] for run in runs})
    per_label_hashes = {}
    for label in labels:
        hashes = {run.get("output_sha256") for run in runs if run["label"] == label}
        hashes.discard(None)
        per_label_hashes[label] = hashes
        if len(hashes) != 1:
            errors.append("%s output is not deterministic" % label)
    all_hashes = set().union(*per_label_hashes.values()) if per_label_hashes else set()
    if output_mode == "exact" and len(all_hashes) != 1:
        errors.append("baseline and candidate output bytes differ")
    for label, compiler in compilers.items():
        if compiler_identity(compiler) != compiler_identities[label]:
            errors.append("compiler %s changed during the benchmark" % label)
    return errors


def build_report(args, repo_root, compiler_identities, runs, block_loads):
    ratios = {
        key: paired_ratios(runs, key)
        for key in ("wall_seconds", "user_seconds", "system_seconds", "max_rss_kib")
    }
    paired = {}
    for key, values in ratios.items():
        paired[key] = {
            "ratios": values,
            "median_ratio": statistics.median(values) if values else None,
        }
    return {
        "created_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "repo_root": str(repo_root),
        "source": {
            "path": str(Path(args.source).resolve()),
            "size": Path(args.source).resolve().stat().st_size,
            "sha256": file_sha256(Path(args.source).resolve()),
        },
        "include": args.include,
        "compiler_arg": args.compiler_arg,
        "abba_blocks": args.abba_blocks,
        "output_mode": args.output_mode,
        "host": {
            "platform": platform.platform(),
            "logical_cpus": os.cpu_count(),
        },
        "load_screen": {
            "max_load1": args.max_load1,
            "max_cpu_some_avg10": args.max_cpu_some_avg10,
            "block_start": block_loads,
        },
        "compilers": compiler_identities,
        "summary": {
            "a": summarize_label(runs, "a"),
            "b": summarize_label(runs, "b"),
            "paired_candidate_over_baseline": paired,
        },
        "runs": runs,
    }


def format_number(value, suffix=""):
    if value is None:
        return "n/a"
    return "%.3f%s" % (value, suffix)


def print_summary(report):
    print("\nA/B summary")
    print("  source: %s" % report["source"]["path"])
    for label in ("a", "b"):
        summary = report["summary"][label]
        wall = summary["wall_seconds"]
        user = summary["user_seconds"]
        rss = summary["max_rss_kib"]
        print("  %s: wall %s, user %s, RSS %s KiB (%d/%d successful)" % (
            label,
            format_number(wall["median"] if wall else None, "s"),
            format_number(user["median"] if user else None, "s"),
            format_number(rss["median"] if rss else None),
            summary["successful_runs"],
            summary["runs"],
        ))
    for metric in ("wall_seconds", "user_seconds", "max_rss_kib"):
        ratio = report["summary"]["paired_candidate_over_baseline"][metric]["median_ratio"]
        delta = None if ratio is None else (ratio - 1.0) * 100.0
        print("  paired %-14s %s" % (metric + ":", format_number(delta, "%")))


def write_report(path, report):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run GNU-time compiler measurements in interleaved ABBA order."
    )
    parser.add_argument("--repo-root", default=str(Path(__file__).resolve().parent.parent))
    parser.add_argument("--compiler-a", required=True)
    parser.add_argument("--compiler-b", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--include", action="append")
    parser.add_argument("--compiler-arg", action="append", default=[])
    parser.add_argument("--abba-blocks", type=int, default=3)
    parser.add_argument("--timeout-sec", type=int, default=180)
    parser.add_argument("--time-binary", default="/usr/bin/time")
    parser.add_argument("--output-prefix", default="/tmp/cppgm-ab-compile")
    parser.add_argument("--output-mode", choices=("exact", "deterministic"), default="exact")
    parser.add_argument("--max-load1", type=float)
    parser.add_argument("--max-cpu-some-avg10", type=float)
    parser.add_argument("--screen-timeout-sec", type=int, default=0)
    args = parser.parse_args()
    if args.abba_blocks < 1:
        parser.error("--abba-blocks must be at least 1")
    if args.include is None:
        args.include = ["dev/src"]
    return args


def main():
    args = parse_args()
    repo_root = Path(args.repo_root).resolve()
    compilers = {
        "a": Path(args.compiler_a).resolve(),
        "b": Path(args.compiler_b).resolve(),
    }
    for label, compiler in compilers.items():
        if not compiler.is_file():
            raise SystemExit("compiler %s does not exist: %s" % (label, compiler))
    if not Path(args.time_binary).is_file():
        raise SystemExit("GNU time does not exist: %s" % args.time_binary)
    source = Path(args.source).resolve()
    if not source.is_file():
        raise SystemExit("source does not exist: %s" % source)

    compiler_identities = {
        label: compiler_identity(compiler) for label, compiler in compilers.items()
    }

    output_dir = Path(args.output_prefix + "-objects")
    output_dir.mkdir(parents=True, exist_ok=True)
    report_path = Path(args.output_prefix + ".json")
    runs = []
    block_loads = []

    def checkpoint(status, errors=None):
        report = build_report(args, repo_root, compiler_identities, runs, block_loads)
        report["status"] = status
        report["validation_errors"] = errors or []
        write_report(report_path, report)
        return report

    try:
        for block in range(args.abba_blocks):
            snapshot = wait_for_screen(args)
            block_loads.append(snapshot)
            print("block %d/%d load1=%.2f cpu-some-avg10=%s" % (
                block + 1,
                args.abba_blocks,
                snapshot.get("load1", 0.0),
                format_number(snapshot.get("some_avg10")),
            ), flush=True)
            for position, label in enumerate(ABBA):
                print("  run %d %s" % (len(runs) + 1, label), flush=True)
                run = run_one(
                    args,
                    repo_root,
                    compilers[label],
                    label,
                    block,
                    position,
                    output_dir,
                )
                runs.append(run)
                checkpoint("running")
                print("    %s wall=%s user=%s rss=%s" % (
                    run["status"],
                    format_number(run.get("wall_seconds"), "s"),
                    format_number(run.get("user_seconds"), "s"),
                    format_number(run.get("max_rss_kib"), " KiB"),
                ), flush=True)
    except KeyboardInterrupt:
        checkpoint("interrupted")
        print("\nbenchmark interrupted; partial report: %s" % report_path,
              file=sys.stderr)
        return 130
    except RuntimeError as exc:
        checkpoint("screen-error", [str(exc)])
        print("benchmark error: %s" % exc, file=sys.stderr)
        print("partial report: %s" % report_path, file=sys.stderr)
        return 2

    report = build_report(args, repo_root, compiler_identities, runs, block_loads)
    errors = validate_runs(
        runs, args.output_mode, compiler_identities, compilers
    )
    report["status"] = "complete"
    report["validation_errors"] = errors
    write_report(report_path, report)
    print_summary(report)
    print("  report: %s" % report_path)
    if errors:
        for error in errors:
            print("  validation error: %s" % error, file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
