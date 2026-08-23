#!/usr/bin/env python3
"""
VirusTotal artifact scanner using the official VirusTotal Python client (vt-py).
Scans files/packages against VirusTotal, reusing existing analysis reports by SHA-256 hash lookup
to save quota and time, or submitting new files for analysis when unknown.
"""

import argparse
import glob
import hashlib
import os
import sys
import vt


def compute_sha256(filepath: str) -> str:
    sha256 = hashlib.sha256()
    with open(filepath, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            sha256.update(chunk)
    return sha256.hexdigest()


def format_markdown_table(results: list[dict]) -> str:
    lines = [
        "## 🛡️ VirusTotal Scan Results",
        "",
        "| File | SHA-256 | Status | Detection Rate | VT Report |",
        "| :--- | :--- | :---: | :---: | :---: |",
    ]

    for r in results:
        sha_short = r["sha256"][:12] + "..."
        if r["status"] in ("EXISTS", "SCANNED"):
            detect_str = f"**{r['malicious']}** / {r['total_engines']}"
            if r["malicious"] > 0:
                status_icon = f"🔴 ALERT ({r['malicious']} detections)"
            elif r["suspicious"] > 0:
                status_icon = f"⚠️ SUSPICIOUS ({r['suspicious']})"
            else:
                status_icon = "🟢 CLEAN"
        elif r["status"] == "UPLOAD_FAILED":
            status_icon = "❌ UPLOAD FAILED"
            detect_str = "N/A"
        else:
            status_icon = "⏱️ TIMEOUT"
            detect_str = "N/A"

        link_str = f"[View Report]({r['gui_link']})"
        lines.append(
            f"| `{r['filename']}` | `{sha_short}` | {status_icon} | {detect_str} | {link_str} |"
        )

    lines.append("")
    return "\n".join(lines)


def scan_file_with_vt(client: vt.Client, filepath: str) -> dict:
    filename = os.path.basename(filepath)
    sha256_hash = compute_sha256(filepath)
    file_size = os.path.getsize(filepath)
    gui_link = f"https://www.virustotal.com/gui/file/{sha256_hash}"

    print(f"\n[+] Scanning: {filename} ({file_size} bytes)")
    print(f"    SHA-256: {sha256_hash}")

    # Step 1: Check if file hash already exists on VirusTotal using vt-py
    print("  [*] Querying VirusTotal database by hash...")
    try:
        obj = client.get_object(f"/files/{sha256_hash}")
        print("  [✓] File hash found in VirusTotal database!")
        stats = getattr(obj, "last_analysis_stats", {})
        malicious = stats.get("malicious", 0)
        suspicious = stats.get("suspicious", 0)
        harmless = stats.get("harmless", 0)
        undetected = stats.get("undetected", 0)
        total = malicious + suspicious + harmless + undetected

        return {
            "filename": filename,
            "filepath": filepath,
            "sha256": sha256_hash,
            "status": "EXISTS",
            "malicious": malicious,
            "suspicious": suspicious,
            "undetected": undetected,
            "harmless": harmless,
            "total_engines": total,
            "gui_link": gui_link,
        }
    except vt.APIError as e:
        if e.code != "NotFoundError":
            print(f"  [!] VirusTotal API error during lookup: {e}", file=sys.stderr)

    # Step 2: Hash not found, upload file for analysis using vt-py client
    print("  [*] Hash not found. Submitting file for analysis via vt-py...")
    try:
        with open(filepath, "rb") as f:
            analysis = client.scan_file(f, wait_for_completion=True)

        stats = getattr(analysis, "stats", {})
        malicious = stats.get("malicious", 0)
        suspicious = stats.get("suspicious", 0)
        harmless = stats.get("harmless", 0)
        undetected = stats.get("undetected", 0)
        total = malicious + suspicious + harmless + undetected

        return {
            "filename": filename,
            "filepath": filepath,
            "sha256": sha256_hash,
            "status": "SCANNED",
            "malicious": malicious,
            "suspicious": suspicious,
            "undetected": undetected,
            "harmless": harmless,
            "total_engines": total,
            "gui_link": gui_link,
        }
    except Exception as e:
        print(f"  [!] Failed to upload/analyze file: {e}", file=sys.stderr)
        return {
            "filename": filename,
            "filepath": filepath,
            "sha256": sha256_hash,
            "status": "UPLOAD_FAILED",
            "malicious": -1,
            "suspicious": -1,
            "undetected": -1,
            "harmless": -1,
            "total_engines": 0,
            "gui_link": gui_link,
        }


def main():
    parser = argparse.ArgumentParser(
        description="Scan files using official VirusTotal Python client (vt-py)."
    )
    parser.add_argument(
        "files",
        nargs="+",
        help="Files or glob patterns to scan (e.g. dist/*)",
    )
    parser.add_argument(
        "--api-key",
        default=os.environ.get("VT_API_KEY"),
        help="VirusTotal API Key (defaults to VT_API_KEY env var)",
    )
    parser.add_argument(
        "--max-positives",
        type=int,
        default=0,
        help="Maximum allowed malicious detections before failing (default: 0)",
    )
    parser.add_argument(
        "--warn-only",
        action="store_true",
        help="Do not exit with error code even if detections exceed threshold",
    )
    parser.add_argument(
        "--output-markdown",
        help="Path to write the markdown summary report to a file",
    )

    args = parser.parse_args()

    if not args.api_key:
        print(
            "[!] Notice: VirusTotal API key (VT_API_KEY) is not set. Skipping VirusTotal scan.",
            file=sys.stderr,
        )
        sys.exit(0)

    target_files = []
    for pattern in args.files:
        matched = glob.glob(pattern)
        if matched:
            for p in matched:
                if os.path.isfile(p):
                    target_files.append(p)
        elif os.path.isfile(pattern):
            target_files.append(pattern)

    if not target_files:
        print("[-] Error: No valid files found to scan.", file=sys.stderr)
        sys.exit(1)

    results = []
    any_failed = False

    with vt.Client(args.api_key) as client:
        for filepath in target_files:
            res = scan_file_with_vt(client, filepath)
            results.append(res)

            if res["malicious"] > args.max_positives:
                any_failed = True
            elif res["status"] in ("UPLOAD_FAILED", "TIMEOUT"):
                any_failed = True

    # Output Summary
    md_summary = format_markdown_table(results)
    print("\n" + md_summary)

    if args.output_markdown:
        try:
            os.makedirs(os.path.dirname(os.path.abspath(args.output_markdown)), exist_ok=True)
            with open(args.output_markdown, "w", encoding="utf-8") as f:
                f.write(md_summary + "\n")
            print(f"[+] Wrote markdown report to {args.output_markdown}")
        except Exception as e:
            print(f"[!] Failed to write to {args.output_markdown}: {e}", file=sys.stderr)

    github_summary_file = os.environ.get("GITHUB_STEP_SUMMARY")
    if github_summary_file:
        try:
            with open(github_summary_file, "a", encoding="utf-8") as f:
                f.write(md_summary + "\n")
            print(f"[+] Appended scan summary to {github_summary_file}")
        except Exception as e:
            print(f"[!] Failed to write to GITHUB_STEP_SUMMARY: {e}", file=sys.stderr)

    if any_failed and not args.warn_only:
        print("\n❌ VirusTotal scan failed quality checks!", file=sys.stderr)
        sys.exit(1)
    else:
        print("\n✅ VirusTotal scan completed successfully.", file=sys.stderr)
        sys.exit(0)


if __name__ == "__main__":
    main()
