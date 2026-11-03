# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure native Windows editing and analysis in a fresh bounded IDE per size."""
from pathlib import Path
import argparse
import json
from statistics import median

from ide_test_process import run_ide_case, write_profile


def write_fixture(path: Path, count: int) -> None:
    lines = [
        "// Red Panda C++ editor performance fixture",
        "// Editing trial uses this line",
        "struct BenchCandidateAlpha { int value; };",
        "struct BenchCandidateBeta { int value; };",
    ]
    for index in range((count - 5) // 6):
        lines += [
            f"struct BenchRecord{index} {{",
            "    int reading;",
            "    int count;",
            "    int next() const { return reading + count; }",
            "};",
            "",
        ]
    lines.extend(["// Padding"] * (count - 1 - len(lines)))
    lines.append("void completeHere() { BenchCandidate")
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ide", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report_root = args.output.resolve()
    report_root.mkdir(parents=True, exist_ok=False)
    results = []
    for lines in (3000, 30000, 100000):
        case = report_root / str(lines)
        case.mkdir()
        write_profile(case, completion=True)
        source = case / f"Editor_Performance_{lines}.cpp"
        write_fixture(source, lines)
        run = run_ide_case(args.ide, case,
            {"REDPANDA_QA_EDITOR_BENCHMARK": str(source)}, timeout=105, memory_mib=1280, platform="windows")
        report = case / "editor-performance.json"
        if not report.is_file():
            raise RuntimeError(f"Missing report for {lines} lines, exit {run.returncode}; {case}")
        data = json.loads(report.read_text(encoding="utf-8"))
        data["exitCode"] = run.returncode
        data["passed"] = data.get("passed", False) and run.returncode == 0 and data.get("lines") == lines
        if data["passed"]:
            for key in ("expandMs", "edit40PairsMs", "completionMs"):
                data[key + "Median"] = median(data[key])
        results.append(data)
        print(json.dumps(data), flush=True)
    (report_root / "summary.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    return 0 if all(data["passed"] for data in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
