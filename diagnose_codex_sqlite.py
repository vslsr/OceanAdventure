#!/usr/bin/env python3
"""Diagnose Codex SQLite access problems without changing Codex data.

The default run is read-only. Pass --write-probe to create and immediately
remove a small temporary file in relevant directories when an actual write
test is needed.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import getpass
import json
import os
import platform
import shutil
import sqlite3
import stat
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple


LEVEL_ORDER = {"PASS": 0, "INFO": 0, "WARN": 1, "FAIL": 2}
DATABASE_SUFFIXES = (".sqlite", ".sqlite3", ".db")
SIDECAR_SUFFIXES = ("-wal", "-shm", "-journal")
NETWORK_FILESYSTEMS = {
    "9p",
    "afs",
    "cifs",
    "fuse.sshfs",
    "ncpfs",
    "nfs",
    "nfs4",
    "smbfs",
    "sshfs",
}
LOG_ERROR_PATTERNS = {
    "database-locked": ("database is locked", "database table is locked"),
    "database-readonly": ("readonly database", "read-only database"),
    "database-open": ("unable to open database", "cannot open database"),
    "database-io": ("disk i/o error", "database disk image is malformed"),
    "disk-full": ("database or disk is full", "no space left on device"),
    "sqlite": ("sqlite",),
}


class Report:
    def __init__(self) -> None:
        self.findings: List[Dict[str, Any]] = []
        self.context: Dict[str, Any] = {}
        self.databases: List[Dict[str, Any]] = []

    def add(self, level: str, code: str, message: str, **details: Any) -> None:
        item: Dict[str, Any] = {
            "level": level,
            "code": code,
            "message": message,
        }
        if details:
            item["details"] = details
        self.findings.append(item)
        marker = {"PASS": "[+]", "INFO": "[i]", "WARN": "[!]", "FAIL": "[x]"}[level]
        print(f"{marker} {message}")
        for key, value in details.items():
            print(f"    {key}: {value}")

    @property
    def exit_code(self) -> int:
        worst = max((LEVEL_ORDER[item["level"]] for item in self.findings), default=0)
        return 1 if worst > 0 else 0

    def as_dict(self) -> Dict[str, Any]:
        counts = {level: 0 for level in LEVEL_ORDER}
        for finding in self.findings:
            counts[finding["level"]] += 1
        return {
            "schema_version": 1,
            "generated_at": dt.datetime.now(dt.timezone.utc).isoformat(),
            "context": self.context,
            "summary": counts,
            "findings": self.findings,
            "databases": self.databases,
        }


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Diagnose Codex SQLite path, permission, locking, disk and integrity issues."
    )
    parser.add_argument(
        "--codex-home",
        type=Path,
        help="Codex data directory. Default: CODEX_HOME, otherwise ~/.codex.",
    )
    parser.add_argument(
        "--sqlite-home",
        type=Path,
        help=(
            "SQLite state directory. Default precedence: config.toml sqlite_home, "
            "CODEX_SQLITE_HOME, then Codex home."
        ),
    )
    parser.add_argument(
        "--database",
        "--db",
        action="append",
        type=Path,
        default=[],
        help="Inspect only this database; repeat for more than one database.",
    )
    parser.add_argument(
        "--skip-quick-check",
        action="store_true",
        help="Open databases read-only but skip PRAGMA quick_check(1).",
    )
    parser.add_argument(
        "--check-timeout",
        type=float,
        default=10.0,
        help="Maximum quick-check time per database in seconds (default: 10).",
    )
    parser.add_argument(
        "--sqlite-lock-timeout",
        type=float,
        default=1.0,
        help="SQLite lock wait in seconds (default: 1).",
    )
    parser.add_argument(
        "--max-databases",
        type=int,
        default=200,
        help="Maximum automatically discovered databases (default: 200).",
    )
    parser.add_argument(
        "--write-probe",
        action="store_true",
        help="Create and remove a temporary probe file to verify real directory writes.",
    )
    parser.add_argument(
        "--json",
        dest="json_path",
        type=Path,
        help="Also write a machine-readable JSON report to this path.",
    )
    args = parser.parse_args(argv)
    if args.check_timeout <= 0 or args.sqlite_lock_timeout < 0:
        parser.error("timeouts must be positive (SQLite lock timeout may be zero)")
    if args.max_databases <= 0:
        parser.error("--max-databases must be positive")
    return args


def resolved_codex_home(explicit: Optional[Path]) -> Tuple[Path, str]:
    if explicit is not None:
        return explicit.expanduser().absolute(), "--codex-home"
    configured = os.environ.get("CODEX_HOME")
    if configured:
        return Path(configured).expanduser().absolute(), "CODEX_HOME"
    return (Path.home() / ".codex").absolute(), "~/.codex"


def resolved_sqlite_home(
    explicit: Optional[Path], codex_home: Path, configured: Optional[Path]
) -> Tuple[Path, str]:
    if explicit is not None:
        return explicit.expanduser().absolute(), "--sqlite-home"
    if configured is not None:
        return configured, "config.toml sqlite_home"
    environment_value = os.environ.get("CODEX_SQLITE_HOME")
    if environment_value:
        candidate = Path(environment_value).expanduser()
        if not candidate.is_absolute():
            candidate = Path.cwd() / candidate
        return candidate.absolute(), "CODEX_SQLITE_HOME"
    return codex_home, "Codex home"


def selected_environment() -> Dict[str, Optional[str]]:
    names = (
        "CODEX_HOME",
        "CODEX_SQLITE_HOME",
        "HOME",
        "USERPROFILE",
        "TMPDIR",
        "TEMP",
        "TMP",
        "XDG_CONFIG_HOME",
    )
    return {name: os.environ.get(name) for name in names}


def permission_details(path: Path) -> Dict[str, Any]:
    details: Dict[str, Any] = {
        "path": str(path),
        "exists": path.exists(),
        "readable": os.access(path, os.R_OK),
        "writable": os.access(path, os.W_OK),
    }
    if path.is_dir():
        details["searchable"] = os.access(path, os.X_OK)
    try:
        info = path.stat()
        details["mode"] = oct(stat.S_IMODE(info.st_mode))
        details["uid"] = getattr(info, "st_uid", None)
        details["gid"] = getattr(info, "st_gid", None)
    except OSError as exc:
        details["stat_error"] = f"{type(exc).__name__}: {exc}"
    return details


def nearest_existing(path: Path) -> Optional[Path]:
    candidate = path
    while not candidate.exists() and candidate != candidate.parent:
        candidate = candidate.parent
    return candidate if candidate.exists() else None


def write_probe(directory: Path) -> Tuple[bool, str]:
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", prefix=".codex-sqlite-write-probe-", dir=str(directory), delete=True
        ) as handle:
            handle.write(b"codex sqlite diagnostic probe\n")
            handle.flush()
            os.fsync(handle.fileno())
        return True, "temporary file creation, flush and removal succeeded"
    except OSError as exc:
        return False, f"{type(exc).__name__}: {exc}"


def filesystem_info(path: Path) -> Dict[str, Any]:
    target = nearest_existing(path)
    if target is None:
        return {"type": "unknown", "network": None}

    if os.name == "nt":
        try:
            import ctypes

            text = str(target.resolve())
            if text.startswith("\\\\"):
                root = "\\\\" + "\\".join(text.lstrip("\\").split("\\")[:2]) + "\\"
            else:
                root = target.resolve().anchor
            drive_type = ctypes.windll.kernel32.GetDriveTypeW(root)
            labels = {
                0: "unknown",
                1: "no-root",
                2: "removable",
                3: "fixed",
                4: "remote",
                5: "cdrom",
                6: "ramdisk",
            }
            return {
                "type": labels.get(drive_type, f"windows-drive-{drive_type}"),
                "mount": root,
                "network": drive_type == 4,
            }
        except Exception as exc:  # Diagnostic fallback must not abort the report.
            return {"type": "unknown", "network": None, "error": str(exc)}

    mounts_file = Path("/proc/mounts")
    if mounts_file.exists():
        best: Optional[Tuple[Path, str, str]] = None
        try:
            for raw_line in mounts_file.read_text(encoding="utf-8", errors="replace").splitlines():
                fields = raw_line.split()
                if len(fields) < 3:
                    continue
                mount_text = fields[1].replace("\\040", " ").replace("\\011", "\t")
                mount = Path(mount_text)
                try:
                    target.resolve().relative_to(mount.resolve())
                except (OSError, ValueError):
                    continue
                if best is None or len(str(mount)) > len(str(best[0])):
                    best = (mount, fields[2], fields[0])
            if best is not None:
                fs_type = best[1].lower()
                is_network = fs_type in NETWORK_FILESYSTEMS or fs_type.startswith("fuse.sshfs")
                return {
                    "type": fs_type,
                    "mount": str(best[0]),
                    "device": best[2],
                    "network": is_network,
                }
        except OSError as exc:
            return {"type": "unknown", "network": None, "error": str(exc)}

    if platform.system() == "Darwin":
        try:
            completed = subprocess.run(
                ["stat", "-f", "%T", str(target)],
                check=False,
                capture_output=True,
                text=True,
                timeout=3,
            )
            fs_type = completed.stdout.strip().lower() or "unknown"
            return {
                "type": fs_type,
                "network": any(token in fs_type for token in ("nfs", "smb", "afp", "sshfs")),
            }
        except (OSError, subprocess.SubprocessError):
            pass
    return {"type": "unknown", "network": None}


def discover_databases(root: Path, maximum: int) -> List[Path]:
    if not root.is_dir():
        return []
    found: List[Path] = []
    try:
        for directory, subdirs, files in os.walk(root):
            current = Path(directory)
            try:
                depth = len(current.relative_to(root).parts)
            except ValueError:
                depth = 99
            if depth >= 4:
                subdirs[:] = []
            subdirs[:] = [name for name in subdirs if name not in {"attachments", "cache", "tmp"}]
            for name in files:
                lower = name.lower()
                if lower.endswith(DATABASE_SUFFIXES) and not lower.endswith(SIDECAR_SUFFIXES):
                    found.append(current / name)
                    if len(found) >= maximum:
                        return sorted(found, key=lambda item: str(item).lower())
    except OSError:
        pass
    return sorted(found, key=lambda item: str(item).lower())


def sqlite_error(exc: BaseException) -> Dict[str, Any]:
    result: Dict[str, Any] = {"type": type(exc).__name__, "message": str(exc)}
    for attr in ("sqlite_errorcode", "sqlite_errorname"):
        value = getattr(exc, attr, None)
        if value is not None:
            result[attr] = value
    return result


def inspect_database(
    path: Path,
    report: Report,
    skip_quick_check: bool,
    check_timeout: float,
    lock_timeout: float,
) -> None:
    print(f"\nDatabase: {path}")
    entry: Dict[str, Any] = {"path": str(path), "permissions": permission_details(path)}
    report.databases.append(entry)

    if not path.exists():
        report.add("FAIL", "database-missing", "Database path does not exist.", path=str(path))
        return
    if not path.is_file():
        report.add("FAIL", "database-not-file", "Database path is not a regular file.", path=str(path))
        return

    try:
        file_info = path.stat()
        entry["size_bytes"] = file_info.st_size
        entry["modified_at"] = dt.datetime.fromtimestamp(
            file_info.st_mtime, dt.timezone.utc
        ).isoformat()
        with path.open("rb") as handle:
            header = handle.read(16)
        entry["sqlite_header_valid"] = header == b"SQLite format 3\x00"
        if entry["sqlite_header_valid"]:
            report.add("PASS", "sqlite-header", "SQLite file header is valid.")
        elif file_info.st_size == 0:
            report.add("FAIL", "database-empty", "Database file is empty.")
        else:
            report.add("FAIL", "sqlite-header-invalid", "File does not have a valid SQLite header.")
    except OSError as exc:
        report.add(
            "FAIL",
            "database-read-failed",
            "Database cannot be read at the filesystem level.",
            error=f"{type(exc).__name__}: {exc}",
        )
        return

    db_permissions = entry["permissions"]
    parent_permissions = permission_details(path.parent)
    entry["parent_permissions"] = parent_permissions
    if not db_permissions.get("readable"):
        report.add("FAIL", "database-unreadable", "Database file is not readable by this account.")
    if not db_permissions.get("writable"):
        report.add("FAIL", "database-unwritable", "Database file is not writable by this account.")
    if not parent_permissions.get("writable") or not parent_permissions.get("searchable", True):
        report.add(
            "FAIL",
            "database-parent-unwritable",
            "Database directory is not writable/searchable; SQLite cannot manage WAL/SHM files.",
            directory=str(path.parent),
        )

    sidecars: Dict[str, Any] = {}
    for suffix in SIDECAR_SUFFIXES:
        sidecar = Path(str(path) + suffix)
        if sidecar.exists():
            try:
                sidecars[suffix] = {
                    "size_bytes": sidecar.stat().st_size,
                    "permissions": permission_details(sidecar),
                }
            except OSError as exc:
                sidecars[suffix] = {"error": str(exc)}
    entry["sidecars"] = sidecars
    if sidecars:
        report.add(
            "INFO",
            "sqlite-sidecars-present",
            "SQLite sidecar files are present; this is normal for an active WAL database.",
            sidecars=", ".join(sorted(sidecars)),
        )
        for suffix, sidecar_info in sidecars.items():
            perms = sidecar_info.get("permissions", {})
            if not perms.get("readable") or not perms.get("writable"):
                report.add(
                    "FAIL",
                    "sqlite-sidecar-permission",
                    f"SQLite sidecar {suffix} is not readable and writable by this account.",
                )

    uri = path.resolve().as_uri() + "?mode=ro"
    connection: Optional[sqlite3.Connection] = None
    try:
        connection = sqlite3.connect(uri, uri=True, timeout=lock_timeout)
        connection.execute("PRAGMA query_only = ON")
        journal_mode = connection.execute("PRAGMA journal_mode").fetchone()
        schema_version = connection.execute("PRAGMA schema_version").fetchone()
        entry["sqlite_open"] = "ok"
        entry["journal_mode"] = journal_mode[0] if journal_mode else None
        entry["schema_version"] = schema_version[0] if schema_version else None
        report.add(
            "PASS",
            "sqlite-readonly-open",
            "SQLite opened the database in read-only mode.",
            journal_mode=entry["journal_mode"],
        )

        if not skip_quick_check:
            deadline = time.monotonic() + check_timeout

            def stop_when_expired() -> int:
                return 1 if time.monotonic() >= deadline else 0

            connection.set_progress_handler(stop_when_expired, 10_000)
            try:
                row = connection.execute("PRAGMA quick_check(1)").fetchone()
                quick_result = row[0] if row else "no result"
                entry["quick_check"] = quick_result
                if quick_result == "ok":
                    report.add("PASS", "sqlite-quick-check", "SQLite quick_check returned ok.")
                else:
                    report.add(
                        "FAIL",
                        "sqlite-integrity",
                        "SQLite quick_check reported an integrity problem.",
                        result=quick_result,
                    )
            except sqlite3.Error as exc:
                entry["quick_check"] = sqlite_error(exc)
                message = str(exc).lower()
                level = "WARN" if "interrupt" in message else "FAIL"
                code = "sqlite-check-timeout" if "interrupt" in message else "sqlite-check-failed"
                report.add(level, code, "SQLite quick_check could not complete.", **sqlite_error(exc))
            finally:
                connection.set_progress_handler(None, 0)
    except sqlite3.Error as exc:
        entry["sqlite_open"] = sqlite_error(exc)
        text = str(exc).lower()
        if "locked" in text or "busy" in text:
            code = "sqlite-locked"
        elif "readonly" in text:
            code = "sqlite-readonly"
        elif "malformed" in text or "not a database" in text:
            code = "sqlite-corrupt"
        else:
            code = "sqlite-open-failed"
        report.add("FAIL", code, "SQLite could not open the database read-only.", **sqlite_error(exc))
    finally:
        if connection is not None:
            connection.close()


def linux_open_database_handles(databases: Iterable[Path]) -> List[Dict[str, Any]]:
    if not sys.platform.startswith("linux") or not Path("/proc").is_dir():
        return []
    targets = {str(path.resolve()): str(path) for path in databases if path.exists()}
    handles: List[Dict[str, Any]] = []
    for process_dir in Path("/proc").iterdir():
        if not process_dir.name.isdigit():
            continue
        fd_dir = process_dir / "fd"
        try:
            process_name = (process_dir / "comm").read_text(errors="replace").strip()
            user_id = process_dir.stat().st_uid
            for descriptor in fd_dir.iterdir():
                try:
                    linked = os.readlink(descriptor)
                except OSError:
                    continue
                normalized = linked[:-10] if linked.endswith(" (deleted)") else linked
                if normalized in targets:
                    handles.append(
                        {
                            "pid": int(process_dir.name),
                            "uid": user_id,
                            "process": process_name,
                            "database": targets[normalized],
                            "fd": descriptor.name,
                            "deleted": linked.endswith(" (deleted)"),
                        }
                    )
        except (OSError, PermissionError):
            continue
    return handles[:100]


def relevant_processes() -> List[Dict[str, Any]]:
    matches: List[Dict[str, Any]] = []
    wanted = ("codex", "chatgpt")
    if sys.platform.startswith("linux") and Path("/proc").is_dir():
        for process_dir in Path("/proc").iterdir():
            if not process_dir.name.isdigit():
                continue
            try:
                name = (process_dir / "comm").read_text(errors="replace").strip()
                if any(token in name.lower() for token in wanted):
                    matches.append({"pid": int(process_dir.name), "process": name})
            except OSError:
                continue
    elif os.name == "nt":
        try:
            completed = subprocess.run(
                ["tasklist", "/FO", "CSV", "/NH"],
                capture_output=True,
                text=True,
                check=False,
                timeout=5,
            )
            for row in csv.reader(completed.stdout.splitlines()):
                if len(row) >= 2 and any(token in row[0].lower() for token in wanted):
                    matches.append({"pid": row[1], "process": row[0]})
        except (OSError, subprocess.SubprocessError):
            pass
    else:
        try:
            completed = subprocess.run(
                ["ps", "-axo", "pid=,user=,comm="],
                capture_output=True,
                text=True,
                check=False,
                timeout=5,
            )
            for line in completed.stdout.splitlines():
                fields = line.strip().split(None, 2)
                if len(fields) == 3 and any(token in fields[2].lower() for token in wanted):
                    matches.append({"pid": fields[0], "user": fields[1], "process": fields[2]})
        except (OSError, subprocess.SubprocessError):
            pass
    return matches[:100]


def config_locations(codex_home: Path, report: Report) -> Dict[str, Path]:
    result: Dict[str, Path] = {}
    config_path = codex_home / "config.toml"
    if not config_path.is_file():
        return result
    try:
        try:
            import tomllib
        except ImportError:
            report.add(
                "INFO",
                "config-parse-skipped",
                "Python is older than 3.11; config.toml path parsing was skipped.",
            )
            return result
        with config_path.open("rb") as handle:
            config = tomllib.load(handle)
        report.add("PASS", "config-readable", "Codex config.toml is readable and valid TOML.")
        for key in ("log_dir", "sqlite_home"):
            value = config.get(key)
            if not isinstance(value, str) or not value:
                continue
            candidate = Path(value).expanduser()
            if not candidate.is_absolute():
                if key == "log_dir":
                    report.add(
                        "WARN",
                        "log-dir-relative",
                        "Configured log_dir is relative; its meaning depends on the process working directory.",
                        log_dir=value,
                    )
                candidate = (Path.cwd() / candidate).absolute()
            result[key] = candidate
    except (OSError, ValueError) as exc:
        report.add(
            "WARN",
            "config-unreadable",
            "Could not read or parse config.toml.",
            error=f"{type(exc).__name__}: {exc}",
        )
    return result


def scan_log_errors(directories: Iterable[Path], report: Report) -> None:
    candidates: List[Path] = []
    seen: set[str] = set()
    for directory in directories:
        if not directory.is_dir():
            continue
        try:
            for current, subdirs, files in os.walk(directory):
                current_path = Path(current)
                try:
                    depth = len(current_path.relative_to(directory).parts)
                except ValueError:
                    depth = 99
                if depth >= 2:
                    subdirs[:] = []
                for name in files:
                    if not name.lower().endswith(".log"):
                        continue
                    path = current_path / name
                    key = str(path.resolve())
                    if key not in seen and path.is_file():
                        seen.add(key)
                        candidates.append(path)
                        if len(candidates) >= 100:
                            break
                if len(candidates) >= 100:
                    break
        except OSError:
            continue
    candidates.sort(key=lambda item: item.stat().st_mtime if item.exists() else 0, reverse=True)
    matches: List[Dict[str, Any]] = []
    for path in candidates[:10]:
        try:
            if path.stat().st_size > 10 * 1024 * 1024:
                continue
            for number, line in enumerate(
                path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1
            ):
                lower = line.lower()
                category = next(
                    (
                        name
                        for name, patterns in LOG_ERROR_PATTERNS.items()
                        if any(pattern in lower for pattern in patterns)
                    ),
                    None,
                )
                if category:
                    matches.append({"file": str(path), "line": number, "category": category})
                    if len(matches) >= 50:
                        break
            if len(matches) >= 50:
                break
        except OSError:
            continue
    report.context["log_error_matches"] = matches
    if matches:
        counts: Dict[str, int] = {}
        for match in matches:
            counts[match["category"]] = counts.get(match["category"], 0) + 1
        report.add(
            "WARN",
            "sqlite-errors-in-logs",
            "Recent Codex log files contain SQLite/storage error markers (content was not copied).",
            categories=counts,
            matches=len(matches),
        )


def recommendations(report: Report) -> List[str]:
    codes = {item["code"] for item in report.findings}
    advice: List[str] = []
    if {
        "codex-home-missing",
        "sqlite-home-missing",
        "database-missing",
        "no-databases",
    } & codes:
        advice.append(
            "Verify which OS account starts Codex and inspect CODEX_HOME, CODEX_SQLITE_HOME and config.toml sqlite_home for that account."
        )
    if {
        "codex-home-unwritable",
        "sqlite-home-unreadable",
        "sqlite-home-unwritable",
        "database-unreadable",
        "database-unwritable",
        "database-parent-unwritable",
        "sqlite-sidecar-permission",
    } & codes:
        advice.append(
            "Correct ownership/ACLs for the Codex service account on the Codex directory, database files, and WAL/SHM sidecars; do not use world-writable permissions."
        )
    if "network-filesystem" in codes:
        advice.append(
            "Move CODEX_HOME to a local disk. SQLite locking and WAL behavior can be unreliable on NFS/SMB/SSHFS-style mounts."
        )
    if {"low-disk-space", "disk-space-failed"} & codes:
        advice.append("Free local disk space and confirm the filesystem is not mounted read-only or quota-limited.")
    if {"sqlite-locked", "open-database-handles"} & codes:
        advice.append(
            "Check for duplicate Codex processes using the same data directory. Stop them cleanly before retrying; do not delete WAL/SHM files while Codex is running."
        )
    if {"sqlite-integrity", "sqlite-corrupt", "sqlite-header-invalid", "database-empty"} & codes:
        advice.append(
            "Stop Codex and make a byte-for-byte backup of the database plus matching WAL/SHM files before attempting recovery or replacement."
        )
    if {"sqlite-open-failed", "sqlite-check-failed"} & codes:
        advice.append(
            "Use the reported SQLite error code together with the filesystem, account and sidecar findings to distinguish permissions, locks and corruption."
        )
    return advice


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    report = Report()
    codex_home, home_source = resolved_codex_home(args.codex_home)
    temp_directory = Path(tempfile.gettempdir())
    codex_binary = shutil.which("codex")

    report.context.update(
        {
            "hostname": platform.node(),
            "platform": platform.platform(),
            "python": sys.version.split()[0],
            "sqlite_library": sqlite3.sqlite_version,
            "account": getpass.getuser(),
            "uid": os.getuid() if hasattr(os, "getuid") else None,
            "euid": os.geteuid() if hasattr(os, "geteuid") else None,
            "cwd": str(Path.cwd()),
            "codex_home": str(codex_home),
            "codex_home_source": home_source,
            "codex_binary": codex_binary,
            "environment": selected_environment(),
            "temp_directory": str(temp_directory),
        }
    )

    print("Codex SQLite diagnostic")
    print("=" * 26)
    print(f"Account:    {report.context['account']}")
    print(f"Platform:   {report.context['platform']}")
    print(f"Codex home: {codex_home} ({home_source})")
    print(f"SQLite:     {sqlite3.sqlite_version} (Python {report.context['python']})")
    print("Mode:       " + ("temporary write probes enabled" if args.write_probe else "read-only"))

    if codex_binary:
        report.add("PASS", "codex-binary", "Codex executable is on PATH.", path=codex_binary)
    else:
        report.add("WARN", "codex-binary-missing", "Codex executable was not found on this account's PATH.")

    if codex_home.is_dir():
        home_permissions = permission_details(codex_home)
        report.context["codex_home_permissions"] = home_permissions
        if home_permissions.get("readable") and home_permissions.get("searchable", True):
            report.add("PASS", "codex-home-readable", "Codex data directory is readable/searchable.")
        else:
            report.add("FAIL", "codex-home-unreadable", "Codex data directory is not readable/searchable.")
        if home_permissions.get("writable"):
            report.add("PASS", "codex-home-writable", "Codex data directory is reported writable.")
        else:
            report.add("FAIL", "codex-home-unwritable", "Codex data directory is not writable.")
    elif codex_home.exists():
        report.add("FAIL", "codex-home-not-directory", "Codex home exists but is not a directory.")
    else:
        report.add(
            "FAIL",
            "codex-home-missing",
            "Codex data directory does not exist for this account.",
            nearest_existing=str(nearest_existing(codex_home)),
        )

    locations = config_locations(codex_home, report)
    log_dir = locations.get("log_dir")
    sqlite_home, sqlite_home_source = resolved_sqlite_home(
        args.sqlite_home, codex_home, locations.get("sqlite_home")
    )
    report.context["configured_log_dir"] = str(log_dir) if log_dir else None
    report.context["sqlite_home"] = str(sqlite_home)
    report.context["sqlite_home_source"] = sqlite_home_source
    print(f"SQLite home: {sqlite_home} ({sqlite_home_source})")

    if sqlite_home != codex_home:
        if sqlite_home.is_dir():
            sqlite_home_permissions = permission_details(sqlite_home)
            report.context["sqlite_home_permissions"] = sqlite_home_permissions
            if sqlite_home_permissions.get("readable") and sqlite_home_permissions.get(
                "searchable", True
            ):
                report.add("PASS", "sqlite-home-readable", "SQLite state directory is readable/searchable.")
            else:
                report.add(
                    "FAIL", "sqlite-home-unreadable", "SQLite state directory is not readable/searchable."
                )
            if sqlite_home_permissions.get("writable"):
                report.add("PASS", "sqlite-home-writable", "SQLite state directory is reported writable.")
            else:
                report.add("FAIL", "sqlite-home-unwritable", "SQLite state directory is not writable.")
        elif sqlite_home.exists():
            report.add(
                "FAIL", "sqlite-home-not-directory", "SQLite home exists but is not a directory."
            )
        else:
            report.add(
                "FAIL",
                "sqlite-home-missing",
                "Configured SQLite state directory does not exist for this account.",
                nearest_existing=str(nearest_existing(sqlite_home)),
            )

    filesystem = filesystem_info(sqlite_home)
    report.context["filesystem"] = filesystem
    if filesystem.get("network") is True:
        report.add(
            "WARN",
            "network-filesystem",
            "Codex SQLite state is on a network/remote filesystem, which can break locking.",
            filesystem=filesystem,
        )
    else:
        report.add(
            "INFO",
            "filesystem",
            "Detected filesystem information.",
            filesystem=filesystem,
        )

    disk_target = nearest_existing(sqlite_home)
    if disk_target is not None:
        try:
            usage = shutil.disk_usage(disk_target)
            report.context["disk"] = {
                "total_bytes": usage.total,
                "used_bytes": usage.used,
                "free_bytes": usage.free,
            }
            if usage.free < 1024 * 1024 * 1024:
                report.add(
                    "WARN",
                    "low-disk-space",
                    "Less than 1 GiB of free disk space remains.",
                    free_bytes=usage.free,
                )
            else:
                report.add("PASS", "disk-space", "Disk has at least 1 GiB free.", free_bytes=usage.free)
        except OSError as exc:
            report.add("WARN", "disk-space-failed", "Could not query disk space.", error=str(exc))

    temp_permissions = permission_details(temp_directory)
    report.context["temp_permissions"] = temp_permissions
    if not temp_directory.is_dir() or not temp_permissions.get("writable"):
        report.add("WARN", "temp-unwritable", "Process temporary directory is missing or not writable.")
    else:
        report.add("PASS", "temp-writable", "Process temporary directory is reported writable.")

    if args.write_probe:
        probe_directories = [("Codex home", codex_home)]
        if sqlite_home != codex_home:
            probe_directories.append(("SQLite home", sqlite_home))
        probe_directories.append(("temporary directory", temp_directory))
        for label, directory in probe_directories:
            if directory.is_dir():
                ok, detail = write_probe(directory)
                report.add(
                    "PASS" if ok else "FAIL",
                    "write-probe-ok" if ok else "write-probe-failed",
                    f"{label} write probe {'succeeded' if ok else 'failed'}.",
                    result=detail,
                )

    if args.database:
        databases = sorted({item.expanduser().absolute() for item in args.database}, key=str)
    else:
        databases = discover_databases(sqlite_home, args.max_databases)
    report.context["database_count"] = len(databases)
    if not databases:
        report.add(
            "WARN",
            "no-databases",
            "No SQLite databases were found; this may indicate the wrong SQLite home or account.",
        )
    else:
        report.add("INFO", "databases-found", f"Found {len(databases)} SQLite database(s).")

    for database in databases:
        inspect_database(
            database,
            report,
            args.skip_quick_check,
            args.check_timeout,
            args.sqlite_lock_timeout,
        )

    handles = linux_open_database_handles(databases)
    report.context["open_database_handles"] = handles
    if handles:
        report.add(
            "WARN",
            "open-database-handles",
            "One or more processes currently have a discovered database open.",
            handles=handles,
        )

    processes = relevant_processes()
    report.context["relevant_processes"] = processes
    if len(processes) > 1:
        report.add(
            "WARN",
            "multiple-codex-processes",
            "Multiple Codex/ChatGPT processes are running; confirm they should share this data directory.",
            processes=processes,
        )
    elif processes:
        report.add("INFO", "codex-process", "A Codex/ChatGPT process is currently running.", processes=processes)

    log_directories = [codex_home / "logs"]
    if log_dir is not None:
        log_directories.append(log_dir)
    scan_log_errors(log_directories, report)

    advice = recommendations(report)
    report.context["recommendations"] = advice
    print("\nSummary")
    print("=" * 7)
    counts = report.as_dict()["summary"]
    print("  ".join(f"{level}={counts[level]}" for level in ("PASS", "INFO", "WARN", "FAIL")))
    if advice:
        print("\nRecommended next steps")
        print("-" * 22)
        for index, item in enumerate(advice, start=1):
            print(f"{index}. {item}")
    else:
        print("No obvious SQLite access problem was detected by these checks.")

    if args.json_path:
        output = args.json_path.expanduser().absolute()
        try:
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(
                json.dumps(report.as_dict(), ensure_ascii=False, indent=2),
                encoding="utf-8",
            )
            print(f"\nJSON report written to: {output}")
        except OSError as exc:
            print(f"\n[x] Could not write JSON report: {type(exc).__name__}: {exc}", file=sys.stderr)
            return 2

    return report.exit_code


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nDiagnostic interrupted.", file=sys.stderr)
        raise SystemExit(130)
