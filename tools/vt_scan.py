#!/usr/bin/env python3
"""
VirusTotal v3 API artifact scanner for GitHub Actions and local CLI use.
Scans files/packages against VirusTotal, reusing existing analysis reports by SHA-256 hash lookup
to save quota and time, or submitting new files for analysis when unknown.
"""

import argparse
import glob
import hashlib
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


def compute_sha256(filepath: str) -> str:
    sha256 = hashlib.sha256()
    with open(filepath, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            sha256.update(chunk)
    return sha256.hexdigest()


class VirusTotalScanner:
    BASE_URL = "https://www.virustotal.com/api/v3"

    def __init__(self, api_key: str, poll_interval: int = 15, timeout: int = 300):
        self.api_key = api_key
        self.poll_interval = poll_interval
        self.timeout = timeout

    def _make_request(
        self,
        url: str,
        method: str = "GET",
        headers: dict = None,
        data: bytes = None,
        max_retries: int = 5,
    ) -> tuple[int, dict]:
        if headers is None:
            headers = {}
        headers["x-api-key"] = self.api_key
        headers["Accept"] = "application/json"

        req = urllib.request.Request(url, data=data, headers=headers, method=method)

        for attempt in range(max_retries):
            try:
                with urllib.request.urlopen(req) as resp:
                    resp_data = resp.read().decode("utf-8")
                    return resp.status, json.loads(resp_data) if resp_data else {}
            except urllib.error.HTTPError as e:
                resp_body = e.read().decode("utf-8")
                if e.code == 404:
                    return 404, {}
                elif e.code == 429:
                    print(
                        f"  [!] Rate limit reached (HTTP 429). Waiting 20s (attempt {attempt + 1}/{max_retries})...",
                        file=sys.stderr,
                    )
                    time.sleep(20)
                    continue
                else:
                    print(
                        f"  [!] HTTP Error {e.code}: {resp_body}",
                        file=sys.stderr,
                    )
                    return e.code, {}
            except urllib.error.URLError as e:
                print(
                    f"  [!] Network error: {e.reason} (attempt {attempt + 1}/{max_retries})",
                    file=sys.stderr,
                )
                time.sleep(5)
                continue

        return 0, {}

    def get_file_report(self, sha256_hash: str) -> dict | None:
        url = f"{self.BASE_URL}/files/{sha256_hash}"
        status, data = self._make_request(url, method="GET")
        if status == 200 and "data" in data:
            return data["data"]
        return None

    def upload_file(self, filepath: str) -> str | None:
        file_size = os.path.getsize(filepath)

        # Get special upload URL if file is larger than 32MB
        upload_endpoint = f"{self.BASE_URL}/files"
        if file_size > 32 * 1024 * 1024:
            status, resp = self._make_request(
                f"{self.BASE_URL}/files/upload_url", method="GET"
            )
            if status == 200 and "data" in resp:
                upload_endpoint = resp["data"]

        boundary = (
            "----VTScanBoundary"
            + hashlib.md5(str(time.time()).encode()).hexdigest()
        )
        body = []
        body.append(f"--{boundary}".encode("utf-8"))
        body.append(
            f'Content-Disposition: form-data; name="file"; filename="{os.path.basename(filepath)}"'.encode(
                "utf-8"
            )
        )
        body.append(b"Content-Type: application/octet-stream")
        body.append(b"")
        with open(filepath, "rb") as f:
            body.append(f.read())
        body.append(f"--{boundary}--".encode("utf-8"))
        body.append(b"")
        payload = b"\r\n".join(body)

        headers = {
            "Content-Type": f"multipart/form-data; boundary={boundary}",
            "Content-Length": str(len(payload)),
        }

        status, resp = self._make_request(
            upload_endpoint, method="POST", headers=headers, data=payload
        )
        if status == 200 and "data" in resp and "id" in resp["data"]:
            return resp["data"]["id"]
        return None

    def poll_analysis(self, analysis_id: str) -> dict | None:
        url = f"{self.BASE_URL}/analyses/{analysis_id}"
        start_time = time.time()

        while time.time() - start_time < self.timeout:
            status, resp = self._make_request(url, method="GET")
            if status == 200 and "data" in resp:
                attr = resp["data"].get("attributes", {})
                analysis_status = attr.get("status")
                if analysis_status == "completed":
                    return attr
                print(
                    f"  [...] Analysis in progress ({analysis_status}). Waiting {self.poll_interval}s...",
                    file=sys.stderr,
                )
            time.sleep(self.poll_interval)

        print(
            f"  [!] Analysis timed out after {self.timeout}s", file=sys.stderr
        )
        return None

    def scan_file(self, filepath: str) -> dict:
        filename = os.path.basename(filepath)
        sha256_hash = compute_sha256(filepath)
        file_size = os.path.getsize(filepath)
        gui_link = f"https://www.virustotal.com/gui/file/{sha256_hash}"

        print(f"\n[+] Scanning: {filename} ({file_size} bytes)")
        print(f"    SHA-256: {sha256_hash}")

        # Step 1: Try lookup by hash first to save API quota and time
        print("  [*] Querying VirusTotal database by hash...")
        report = self.get_file_report(sha256_hash)

        if report:
            print("  [✓] File hash found in VirusTotal database!")
            stats = (
                report.get("attributes", {})
                .get("last_analysis_stats", {})
            )
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

        # Step 2: Hash not found, upload file for analysis
        print("  [*] Hash not found. Submitting file for analysis...")
        analysis_id = self.upload_file(filepath)
        if not analysis_id:
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

        print(f"  [*] Uploaded successfully. Analysis ID: {analysis_id}")
        attr = self.poll_analysis(analysis_id)

        if not attr:
            return {
                "filename": filename,
                "filepath": filepath,
                "sha256": sha256_hash,
                "status": "TIMEOUT",
                "malicious": -1,
                "suspicious": -1,
                "undetected": -1,
                "harmless": -1,
                "total_engines": 0,
                "gui_link": gui_link,
            }

        stats = attr.get("stats", {})
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


def main():
    parser = argparse.ArgumentParser(
        description="Scan files with VirusTotal v3 API."
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
        "--poll-interval",
        type=int,
        default=15,
        help="Seconds between analysis status checks (default: 15)",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=300,
        help="Max seconds to wait for analysis completion (default: 300)",
    )
    parser.add_argument(
        "--output-markdown",
        help="Path to write the markdown summary report to a file",
    )

    args = parser.parse_args()

    if not args.api_key:
        print(
            "[-] Error: VirusTotal API key is missing. Set VT_API_KEY environment variable or pass --api-key.",
            file=sys.stderr,
        )
        sys.exit(1)

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

    scanner = VirusTotalScanner(
        api_key=args.api_key,
        poll_interval=args.poll_interval,
        timeout=args.timeout,
    )

    results = []
    any_failed = False

    for idx, filepath in enumerate(target_files):
        # Pause briefly between files if multiple to respect rate limits
        if idx > 0:
            time.sleep(5)

        res = scanner.scan_file(filepath)
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
