import argparse
import os
import re
from pathlib import Path


def replace_define(text: str, name: str, value: int) -> str:
    pattern = rf"(?m)^#define {re.escape(name)}\s+\d+"
    replacement = f"#define {name} {value}"
    text, count = re.subn(pattern, replacement, text)
    if count != 1:
        raise RuntimeError(f"Expected exactly one {name} define")
    return text


def read_define(text: str, name: str) -> int:
    match = re.search(rf"(?m)^#define {re.escape(name)}\s+(\d+)", text)
    if not match:
        raise RuntimeError(f"Missing {name} define")
    return int(match.group(1))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-count", type=int, required=True)
    parser.add_argument("--version", default="")
    parser.add_argument("--ref-name", default="")
    parser.add_argument("--version-header", default="src/HexEdit/version.h")
    args = parser.parse_args()

    header = Path(args.version_header).resolve()
    text = header.read_text(encoding="utf-8")

    version_match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", args.version)
    tag_match = re.fullmatch(r"v(\d+)\.(\d+)\.(\d+)", args.ref_name)
    if version_match:
        major, minor, patch = (int(part) for part in version_match.groups())
    elif tag_match:
        major, minor, patch = (int(part) for part in tag_match.groups())
    elif args.version:
        raise RuntimeError("--version must use MAJOR.MINOR.PATCH format")
    else:
        major = read_define(text, "VERSION_MAJOR")
        minor = read_define(text, "VERSION_MINOR")
        patch = read_define(text, "VERSION_PATCH")

    text = replace_define(text, "VERSION_MAJOR", major)
    text = replace_define(text, "VERSION_MINOR", minor)
    text = replace_define(text, "VERSION_PATCH", patch)
    text = replace_define(text, "VERSION_BUILD_COUNT", args.build_count)
    header.write_text(text, encoding="utf-8", newline="")

    product_version = f"{major}.{minor}.{patch}"
    file_version = f"{product_version}.{args.build_count}"

    github_env = os.environ.get("GITHUB_ENV")
    if github_env:
        with open(github_env, "a", encoding="utf-8") as env:
            print(f"HEXEDIT_PRODUCT_VERSION={product_version}", file=env)
            print(f"HEXEDIT_FILE_VERSION={file_version}", file=env)

    print(f"Stamped HexEdit version {file_version}")
    print(f"Writing: {header}")


if __name__ == "__main__":
    main()
