#!/usr/bin/env python3
"""WAAPI smoke test for the RealWorld Weather Acoustics Authoring plug-in.

This is test tooling. It intentionally depends on the third-party ``waapi-client``
Python package, but the product build and plug-in do not.
"""

from __future__ import print_function

import argparse
import json
import math
import os
import sys
import time
import traceback
from datetime import datetime, timezone
from pathlib import Path

from waapi import CannotConnectToWaapiException, WaapiClient


SMOKE_NAME = "RWWA_Smoke"
SOURCE_NAME = "RWWA_Smoke_Source"
DEFAULT_SOURCE_CLASS_ID = 2031682562
PROJECT_INFO_URI = "ak.wwise.core.getProjectInfo"


class SmokeAssertionError(RuntimeError):
    pass


def utc_now():
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def canonical_path(value):
    return os.path.normcase(os.path.realpath(os.path.abspath(value)))


def json_safe(value):
    if isinstance(value, float) and not math.isfinite(value):
        if math.isnan(value):
            return "NaN"
        return "Infinity" if value > 0 else "-Infinity"
    if isinstance(value, dict):
        return {str(key): json_safe(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_safe(item) for item in value]
    return value


def write_report(path, report):
    report_path = Path(path)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = report_path.with_suffix(report_path.suffix + ".tmp")
    payload = json.dumps(json_safe(report), indent=2, sort_keys=False, allow_nan=False)
    temporary_path.write_text(payload + "\n", encoding="utf-8")
    os.replace(str(temporary_path), str(report_path))


def record_step(report, name, status, detail=None):
    step = {"name": name, "status": status, "atUtc": utc_now()}
    if detail is not None:
        step["detail"] = detail
    report["steps"].append(step)


def call(client, report, uri, args=None, options=None):
    call_args = args or {}
    if options is None:
        result = client.call(uri, call_args)
    else:
        result = client.call(uri, call_args, options=options)
    if result is None:
        raise RuntimeError("WAAPI call returned no result: {0}".format(uri))
    record_step(report, uri, "passed")
    return result


def connect_with_retry(url, timeout_seconds, report):
    deadline = time.monotonic() + timeout_seconds
    attempts = 0
    last_error = None
    while time.monotonic() < deadline:
        attempts += 1
        try:
            client = WaapiClient(url=url, allow_exception=True)
            report["connection"] = {
                "attempts": attempts,
                "connectedAtUtc": utc_now(),
                "url": url,
            }
            record_step(report, "connectWaapi", "passed", {"attempts": attempts})
            return client
        except CannotConnectToWaapiException as error:
            last_error = str(error)
            time.sleep(0.5)
    raise RuntimeError(
        "WAAPI did not become ready within {0:.1f}s ({1})".format(
            timeout_seconds, last_error or "connection refused"
        )
    )


def smoke_source_definition(source_class_id):
    source = {
        "type": "SourcePlugin",
        "name": SOURCE_NAME,
        "classId": source_class_id,
        "@Duration": 60.0,
        "@MasterGainDb": -6.0,
        "@RainIntensity": 0.85,
        "@Seed": 24681357,
        "@GeometryEnabled": True,
        "@ListenerX": 0.0,
        "@ListenerY": 0.0,
        "@ListenerZ": 0.0,
        "@ListenerYawDegrees": 20.0,
        "@FeatureCount": 4,
    }

    ring = (
        (0.0, 0.0, 6.0),
        (6.0, 0.0, 0.0),
        (0.0, 0.0, -6.0),
        (-6.0, 0.0, 0.0),
    )
    for index, (x_value, y_value, z_value) in enumerate(ring, start=1):
        source["@Feature{0}X".format(index)] = x_value
        source["@Feature{0}Y".format(index)] = y_value
        source["@Feature{0}Z".format(index)] = z_value
        source["@Feature{0}Radius".format(index)] = 2.5
        source["@Feature{0}Profile".format(index)] = index - 1
        source["@Feature{0}Mask".format(index)] = 1
        source["@Feature{0}Priority".format(index)] = 10
    return source


def create_smoke_object(client, report, source_class_id):
    args = {
        "objects": [
            {
                "object": "\\Actor-Mixer Hierarchy\\Default Work Unit",
                "children": [
                    {
                        "type": "Sound",
                        "name": SMOKE_NAME,
                        "notes": "Owned by the automated RealWorld Weather Acoustics Authoring smoke test.",
                        "children": [smoke_source_definition(source_class_id)],
                    }
                ],
            }
        ],
        "onNameConflict": "replace",
        "autoAddToSourceControl": False,
    }
    result = call(
        client,
        report,
        "ak.wwise.core.object.set",
        args,
        {"return": ["id", "name"]},
    )
    try:
        sound = result["objects"][0]["children"][0]
        source = sound["children"][0]
        sound_id = sound["id"]
        source_id = source["id"]
    except (KeyError, IndexError, TypeError) as error:
        raise RuntimeError(
            "object.set did not return the created smoke Sound and SourcePlugin IDs: {0}".format(error)
        )

    report["objectSet"] = {
        "mode": "replace",
        "parent": "\\Actor-Mixer Hierarchy\\Default Work Unit",
        "soundName": SMOKE_NAME,
        "soundId": sound_id,
        "sourceName": SOURCE_NAME,
        "sourceId": source_id,
        "sourceClassId": source_class_id,
        "sourceProperties": smoke_source_definition(source_class_id),
    }
    return sound_id, source_id


def counter_value(counters, counter_id):
    matches = [item for item in counters if item.get("id") == counter_id]
    return matches[-1].get("value") if matches else None


def contains_smoke_identity(value, sound_id, source_id):
    identities = {SMOKE_NAME.lower(), SOURCE_NAME.lower(), sound_id.lower(), source_id.lower()}
    serialized = json.dumps(value, sort_keys=True).lower()
    return any(identity in serialized for identity in identities)


def run_smoke(args, report):
    project_path = Path(args.project).resolve(strict=True)
    client = None
    transport_id = None
    capture_started = False
    capture_artifact_valid = False
    core_assertions_passed = False
    mandatory_cleanup_errors = []
    cleanup_warnings = []

    try:
        client = connect_with_retry(args.waapi_url, args.connect_timeout, report)

        project_info = call(client, report, PROJECT_INFO_URI)
        actual_project_path = project_info.get("path")
        report["project"] = {
            "expectedPath": str(project_path),
            "actualPath": actual_project_path,
            "name": project_info.get("name"),
            "id": project_info.get("id"),
            "queryUri": PROJECT_INFO_URI,
        }
        if not actual_project_path or canonical_path(actual_project_path) != canonical_path(str(project_path)):
            raise RuntimeError(
                "Refusing to modify the project returned by WAAPI. Expected '{0}', got '{1}'.".format(
                    project_path, actual_project_path
                )
            )

        sound_id, source_id = create_smoke_object(client, report, args.source_class_id)
        call(
            client,
            report,
            "ak.wwise.core.project.save",
            {"autoCheckOutToSourceControl": False},
        )

        call(
            client,
            report,
            "ak.wwise.core.profiler.enableProfilerData",
            {
                "dataTypes": [
                    {"dataType": "voices", "enable": True},
                    {"dataType": "cpu", "enable": True},
                ]
            },
        )
        capture_result = call(client, report, "ak.wwise.core.profiler.startCapture")
        capture_started = True
        report["profiler"] = {"captureStartTimeMs": capture_result.get("return")}

        transport_result = call(
            client, report, "ak.wwise.core.transport.create", {"object": sound_id}
        )
        transport_id = transport_result.get("transport")
        if transport_id is None:
            raise RuntimeError("transport.create did not return a transport ID")
        call(
            client,
            report,
            "ak.wwise.core.transport.executeAction",
            {"transport": transport_id, "action": "play"},
        )
        time.sleep(args.playback_seconds)

        transport_state = call(
            client,
            report,
            "ak.wwise.core.transport.getState",
            {"transport": transport_id},
        ).get("state")
        voices = call(
            client,
            report,
            "ak.wwise.core.profiler.getVoices",
            {"time": "capture"},
            {
                "return": [
                    "pipelineID",
                    "playingID",
                    "objectGUID",
                    "objectName",
                    "playTargetGUID",
                    "playTargetName",
                    "isStarted",
                    "isVirtual",
                ]
            },
        ).get("return", [])
        cpu_usage = call(
            client,
            report,
            "ak.wwise.core.profiler.getCpuUsage",
            {"time": "capture"},
        ).get("return", [])
        counters = call(
            client,
            report,
            "ak.wwise.core.profiler.getPerformanceMonitor",
            {"time": "capture"},
        ).get("return", [])

        smoke_voices = [
            voice for voice in voices if contains_smoke_identity(voice, sound_id, source_id)
        ]
        source_cpu = [
            item
            for item in cpu_usage
            if item.get("id") == args.source_class_id
            or "realworld weather" in str(item.get("elementName", "")).lower()
            or "realworldweather" in str(item.get("elementName", "")).lower()
        ]
        voices_physical = counter_value(counters, "VoicesPhysical")
        voices_total = counter_value(counters, "VoicesTotal")
        output_peak = counter_value(counters, "OutputPeak")
        output_peak_finite = isinstance(output_peak, (int, float)) and math.isfinite(output_peak)
        output_peak_audible = output_peak_finite and output_peak > args.silence_floor_db

        # The smoke project is isolated and this transport is the only playback started by
        # the test. A playing transport plus captured voices and a non-zero global voice
        # counter is therefore explainable evidence when Wwise omits object identity fields.
        isolated_transport_evidence = (
            transport_state == "playing"
            and bool(voices)
            and isinstance(voices_total, (int, float))
            and voices_total >= 1
        )
        voice_or_cpu_evidence = bool(smoke_voices or source_cpu or isolated_transport_evidence)

        report["profiler"].update(
            {
                "transportId": transport_id,
                "transportState": transport_state,
                "voices": voices,
                "smokeVoices": smoke_voices,
                "cpuUsage": cpu_usage,
                "sourceCpuEvidence": source_cpu,
                "performanceMonitor": counters,
                "voicesPhysical": voices_physical,
                "voicesTotal": voices_total,
                "outputPeakDb": output_peak,
                "silenceFloorDb": args.silence_floor_db,
                "isolatedTransportEvidence": isolated_transport_evidence,
            }
        )
        report["assertions"] = {
            "transportPlaying": transport_state == "playing",
            "smokeVoiceOrSourceCpuEvidence": voice_or_cpu_evidence,
            "outputPeakFinite": output_peak_finite,
            "outputPeakAboveSilenceFloor": output_peak_audible,
        }
        core_assertions_passed = all(report["assertions"].values())
        if not core_assertions_passed:
            failed = [name for name, passed in report["assertions"].items() if not passed]
            raise SmokeAssertionError("Smoke assertions failed: {0}".format(", ".join(failed)))
    finally:
        if client is not None and transport_id is not None:
            try:
                call(
                    client,
                    report,
                    "ak.wwise.core.transport.executeAction",
                    {"transport": transport_id, "action": "stop"},
                )
            except Exception as error:  # cleanup must continue through every operation
                mandatory_cleanup_errors.append("transport.stop: {0}".format(error))
            try:
                call(
                    client,
                    report,
                    "ak.wwise.core.transport.destroy",
                    {"transport": transport_id},
                )
            except Exception as error:
                mandatory_cleanup_errors.append("transport.destroy: {0}".format(error))

        if client is not None and capture_started:
            try:
                stop_result = call(client, report, "ak.wwise.core.profiler.stopCapture")
                report.setdefault("profiler", {})["captureStopTimeMs"] = stop_result.get("return")
            except Exception as error:
                mandatory_cleanup_errors.append("profiler.stopCapture: {0}".format(error))
            try:
                capture_path = Path(args.capture_file).resolve()
                call(
                    client,
                    report,
                    "ak.wwise.core.profiler.saveCapture",
                    {"file": str(capture_path)},
                )
                for _ in range(20):
                    if capture_path.is_file() and capture_path.stat().st_size > 0:
                        capture_artifact_valid = True
                        break
                    time.sleep(0.05)
                if not capture_artifact_valid:
                    raise SmokeAssertionError(
                        "Profiler capture was not written as a non-empty file: '{0}'.".format(
                            capture_path
                        )
                    )
                report.setdefault("profiler", {}).update(
                    {
                        "captureFile": str(capture_path),
                        "captureFileBytes": capture_path.stat().st_size,
                    }
                )
            except Exception as error:
                mandatory_cleanup_errors.append("profiler.saveCapture: {0}".format(error))

        if client is not None:
            try:
                client.disconnect()
            except Exception as error:
                mandatory_cleanup_errors.append("waapi.disconnect: {0}".format(error))

        report["cleanup"] = {
            "mandatoryErrors": mandatory_cleanup_errors,
            "warnings": cleanup_warnings,
        }
        report.setdefault("assertions", {})["profilerCaptureSaved"] = capture_artifact_valid
        report["success"] = (
            core_assertions_passed
            and capture_artifact_valid
            and not mandatory_cleanup_errors
        )


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", required=True, help="Absolute path to the isolated smoke .wproj")
    parser.add_argument("--report", required=True, help="JSON report path")
    parser.add_argument("--capture-file", required=True, help="Required non-empty .prof output path")
    parser.add_argument("--waapi-url", default="ws://127.0.0.1:8080/waapi")
    parser.add_argument("--connect-timeout", type=float, default=30.0)
    parser.add_argument("--playback-seconds", type=float, default=1.5)
    parser.add_argument("--silence-floor-db", type=float, default=-90.0)
    parser.add_argument("--source-class-id", type=int, default=DEFAULT_SOURCE_CLASS_ID)
    args = parser.parse_args(argv)
    if args.connect_timeout <= 0:
        parser.error("--connect-timeout must be positive")
    if args.playback_seconds <= 0:
        parser.error("--playback-seconds must be positive")
    if not args.capture_file.lower().endswith(".prof"):
        parser.error("--capture-file must end in .prof")
    return args


def main(argv=None):
    args = parse_args(argv or sys.argv[1:])
    report = {
        "schemaVersion": 1,
        "tool": "RealWorldWeatherAcoustics.WwiseAuthoringSmokeClient",
        "startedAtUtc": utc_now(),
        "finishedAtUtc": None,
        "success": False,
        "inputs": {
            "projectPath": str(Path(args.project).resolve()),
            "waapiUrl": args.waapi_url,
            "sourceClassId": args.source_class_id,
            "playbackSeconds": args.playback_seconds,
            "silenceFloorDb": args.silence_floor_db,
        },
        "waapiCompatibility": {
            "wwiseVersion": "2023.1.19.8928",
            "projectInfoUri": PROJECT_INFO_URI,
            "note": (
                "Wwise 2023.1 exposes ak.wwise.core.getProjectInfo; "
                "ak.wwise.core.project.getInfo is not present in its installed schema."
            ),
        },
        "steps": [],
        "assertions": {},
        "cleanup": {},
        "error": None,
    }
    exit_code = 1
    try:
        run_smoke(args, report)
        exit_code = 0 if report.get("success") else 1
    except Exception as error:
        report["success"] = False
        report["error"] = {
            "type": type(error).__name__,
            "message": str(error),
            "traceback": traceback.format_exc(),
        }
        record_step(report, "smoke", "failed", str(error))
    finally:
        report["finishedAtUtc"] = utc_now()
        write_report(args.report, report)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
