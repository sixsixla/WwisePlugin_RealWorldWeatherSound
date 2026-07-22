#!/usr/bin/env python3
"""Persist the retained heavy-rain Audio File Source + RWWA Effect demo."""

import argparse
import hashlib
import json
import sys
import traceback
import wave
from datetime import datetime, timezone
from pathlib import Path

from wwise_authoring_smoke import (
    PROJECT_INFO_URI,
    call,
    call_when_project_ready,
    canonical_path,
    connect_with_retry,
    effect_smoke_properties,
    read_plugin_properties,
    read_wwise_object,
)


DEMO_SOUND_NAME = "RWWA_Demo_Heavy_Rain_Puddles"
DEMO_AUDIO_SOURCE_NAME = "RWWA_Demo_Heavy_Rain_Puddles_Audio"
DEMO_EFFECT_NAME = "RWWA_Demo_Weather_Geometry_Effect"
DEMO_EVENT_NAME = "Play_RWWA_Demo_Heavy_Rain_Puddles"
DEFAULT_EFFECT_CLASS_ID = 2031748099


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def inspect_rain_wav(path):
    path = Path(path).resolve(strict=True)
    with wave.open(str(path), "rb") as reader:
        channels = reader.getnchannels()
        sample_width_bytes = reader.getsampwidth()
        sample_rate = reader.getframerate()
        frame_count = reader.getnframes()
        compression_type = reader.getcomptype()

    duration_seconds = frame_count / float(sample_rate)
    assertions = {
        "isPcm": compression_type == "NONE",
        "isStereo": channels == 2,
        "is48Khz": sample_rate == 48000,
        "is24Bit": sample_width_bytes == 3,
        "isExactlyThirtySeconds": abs(duration_seconds - 30.0) <= (1.0 / sample_rate),
        "isNonEmpty": path.stat().st_size > 44,
    }
    return {
        "path": str(path),
        "fileName": path.name,
        "bytes": path.stat().st_size,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "codec": "pcm_s24le" if sample_width_bytes == 3 else "pcm",
        "channels": channels,
        "sampleRate": sample_rate,
        "bitsPerSample": sample_width_bytes * 8,
        "frameCount": frame_count,
        "durationSeconds": duration_seconds,
        "assertions": assertions,
    }


def make_effect_definition(effect_class_id):
    definition = {
        "type": "Effect",
        "name": DEMO_EFFECT_NAME,
        "classId": effect_class_id,
    }
    definition.update(
        {"@" + name: value for name, value in effect_smoke_properties().items()}
    )
    return definition


def create_demo(client, report, effect_class_id, input_wav):
    sound_definition = {
        "type": "Sound",
        "name": DEMO_SOUND_NAME,
        "notes": (
            "Persistent human-listening demo: 30-second heavy-rain Audio File "
            "Source processed by RealWorld Weather Acoustics Effect."
        ),
        "@IsLoopingEnabled": True,
        "@IsLoopingInfinite": True,
        "@IsStreamingEnabled": True,
        "children": [
            {
                "type": "AudioFileSource",
                "name": DEMO_AUDIO_SOURCE_NAME,
                "import": {"files": [{"audioFile": str(input_wav)}]},
            }
        ],
        "@Effects": [
            {
                "type": "EffectSlot",
                "name": "",
                "@Effect": make_effect_definition(effect_class_id),
            }
        ],
    }
    result = call(
        client,
        report,
        "ak.wwise.core.object.set",
        {
            "objects": [
                {
                    "object": "\\Actor-Mixer Hierarchy\\Default Work Unit",
                    "children": [sound_definition],
                }
            ],
            "onNameConflict": "replace",
            "listMode": "replaceAll",
            "autoAddToSourceControl": False,
        },
        {"return": ["id", "name", "type", "classId"]},
    )
    try:
        sound = result["objects"][0]["children"][0]
        audio_source = sound["children"][0]
        effect_slot = sound["@Effects"][0]
        effect = effect_slot["@Effect"]
        sound_id = sound["id"]
        audio_source_id = audio_source["id"]
        effect_id = effect["id"]
    except (KeyError, IndexError, TypeError) as error:
        raise RuntimeError(
            "object.set did not return the persistent Sound, AudioFileSource, "
            "EffectSlot, and Effect IDs: {0}".format(error)
        )

    event_result = call(
        client,
        report,
        "ak.wwise.core.object.set",
        {
            "objects": [
                {
                    "object": "\\Events\\Default Work Unit",
                    "children": [
                        {
                            "type": "Event",
                            "name": DEMO_EVENT_NAME,
                            "children": [
                                {
                                    "name": "",
                                    "type": "Action",
                                    "@ActionType": 1,
                                    "@Target": sound_id,
                                }
                            ],
                        }
                    ],
                }
            ],
            "onNameConflict": "replace",
            "autoAddToSourceControl": False,
        },
        {"return": ["id", "name", "type"]},
    )
    try:
        event = event_result["objects"][0]["children"][0]
        action = event["children"][0]
        event_id = event["id"]
        action_id = action["id"]
    except (KeyError, IndexError, TypeError) as error:
        raise RuntimeError(
            "object.set did not return the persistent Event and Action IDs: {0}".format(
                error
            )
        )

    sound_readback = read_wwise_object(
        client,
        sound_id,
        ["id", "name", "type", "@IsLoopingEnabled", "@IsLoopingInfinite", "@IsStreamingEnabled"],
    )
    audio_source_readback = read_wwise_object(
        client, audio_source_id, ["id", "name", "type", "originalFilePath"]
    )
    effect_readback = read_wwise_object(
        client, effect_id, ["id", "name", "type", "classId"]
    )
    event_readback = read_wwise_object(client, event_id, ["id", "name", "type"])
    action_readback = read_wwise_object(
        client, action_id, ["id", "name", "type", "@ActionType", "@Target"]
    )
    expected_properties = effect_smoke_properties()
    actual_properties = read_plugin_properties(
        client, effect_id, list(expected_properties)
    )
    original_file_path = audio_source_readback.get("originalFilePath")
    action_target = action_readback.get("@Target") or {}
    assertions = {
        "soundCreated": sound_readback.get("name") == DEMO_SOUND_NAME,
        "audioFileSourceCreated": (
            audio_source_readback.get("name") == DEMO_AUDIO_SOURCE_NAME
            and audio_source_readback.get("type") == "AudioFileSource"
        ),
        "audioFileSourceReferencesRainWav": (
            bool(original_file_path)
            and Path(str(original_file_path)).name.casefold()
            == Path(input_wav).name.casefold()
        ),
        "loopingEnabled": sound_readback.get("@IsLoopingEnabled") is True,
        "loopingInfinite": sound_readback.get("@IsLoopingInfinite") is True,
        "streamingEnabled": sound_readback.get("@IsStreamingEnabled") is True,
        "effectCreated": (
            effect_readback.get("name") == DEMO_EFFECT_NAME
            and int(effect_readback.get("classId")) == effect_class_id
        ),
        "all71EffectPropertiesReadable": len(actual_properties) == 71,
        "effectIsConfiguredForRain": int(actual_properties.get("InputRole", -1)) == 0,
        "eventCreated": event_readback.get("name") == DEMO_EVENT_NAME,
        "playActionCreated": (
            action_readback.get("type") == "Action"
            and int(action_readback.get("@ActionType", -1)) == 1
        ),
        "playActionTargetsDemoSound": (
            isinstance(action_target, dict)
            and str(action_target.get("id", "")).casefold() == sound_id.casefold()
        ),
    }
    report["demo"] = {
        "sound": sound_readback,
        "audioFileSource": audio_source_readback,
        "effect": effect_readback,
        "event": event_readback,
        "action": action_readback,
        "effectPropertyCount": len(actual_properties),
        "effectProperties": actual_properties,
        "assertions": assertions,
    }
    if not all(assertions.values()):
        raise RuntimeError(
            "Persistent rain demo assertions failed: {0}".format(
                [name for name, passed in assertions.items() if not passed]
            )
        )


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", required=True)
    parser.add_argument("--wav", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--waapi-url", default="ws://127.0.0.1:8080/waapi")
    parser.add_argument("--connect-timeout", type=float, default=30.0)
    parser.add_argument("--effect-class-id", type=int, default=DEFAULT_EFFECT_CLASS_ID)
    args = parser.parse_args(argv)
    args.project = str(Path(args.project).resolve(strict=True))
    args.wav = str(Path(args.wav).resolve(strict=True))
    if Path(args.project).suffix.casefold() != ".wproj":
        parser.error("--project must identify a .wproj")
    if Path(args.wav).suffix.casefold() != ".wav":
        parser.error("--wav must identify a .wav")
    if args.connect_timeout <= 0:
        parser.error("--connect-timeout must be positive")
    if args.effect_class_id < 0 or args.effect_class_id > 0xFFFFFFFF:
        parser.error("--effect-class-id must be an unsigned 32-bit integer")
    return args


def main(argv=None):
    args = parse_args(argv or sys.argv[1:])
    project_path = Path(args.project)
    input_wav = Path(args.wav)
    originals_root = (project_path.parent / "Originals").resolve(strict=True)
    try:
        input_wav.relative_to(originals_root)
    except ValueError:
        raise RuntimeError(
            "The persistent demo WAV must live under the Wwise project's Originals "
            "directory: '{0}'".format(input_wav)
        )

    report = {
        "schemaVersion": 1,
        "tool": "RealWorldWeatherAcoustics.ImportRainDemo",
        "startedAtUtc": utc_now(),
        "finishedAtUtc": None,
        "success": False,
        "project": {"expectedPath": str(project_path)},
        "inputWav": inspect_rain_wav(input_wav),
        "steps": [],
        "demo": None,
        "error": None,
    }
    client = None
    exit_code = 1
    try:
        failed_wav_assertions = [
            name
            for name, passed in report["inputWav"]["assertions"].items()
            if not passed
        ]
        if failed_wav_assertions:
            raise RuntimeError(
                "Rain WAV format assertions failed: {0}".format(failed_wav_assertions)
            )
        client = connect_with_retry(args.waapi_url, args.connect_timeout, report)
        project_info = call_when_project_ready(
            client,
            report,
            PROJECT_INFO_URI,
            timeout_seconds=args.connect_timeout,
        )
        actual_project_path = project_info.get("path")
        report["project"].update(
            {
                "actualPath": actual_project_path,
                "name": project_info.get("name"),
                "id": project_info.get("id"),
            }
        )
        if not actual_project_path or canonical_path(actual_project_path) != canonical_path(
            str(project_path)
        ):
            raise RuntimeError(
                "Refusing to modify a different Wwise project. Expected '{0}', got '{1}'.".format(
                    project_path, actual_project_path
                )
            )
        create_demo(client, report, args.effect_class_id, input_wav)
        call(
            client,
            report,
            "ak.wwise.core.project.save",
            {"autoCheckOutToSourceControl": False},
        )
        report["success"] = True
        exit_code = 0
    except Exception as error:
        report["error"] = {
            "type": type(error).__name__,
            "message": str(error),
            "traceback": traceback.format_exc(),
        }
    finally:
        if client is not None:
            try:
                client.disconnect()
            except Exception as error:
                if report["error"] is None:
                    report["error"] = {
                        "type": type(error).__name__,
                        "message": "WAAPI disconnect failed: {0}".format(error),
                    }
                    report["success"] = False
                    exit_code = 1
        report["finishedAtUtc"] = utc_now()
        report_path = Path(args.report).resolve()
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(
            json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
