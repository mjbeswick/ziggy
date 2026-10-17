#!/usr/bin/env python3
"""Copy sanoTTS's ESPHome component and apply BabyTalk's edits.

This is a portable (macOS/Linux) replacement for BabyTalk's
components/sanotts/prepare.sh, which needs GNU sed.  Same result, same
audited commit, same patch.

  scripts/prepare_sanotts.py <sanoTTS checkout> <output dir>

Exit 0: prepared.  1: no checkout (the firmware builds without TTS).
Exit 2: wrong commit or the edits did not apply.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

COMMIT = "18e26b2b365bff41d211e516b0760021451438f1"


def fail(msg: str) -> None:
    print(f"prepare_sanotts: {msg}", file=sys.stderr)
    sys.exit(2)


def main() -> int:
    if len(sys.argv) != 3:
        fail("usage: prepare_sanotts.py <sanoTTS checkout> <output dir>")
    root = Path(sys.argv[1])
    out = Path(sys.argv[2])
    src = root / "esphome" / "components" / "sanotts"
    if not (src / "snt_nano.c").is_file():
        print("prepare_sanotts: no sanoTTS checkout; building without TTS", file=sys.stderr)
        return 1

    have = "unknown"
    try:
        have = subprocess.check_output(
            ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        pass
    if have != COMMIT and not os.environ.get("SANOTTS_ALLOW_ANY_COMMIT"):
        fail(
            f"checkout is at {have}, not the audited {COMMIT}\n"
            f"  git -C {root} fetch origin {COMMIT} && git -C {root} checkout {COMMIT}\n"
            "  (SANOTTS_ALLOW_ANY_COMMIT=1 to override)"
        )

    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(src, out)

    # One scratch bank: the runtime is serial here.
    nano = out / "snt_nano.c"
    text = nano.read_text()
    if "static NanoScratch g_scr[2];" in text:
        text = text.replace("static NanoScratch g_scr[2];", "static NanoScratch g_scr[1];")
    if "static NanoScratch g_scr[1];" not in text:
        fail("could not shrink the scratch banks in snt_nano.c")

    # Speaking pace as a runtime setting.
    lines = text.splitlines(keepends=True)
    for i, line in enumerate(lines):
        if line.startswith("#define LENGTH_SCALE 1.0f"):
            lines[i] = (
                "float snt_length_scale = 1.0f;  /* BabyTalk: runtime pace */\n"
                "#define LENGTH_SCALE snt_length_scale\n"
            )
            break
    else:
        fail("LENGTH_SCALE define not found in snt_nano.c")
    text = "".join(lines)
    nano.write_text(text)

    # The scalar kernels are unused when the P4 SIMD ones are in, as on the S3.
    ref = out / "snt_kernels_ref.c"
    rtext = ref.read_text()
    if rtext.count("#if !SANOTTS_S3_SIMD") != 2:
        fail("unexpected kernel guards in snt_kernels_ref.c")
    ref.write_text(rtext.replace("#if !SANOTTS_S3_SIMD", "#if !SANOTTS_S3_SIMD && !SANOTTS_P4_SIMD"))

    patch = Path(__file__).resolve().parent.parent / "third_party" / "babytalk" / "components" / "sanotts" / "snt_nano.patch"
    if not patch.is_file():
        fail(f"missing patch {patch}")
    with patch.open("rb") as pf:
        rc = subprocess.run(["patch", "-s", "-d", str(out), "-p1"], stdin=pf).returncode
    if rc != 0:
        fail("snt_nano.patch did not apply")

    check = (out / "snt_nano.c").read_text()
    if "defined(SNT_NANO_W_Q4)" not in check or "SNT_NANO_PW_BATCH > 1" not in check:
        fail("patch markers missing after apply")
    print(f"prepare_sanotts: prepared {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
