import argparse
import re
from pathlib import Path


SEMVER_CORE_RE = r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"


def extract_section(text: str, version: str) -> str:
    heading_re = re.compile(r"(?m)^##\s+(?:\[" + re.escape(version) + r"\]|" + re.escape(version) + r")(?:\s+-\s+.*)?\s*$")
    match = heading_re.search(text)
    if not match:
        raise RuntimeError(f"Missing CHANGELOG.md section for version {version}")

    next_heading = re.search(r"(?m)^##\s+", text[match.end():])
    end = match.end() + next_heading.start() if next_heading else len(text)
    section = text[match.start():end].strip()
    if not section:
        raise RuntimeError(f"Empty CHANGELOG.md section for version {version}")
    return section + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--changelog", default="CHANGELOG.md")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    if not re.fullmatch(SEMVER_CORE_RE, args.version, flags=re.ASCII):
        raise RuntimeError("--version must use MAJOR.MINOR.PATCH with no leading zeroes")

    changelog_path = Path(args.changelog)
    output_path = Path(args.output)
    try:
        text = changelog_path.read_text(encoding="utf-8")
        output_path.write_text(extract_section(text, args.version), encoding="utf-8", newline="")
    except Exception as exc:
        raise SystemExit(str(exc)) from exc


if __name__ == "__main__":
    main()
