# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise project reference publication across an intervening editor edit."""
import argparse
import json
from pathlib import Path
import tempfile

from ide_test_process import run_ide_case, write_profile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ide", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="redpanda-project-reference-") as temporary:
        case = Path(temporary)
        write_profile(case, completion=True)
        run = run_ide_case(args.ide, case, {"REDPANDA_QA_PROJECT_REFERENCES": "1"}, timeout=65)
        report = case / "project-reference-result.json"
        if not report.is_file():
            raise RuntimeError(f"Project reference checks produced no report, exit {run.returncode}, "
                               f"stderr={run.stderr.decode(errors='replace')}")
        data = json.loads(report.read_text(encoding="utf-8"))
        if run.returncode or data.get("failures") or len(data.get("checks", [])) != 4:
            raise RuntimeError(f"Project reference checks failed, exit {run.returncode}, report={data}")
        print("PASS: asynchronous project reference result and revision invalidation")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
