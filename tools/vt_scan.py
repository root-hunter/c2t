#!/usr/bin/env python3
"""
VirusTotal artifact scanner using the official VirusTotal Python client (vt-py).
Scans files/packages against VirusTotal, reusing existing analysis reports by SHA-256 hash lookup
to save quota and time, or submitting new files for analysis when unknown.
Outputs detailed security verification reports including file metadata, hashes, reputation,
threat labels, and per-vendor detection breakdowns.
"""

import argparse
import glob
import hashlib
import os
import sys
import vt


def compute_hashes(filepath: str) -> dict[str, str]:
    md5 = hashlib.md5()
    sha1 = hashlib.sha1()
    sha256 = hashlib.sha256()

    with open(filepath, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            md5.update(chunk)
            sha1.update(chunk)
            sha256.update(chunk)

    return {
        "md5": md5.hexdigest(),
        "sha1": sha1.hexdigest(),
        "sha256": sha256.hexdigest(),
    }


def format_bytes(size: int) -> str:
    for unit in ["B", "KB", "MB", "GB"]:
        if size < 1024.0:
            return f"{size:.2f} {unit}"
        size /= 1024.0
    return f"{size:.2f} TB"


def format_markdown_report(results: list[dict]) -> str:
    lines = [
        "## 🛡️ VirusTotal Security Verification Report",
        "",
        "### 📊 Summary",
        "",
        "| File | Size | SHA-256 | Status | Detection Rate | VT Report |",
        "| :--- | :---: | :--- | :---: | :---: | :---: |",
    ]

    for r in results:
        sha_short = r["hashes"]["sha256"][:12] + "..."
        size_str = format_bytes(r["file_size"])
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
            f"| `{r['filename']}` | `{size_str}` | `{sha_short}` | {status_icon} | {detect_str} | {link_str} |"
        )

    lines.append("")
    lines.append("---")
    lines.append("")
    lines.append("### 🔍 Detailed File Analysis")
    lines.append("")

    for r in results:
        lines.append(f"#### 📄 `{r['filename']}`")
        lines.append(f"- **File Type**: `{r.get('type_description', 'N/A')}`")
        lines.append(f"- **File Size**: `{r['file_size']:,} bytes` ({format_bytes(r['file_size'])})")
        lines.append(f"- **SHA-256**: `{r['hashes']['sha256']}`")
        lines.append(f"- **MD5**: `{r['hashes']['md5']}`")
        lines.append(f"- **SHA-1**: `{r['hashes']['sha1']}`")
        lines.append(f"- **VT Reputation Score**: `{r.get('reputation', 0)}`")

        threat_label = r.get("threat_label")
        if threat_label:
            lines.append(f"- **Suggested Threat Label**: `{threat_label}`")
        else:
            lines.append("- **Threat Classification**: `None (Clean)`")

        lines.append(f"- **VT Direct Link**: [{r['gui_link']}]({r['gui_link']})")
        lines.append("")

        vendor_results = r.get("vendor_results", {})
        detections = [v for v in vendor_results.values() if v.get("category") in ("malicious", "suspicious")]

        if detections:
            lines.append("> [!WARNING]")
            lines.append(f"> **Security Vendor Detections ({len(detections)}):**")
            lines.append(">")
            for d in detections:
                vendor = d.get("engine_name") or d.get("vendor", "Unknown")
                category = d.get("category", "malicious").upper()
                result_name = d.get("result") or "Generic Detection"
                lines.append(f"> - **{vendor}** (`{category}`): `{result_name}`")
            lines.append("")
        else:
            lines.append("🟢 **No security vendors flagged this file as malicious or suspicious.**")
            lines.append("")

        if vendor_results:
            lines.append("<details>")
            lines.append("<summary>🔍 <b>Full Vendor Analysis Breakdown (Click to expand)</b></summary>")
            lines.append("")
            lines.append("| Security Vendor | Category | Detection Result |")
            lines.append("| :--- | :---: | :--- |")

            # Sort vendors alphabetically
            for vendor_name in sorted(vendor_results.keys()):
                info = vendor_results[vendor_name]
                cat = info.get("category", "undetected")
                res = info.get("result") or "-"
                if cat == "malicious":
                    icon = "🔴 Malicious"
                elif cat == "suspicious":
                    icon = "⚠️ Suspicious"
                elif cat == "harmless":
                    icon = "🟢 Clean"
                elif cat == "undetected":
                    icon = "⚪ Undetected"
                else:
                    icon = f"⚪ {cat}"

                lines.append(f"| **{vendor_name}** | {icon} | `{res}` |")

            lines.append("</details>")
            lines.append("")

    return "\n".join(lines)


def scan_file_with_vt(client: vt.Client, filepath: str) -> dict:
    filename = os.path.basename(filepath)
    hashes = compute_hashes(filepath)
    file_size = os.path.getsize(filepath)
    gui_link = f"https://www.virustotal.com/gui/file/{hashes['sha256']}"

    print(f"\n[+] Scanning: {filename} ({file_size} bytes)")
    print(f"    SHA-256: {hashes['sha256']}")
    print(f"    MD5:     {hashes['md5']}")

    # Step 1: Check if file hash already exists on VirusTotal using vt-py
    print("  [*] Querying VirusTotal database by hash...")
    try:
        obj = client.get_object(f"/files/{hashes['sha256']}")
        print("  [✓] File hash found in VirusTotal database!")
        stats = getattr(obj, "last_analysis_stats", {})
        malicious = stats.get("malicious", 0)
        suspicious = stats.get("suspicious", 0)
        harmless = stats.get("harmless", 0)
        undetected = stats.get("undetected", 0)
        total = malicious + suspicious + harmless + undetected

        type_desc = getattr(obj, "type_description", "Unknown")
        reputation = getattr(obj, "reputation", 0)

        threat_class = getattr(obj, "popular_threat_classification", {}) or {}
        threat_label = threat_class.get("suggested_threat_label")

        vendor_results = getattr(obj, "last_analysis_results", {}) or {}

        return {
            "filename": filename,
            "filepath": filepath,
            "hashes": hashes,
            "file_size": file_size,
            "status": "EXISTS",
            "type_description": type_desc,
            "reputation": reputation,
            "threat_label": threat_label,
            "malicious": malicious,
            "suspicious": suspicious,
            "undetected": undetected,
            "harmless": harmless,
            "total_engines": total,
            "vendor_results": vendor_results,
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

        vendor_results = getattr(analysis, "results", {}) or {}

        # Fetch file object metadata if now created
        type_desc = "Unknown"
        reputation = 0
        threat_label = None
        try:
            obj = client.get_object(f"/files/{hashes['sha256']}")
            type_desc = getattr(obj, "type_description", "Unknown")
            reputation = getattr(obj, "reputation", 0)
            threat_class = getattr(obj, "popular_threat_classification", {}) or {}
            threat_label = threat_class.get("suggested_threat_label")
        except Exception:
            pass

        return {
            "filename": filename,
            "filepath": filepath,
            "hashes": hashes,
            "file_size": file_size,
            "status": "SCANNED",
            "type_description": type_desc,
            "reputation": reputation,
            "threat_label": threat_label,
            "malicious": malicious,
            "suspicious": suspicious,
            "undetected": undetected,
            "harmless": harmless,
            "total_engines": total,
            "vendor_results": vendor_results,
            "gui_link": gui_link,
        }
    except Exception as e:
        print(f"  [!] Failed to upload/analyze file: {e}", file=sys.stderr)
        return {
            "filename": filename,
            "filepath": filepath,
            "hashes": hashes,
            "file_size": file_size,
            "status": "UPLOAD_FAILED",
            "type_description": "Unknown",
            "reputation": 0,
            "threat_label": None,
            "malicious": -1,
            "suspicious": -1,
            "undetected": -1,
            "harmless": -1,
            "total_engines": 0,
            "vendor_results": {},
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

    # Output Summary Report
    md_summary = format_markdown_report(results)
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
