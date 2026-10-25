"""Validate real C++ exports independently against the published JSON Schemas.

Run after a build that wrote ``artifacts/native-ui`` (both build scripts point
``H2D_TEST_ARTIFACTS`` there), with ``jsonschema`` installed.

Artifact names follow the *data format*, not the application version. The compact
feedback moved to the object structure in 0.9.0, so ``feedback.json`` and
``feedback-no-image.json`` carry that structure, and ``feedback-notag.json`` is the same
file with the optional ``annotationSpace`` tag dropped. No export produces the parallel
annotations/changes shape any more, so ``feedback-v0.7.json`` is gone; reading such a
file back is covered by ``tests/core_test.cpp::legacyFeedbackStillImports``.
"""
import base64
import copy
from datetime import datetime
import hashlib
import json
import math
import os
import re
import struct
from pathlib import Path

from jsonschema import Draft202012Validator, FormatChecker
from referencing import Registry, Resource
from referencing.exceptions import NoSuchResource

root = Path(__file__).resolve().parents[1]
artifacts = Path(os.environ.get("H2D_TEST_ARTIFACTS", root / "artifacts" / "native-ui"))
FORMAT_CHECKER = FormatChecker()


@FORMAT_CHECKER.checks("date-time")
def valid_date_time(value):
    # jsonschema's RFC3339 checker is an optional dependency. Keep these
    # offline checks effective even on a minimal Python installation.
    if not isinstance(value, str):
        return True  # The schema's type assertion handles non-strings.
    if not re.fullmatch(r"\d{4}-(0[1-9]|1[0-2])-(0[1-9]|[12]\d|3[01])T(?:[01]\d|2[0-3]):[0-5]\d:[0-5]\d(?:\.\d{1,9})?(?:Z|[+-](?:[01]\d|2[0-3]):[0-5]\d)", value):
        return False
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00")).utcoffset() is not None
    except ValueError:
        return False

SCHEMAS = (
    "feedback-minimal.schema.json",
    "feedback-minimal-v2.schema.json",
    "feedback-minimal-v3.schema.json",
    "feedback-minimal-v4.schema.json",
    "feedback-v0.7.schema.json",
    "feedback-v1.schema.json",
    "feedback-v1.1.schema.json",
    "feedback-v2.schema.json",
    "project-v3.schema.json",
    "video-feedback-v1.schema.json",
    "video-project-v1.schema.json",
)


def schema(name):
    return json.loads((root / "schema" / name).read_text(encoding="utf-8"))


def export(name):
    return json.loads((artifacts / name).read_text(encoding="utf-8"))


def retrieve_schema(uri):
    # Resolve only bundled schemas; validation must never fetch a network URL.
    if uri == "urn:edithere:feedback-minimal:4":
        return Resource.from_contents(schema("feedback-minimal-v4.schema.json"))
    if uri == "urn:edithere:feedback-minimal:3":
        return Resource.from_contents(schema("feedback-minimal-v3.schema.json"))
    name = uri.rsplit("/", 1)[-1]
    if name not in SCHEMAS:
        raise NoSuchResource(ref=uri)
    return Resource.from_contents(schema(name))


def validate(name, document):
    Draft202012Validator(schema(name), format_checker=FORMAT_CHECKER,
                         registry=Registry(retrieve=retrieve_schema)).validate(document)


def check_objects(document):
    for item in document["objects"]:
        if "kind" in item:
            assert item["kind"] in ("region", "point", "global"), item
            assert {"kind", "annotations"} <= set(item)
            assert "recognizedText" not in item
            if "ocrtext" in item:
                assert isinstance(item["ocrtext"], str) and item["ocrtext"].strip() and len(item["ocrtext"]) <= 500
            assert "movements" not in item and "result" not in item
            movement = item.get("movement")
            if item["kind"] == "region":
                assert {"source"} <= set(item) and "point" not in item
                source = item["source"]
                assert all(isinstance(value, (int, float)) and value == int(value) for value in source.values())
                assert source["x2"] > source["x1"] and source["y2"] > source["y1"]
                if movement:
                    assert {"rect", "dx", "dy"} <= set(movement) <= {"rect", "dx", "dy", "scaleX", "scaleY"}
                    assert all(value == int(value) for value in movement["rect"].values())
                    final = movement["rect"]
                    expected = {"dx": final["x1"] - source["x1"], "dy": final["y1"] - source["y1"],
                                "scaleX": (final["x2"] - final["x1"]) / (source["x2"] - source["x1"]),
                                "scaleY": (final["y2"] - final["y1"]) / (source["y2"] - source["y1"])}
                    for key, number in expected.items():
                        if key.startswith("scale"):
                            number = max(.01, math.floor(number * 100 + .5 + 1e-9) / 100)
                            if number == 1:
                                assert key not in movement
                                continue
                        assert math.isclose(movement[key], number, rel_tol=1e-9, abs_tol=1e-7)
            elif item["kind"] == "point":
                assert {"point"} <= set(item) and "source" not in item and not movement
            else:
                assert not ({"source", "point", "movement", "recognizedText", "ocrtext"} & set(item))
            for annotation in item["annotations"]:
                assert set(annotation) == {"number", "text"} and 1 <= annotation["number"] <= 1000, annotation
            continue
        assert {"source", "movements", "annotations"} <= set(item) <= {
            "id", "source", "movements", "annotations", "recognizedText"}, item
        for annotation in item["annotations"]:
            assert isinstance(annotation, str) or (set(annotation) == {"number", "text"} and 1 <= annotation["number"] <= 1000), annotation


def image_info(data):
    if data.startswith(b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR") and len(data) >= 33:
        return "image/png", *struct.unpack(">II", data[16:24])
    if data[:6] in (b"GIF87a", b"GIF89a") and len(data) >= 10:
        return "image/gif", *struct.unpack("<HH", data[6:10])
    if data[:2] == b"BM" and len(data) >= 26:
        dib = struct.unpack("<I", data[14:18])[0]
        if dib == 12:
            return "image/bmp", *struct.unpack("<HH", data[18:22])
        if dib >= 40 and len(data) >= 54:
            width, height = struct.unpack("<ii", data[18:26])
            return "image/bmp", width, abs(height)
    if data[:4] == b"RIFF" and data[8:12] == b"WEBP" and len(data) >= 30:
        chunk = data[12:16]
        if chunk == b"VP8X":
            return "image/webp", 1 + int.from_bytes(data[24:27], "little"), 1 + int.from_bytes(data[27:30], "little")
        if chunk == b"VP8 " and data[23:26] == b"\x9d\x01\x2a":
            width, height = struct.unpack("<HH", data[26:30])
            return "image/webp", width & 0x3fff, height & 0x3fff
        if chunk == b"VP8L" and data[20] == 0x2f:
            bits = int.from_bytes(data[21:25], "little")
            return "image/webp", 1 + (bits & 0x3fff), 1 + ((bits >> 14) & 0x3fff)
    if data.startswith(b"\xff\xd8") and data.endswith(b"\xff\xd9"):
        offset = 2
        while offset + 4 <= len(data) and data[offset] == 0xff:
            offset += 1
            while offset < len(data) and data[offset] == 0xff:
                offset += 1
            marker = data[offset]
            offset += 1
            if marker in (0xda, 0xd9):
                break
            if marker == 1 or 0xd0 <= marker <= 0xd7:
                continue
            length = int.from_bytes(data[offset:offset + 2], "big")
            if length < 2 or offset + length > len(data):
                break
            if marker in (0xc0, 0xc1, 0xc2, 0xc3, 0xc5, 0xc6, 0xc7, 0xc9, 0xca, 0xcb, 0xcd, 0xce, 0xcf) and length >= 8:
                height, width = struct.unpack(">HH", data[offset + 3:offset + 7])
                return "image/jpeg", width, height
            offset += length
    raise AssertionError("not a supported PNG/JPEG/WebP/GIF/BMP image")


def attachment(filename):
    assert isinstance(filename, str) and Path(filename).name == filename and "\\" not in filename
    candidate = artifacts / filename
    assert not candidate.is_symlink() and candidate.is_file(), candidate
    assert candidate.resolve().parent == artifacts.resolve(), candidate
    return candidate.read_bytes()


def check_image(document):
    image = document.get("image")
    if image is None:
        return None
    if isinstance(image, str):
        prefix, encoded = image.split(",", 1)
        data = base64.b64decode(encoded, validate=True)
        mime, width, height = image_info(data)
        assert prefix == f"data:{mime};base64"
    else:
        if image["mode"] == "file":
            data = attachment(image["file"])
        elif image["mode"] == "embedded":
            prefix, encoded = image["dataUrl"].split(",", 1)
            assert prefix == f"data:{image['mimeType']};base64"
            data = base64.b64decode(encoded, validate=True)
        else:
            assert image["mode"] == "none" and "file" not in image and "dataUrl" not in image
            data = None
        if data is not None:
            assert hashlib.sha256(data).hexdigest() == image["sha256"].lower(), "image SHA-256 mismatch"
            mime, width, height = image_info(data)
            assert (mime, width, height) == (image["mimeType"], image["width"], image["height"])
        else:
            width, height = image["width"], image["height"]
        if "preview" in image:
            preview_mime, preview_width, preview_height = image_info(attachment(image["preview"]))
            assert preview_mime == "image/jpeg" and 0 < preview_width <= 32767 and 0 < preview_height <= 32767
            assert preview_width * preview_height <= 32_000_000
    assert 0 < width <= 32767 and 0 < height <= 32767 and width * height <= 32_000_000
    return width, height


for name in SCHEMAS:
    Draft202012Validator.check_schema(schema(name))
print(f"PASS: {len(SCHEMAS)} published schemas load")

# Contract probes do not depend on C++ implementation helpers. Both schema
# entry points must accept compatibility files and reject invalid new metadata.
image_probe = {
    "$schema": "urn:edithere:feedback-minimal:2", "schemaVersion": "feedback-minimal-2",
    "tool": {"name": "EditHere", "version": "schema-contract"}, "exportedAt": "2026-10-17T00:00:00Z",
    "annotationSpace": "result", "objects": [],
}
v3_image_probe = {
    "schemaVersion": "feedback-minimal-3",
    "tool": {"name": "EditHere", "version": "schema-contract"}, "exportedAt": "2026-10-17T00:00:00Z",
    "annotationSpace": "result", "objects": [
        {"kind": "region", "source": {"x1": 1, "y1": 2, "x2": 5, "y2": 6},
         "movement": {"rect": {"x1": 2, "y1": 3, "x2": 10, "y2": 9}, "dx": 1, "dy": 1, "scaleX": 2, "scaleY": 1.5},
         "annotations": [{"number": 1, "text": "region"}]},
        {"kind": "point", "point": {"x": 12, "y": 8}, "ocrtext": "保存", "annotations": [{"number": 2, "text": "point"}]},
        {"kind": "global", "annotations": [{"number": 3, "text": "global"}]},
    ],
}
descriptor_probe = {"mode": "none", "mimeType": "image/png", "width": 10, "height": 20,
                    "devicePixelRatio": None, "sha256": "0" * 64}
for schema_name in ("feedback-minimal-v2.schema.json", "feedback-minimal.schema.json"):
    for image in (None, "data:image/png;base64,YQ==", descriptor_probe,
                  dict(descriptor_probe, mode="file", file="feedback.png"),
                  dict(descriptor_probe, mode="embedded", dataUrl="data:image/png;base64,YQ=="),
                  dict(descriptor_probe, preview="feedback.annotated.jpg")):
        validate(schema_name, dict(image_probe, **({"image": image} if image is not None else {})))
    invalid_images = [
        dict(descriptor_probe, mode="file"),
        dict(descriptor_probe, mode="embedded"),
        dict(descriptor_probe, file="feedback.png"),
        dict(descriptor_probe, mode="file", file="../private.png"),
        dict(descriptor_probe, mode="file", file="nul.png"),
        dict(descriptor_probe, mode="file", file="feedback.jpg"),
        dict(descriptor_probe, mode="embedded", dataUrl="data:image/jpeg;base64,YQ=="),
        dict(descriptor_probe, preview="../feedback.annotated.jpg"),
        dict(descriptor_probe, preview="feedback.png"),
        dict(descriptor_probe, sha256="incorrect"),
        dict(descriptor_probe, devicePixelRatio=0),
        dict(descriptor_probe, width=0),
        dict(descriptor_probe, height=32768),
        dict(descriptor_probe, unsupported=True),
    ]
    missing = copy.deepcopy(descriptor_probe)
    del missing["devicePixelRatio"]
    invalid_images.append(missing)
    validator = Draft202012Validator(schema(schema_name), format_checker=FORMAT_CHECKER,
                                     registry=Registry(retrieve=retrieve_schema))
    for image in invalid_images:
        assert not validator.is_valid(dict(image_probe, image=image)), image
for schema_name in ("feedback-minimal-v3.schema.json", "feedback-minimal.schema.json"):
    validate(schema_name, v3_image_probe)
    older_v3 = copy.deepcopy(v3_image_probe)
    older_v3["$schema"] = "urn:edithere:feedback-minimal:3"
    for item in older_v3["objects"]:
        if "movement" in item:
            item["result"] = item.pop("movement")
            item["movements"] = [{"to": item["result"]["rect"]}]
        item.setdefault("movements", [])
        if "ocrtext" in item:
            item["recognizedText"] = {"text": item.pop("ocrtext"), "engine": "legacy-probe", "language": "zh-Hans"}
    validate(schema_name, older_v3)
    validator = Draft202012Validator(schema(schema_name), format_checker=FORMAT_CHECKER,
                                     registry=Registry(retrieve=retrieve_schema))
    invalid_objects = [
        dict(v3_image_probe["objects"][0], point={"x": 1, "y": 2}),
        dict(v3_image_probe["objects"][0], movement=None),
        dict(v3_image_probe["objects"][0], movements=[]),
        dict(v3_image_probe["objects"][0], result=v3_image_probe["objects"][0]["movement"]),
        dict(v3_image_probe["objects"][1], source={"x1": 1, "y1": 2, "x2": 1, "y2": 2}),
        dict(v3_image_probe["objects"][1], point={"x": 1}),
        dict(v3_image_probe["objects"][1], movements=None),
        *[dict(v3_image_probe["objects"][1], ocrtext=text) for text in (None, 7, {}, [], "", " \n\t", "x" * 501)],
        dict(v3_image_probe["objects"][1], recognizedText={"text": "ambiguous", "engine": "legacy"}),
        dict(v3_image_probe["objects"][2], source=None),
        dict(v3_image_probe["objects"][2], recognizedText={"text": "global OCR", "engine": "probe"}),
        dict(v3_image_probe["objects"][2], ocrtext="global OCR"),
        dict(v3_image_probe["objects"][2], movement=v3_image_probe["objects"][0]["movement"]),
    ]
    for object_probe in invalid_objects:
        assert not validator.is_valid(dict(v3_image_probe, objects=[object_probe])), object_probe
print("PASS: feedback-minimal-3 single movement, legacy compatibility, discriminated targets and ambiguous-field rejection")

# Published v3 and video v1 keep their original timestamp requirement. Only
# the new v4 image format omits it; compatibility readers may be more lenient.
assert "exportedAt" in schema("feedback-minimal-v3.schema.json")["required"]
assert "exportedAt" in schema("video-feedback-v1.schema.json")["required"]
assert "children" not in schema("feedback-minimal-v3.schema.json")["$defs"]["feedbackObject"]["properties"]
assert all("children" not in branch.get("properties", {})
           for branch in schema("video-feedback-v1.schema.json")["properties"]["objects"]["items"]["oneOf"])

# Both writers run these same cases. Independently validate their expected
# contracts here, rather than trusting either writer to define valid output.
child_contract = json.loads((root / "tests" / "fixtures" / "child-feedback-contract.json").read_text(encoding="utf-8"))
for case in child_contract["cases"]:
    validate("feedback-minimal-v4.schema.json", {
        "schemaVersion": child_contract["schemaVersion"],
        "tool": {"name": "EditHere", "version": "shared-contract"},
        "annotationSpace": "result", "objects": case["expected"]["objects"],
    })
print(f"PASS: {len(child_contract['cases'])} shared desktop/Web child-feedback contracts")

# v4 keeps the v3 object shape and adds children: annotations that belong to a
# moved component stay nested under it instead of becoming a second object.
child = {"kind": "region", "source": {"x1": 3, "y1": 4, "x2": 8, "y2": 8},
         "annotations": [{"number": 4, "text": "the moved button"}]}
v4_probe = copy.deepcopy(v3_image_probe)
v4_probe["schemaVersion"] = "feedback-minimal-4"
v4_probe["objects"][0] = dict(v4_probe["objects"][0], children=[child])
v4_validator = Draft202012Validator(schema("feedback-minimal-v4.schema.json"), format_checker=FORMAT_CHECKER,
                                   registry=Registry(retrieve=retrieve_schema))
validate("feedback-minimal-v4.schema.json", v4_probe)
# Nesting is optional: an export without child annotations stays valid.
validate("feedback-minimal-v4.schema.json", dict(v4_probe, objects=v3_image_probe["objects"]))
invalid_children = [
    dict(v4_probe["objects"][0], children=[dict(child, kind="global")]),
    dict(v4_probe["objects"][0], children=[dict(child, kind="point", point={"x": 1, "y": 2})]),
    dict(v4_probe["objects"][0], children=[dict(child, source=None)]),
    dict(v4_probe["objects"][0], children=[dict(child, parent={"source": {"x1": 1, "y1": 2, "x2": 5, "y2": 6}})]),
    dict(v4_probe["objects"][0], children=[]),
    dict(v4_probe["objects"][0], children=child),
    dict(v4_probe["objects"][2], children=[child]),
    dict(v4_probe["objects"][1], children=[child]),
]
for object_probe in invalid_children:
    assert not v4_validator.is_valid(dict(v4_probe, objects=[object_probe])), object_probe
# A v3 file must not claim v4: the version tag names the exact format.
assert not v4_validator.is_valid(v3_image_probe), "v3 feedback validated as v4"
print("PASS: feedback-minimal-4 child annotations, non-region children and v3 rejection")

print("PASS: image mode/metadata/legacy compatibility and malformed contract probes for both schemas")

capture_probe = {"source": "file", "capturedAt": "2026-10-17T00:00:00Z", "fileName": "game.png",
                 "page": "Game home", "pageSource": "agent"}
invalid_capture_probes = [None, {}, dict(capture_probe, source="unknown"),
                          dict(capture_probe, capturedAt="2026-10-17T00:00:00"),
                          dict(capture_probe, capturedAt="2026-02-30T00:00:00Z"),
                          dict(capture_probe, fileName="../private.png"),
                          dict(capture_probe, fileName="C:\\private.png"),
                          dict(capture_probe, fileName="\0private.png"),
                          dict(capture_probe, fileName=".."),
                          dict(capture_probe, page=" \n\t"), dict(capture_probe, page="p" * 501),
                          dict(capture_probe, page="page\0value"), dict(capture_probe, windowTitle="title\0value"),
                          dict(capture_probe, pageSource="guessed"), dict(capture_probe, windowTitle=3),
                          dict(capture_probe, appName="a" * 1001), dict(capture_probe, instructions="extra")]
missing_page = dict(capture_probe)
del missing_page["page"]
invalid_capture_probes.append(missing_page)
missing_page_source = dict(capture_probe)
del missing_page_source["pageSource"]
invalid_capture_probes.append(missing_page_source)
for schema_name in ("feedback-minimal-v2.schema.json", "feedback-minimal.schema.json"):
    validator = Draft202012Validator(schema(schema_name), format_checker=FORMAT_CHECKER,
                                     registry=Registry(retrieve=retrieve_schema))
    validate(schema_name, dict(image_probe, capture=capture_probe))
    for source in ("screen", "file", "clipboard", "demo", "scroll", "history", "pin", "drop", "video"):
        validate(schema_name, dict(image_probe, capture=dict(capture_probe, source=source)))
    validate(schema_name, dict(image_probe, capture={"source": "screen", "capturedAt": "2026-10-17T08:00:00+08:00"}))
    for capture in invalid_capture_probes:
        assert not validator.is_valid(dict(image_probe, capture=capture)), capture
print("PASS: optional capture compatibility, acquisition sources, paired agent page fields and invalid context probes")

for version, artifact in (("v1", "feedback-v1.json"), ("v1.1", "feedback-v1.1.json"),
                          ("v2", "feedback-v2.json")):
    validate(f"feedback-{version}.schema.json", export(artifact))
    print(f"PASS: project export {artifact}")

# The current project format, including a region made by hand.
validate("project-v3.schema.json", export("project-v3.json"))
print("PASS: project export project-v3.json")
parent_project = copy.deepcopy(export("project-v3.json"))
parent_note = next(note for note in parent_project["annotations"]
                   if note["kind"] == "rectangle" and "movementSource" not in note)
parent_note["parent"] = {"source": {"x1": 1, "y1": 1, "x2": 10, "y2": 10}, "state": "child"}
project_validator = Draft202012Validator(schema("project-v3.schema.json"), format_checker=FORMAT_CHECKER,
                                       registry=Registry(retrieve=retrieve_schema))
validate("project-v3.schema.json", parent_project)
for parent_value in ({"source": parent_note["parent"]["source"]},
                     {"state": "child"},
                     dict(parent_note["parent"], state="none")):
    invalid_project = copy.deepcopy(parent_project)
    invalid_note = next(note for note in invalid_project["annotations"] if "parent" in note)
    invalid_note["parent"] = parent_value
    assert not project_validator.is_valid(invalid_project), parent_value
print("PASS: project parent-link shape and invalid partial states")

# A project document written by the 0.2 desktop reader, kept for compatibility.
validate("feedback-v1.schema.json",
         json.loads((root / "tests" / "fixtures" / "desktop-review.json").read_text(encoding="utf-8")))
print("PASS: desktop 0.2 compatibility fixture")

# Current compact feedback. New exports have a file/embedded/none descriptor;
# legacy embedded strings remain covered by the same compatibility entry point.
embedded = export("feedback.json")
plain = export("feedback-no-image.json")
validate("feedback-minimal.schema.json", embedded)
validate("feedback-minimal.schema.json", plain)
assert {key: value for key, value in embedded.items() if key not in ("image", "exportedAt")} == \
       {key: value for key, value in plain.items() if key not in ("image", "exportedAt")}
assert embedded["schemaVersion"] == "feedback-minimal-4"
assert len({item["id"] for item in embedded["objects"]}) == len(embedded["objects"])
assert all(isinstance(annotation, dict) for item in embedded["objects"] for annotation in item["annotations"])
assert embedded["tool"]["name"] == "EditHere"
validate("feedback-minimal-v4.schema.json", embedded)
check_objects(embedded)
print(f"PASS: current feedback, {len(embedded['objects'])} object(s), with and without the image")

# A full annotation list can coexist with an unannotated moved object. The
# published schema must accept the same export the application can reopen.
limit_feedback = export("feedback-limit.json")
validate("feedback-minimal.schema.json", limit_feedback)
check_objects(limit_feedback)
assert len(limit_feedback["objects"]) == 1001
assert sum(len(item["annotations"]) for item in limit_feedback["objects"]) == 1000
print("PASS: feedback with 1000 annotations and an additional moved object")

# The tag-less sample is what the importer accepts without annotationSpace, so it cannot be
# validated against the schema that requires the tag; its structure is asserted instead.
tagless = export("feedback-notag.json")
assert "annotationSpace" not in tagless and tagless["objects"], tagless
check_objects(tagless)
print(f"PASS: current feedback without the annotationSpace tag, {len(tagless['objects'])} object(s)")

width, height = check_image(embedded)
check_image(plain)
assert embedded["annotationSpace"] == "result"
print(f"PASS: original screenshot {width}x{height}, checked bytes/hash and result-coordinate notes")

for artifact in ("feedback-file.json", "feedback-embedded.json", "feedback-none.json", "feedback-preview.json"):
    document = export(artifact)
    validate("feedback-minimal-v4.schema.json", document)
    validate("feedback-minimal.schema.json", document)
    check_objects(document)
    check_image(document)
    assert isinstance(document["image"], dict)
    expected_mode = {"feedback-file.json": "file", "feedback-embedded.json": "embedded", "feedback-none.json": "none"}.get(artifact)
    if expected_mode:
        assert document["image"]["mode"] == expected_mode
    else:
        assert "preview" in document["image"]
print("PASS: real file/embedded/none/annotated-preview exports and all referenced attachments")

# Real OCR-bearing exports from the model tests, including the project-only
# deletion marker and the setting that removes evidence from feedback.
ocr = export("feedback-ocr.json")
validate("feedback-minimal-v4.schema.json", ocr)
validate("feedback-minimal.schema.json", ocr)
check_objects(ocr)
assert ocr["objects"][0]["ocrtext"] == "Export JSON"
assert "recognizedText" not in ocr["objects"][0]
disabled = export("feedback-ocr-disabled.json")
validate("feedback-minimal-v4.schema.json", disabled)
assert all("recognizedText" not in item and "ocrtext" not in item for item in disabled["objects"])
for artifact in ("project-ocr.json", "project-ocr-deleted.json"):
    validate("project-v3.schema.json", export(artifact))
assert export("project-ocr-deleted.json")["annotations"][0]["recognizedText"] == {
    "text": "", "edited": True}
video = export("video-feedback-ocr.json")
validate("video-feedback-v1.schema.json", video)
for frame in video["frames"]:
    validate("feedback-minimal-v4.schema.json", frame["feedback"])
    check_image(frame["feedback"])
    nested = frame["feedback"]["objects"]
    indexed = [{key: value for key, value in item.items()
                if key not in ("frameId", "timestampMs")}
               for item in video["objects"] if item["frameId"] == frame["id"]]
    assert nested == indexed
print("PASS: OCR feedback, omitted export, editable/deleted project and video index")

video_bundle = export("video-feedback-file.json")
validate("video-feedback-v1.schema.json", video_bundle)
for frame in video_bundle["frames"]:
    check_image(frame["feedback"])
    if "imageFile" in frame:
        assert frame["imageFile"] == frame["feedback"]["image"]["file"]
print("PASS: video frame file attachments with hash/dimensions and compatible root index")

# Actual acquisition context and the user's title-privacy setting, rather than
# a synthesized file that merely repeats the schema's allowed keys.
capture_feedback = export("feedback-capture.json")
private_feedback = export("feedback-capture-private.json")
for document in (capture_feedback, private_feedback):
    validate("feedback-minimal-v4.schema.json", document)
    validate("feedback-minimal.schema.json", document)
    check_image(document)
    capture = document["capture"]
    assert capture["page"].strip() and capture["pageSource"] == "agent"
assert capture_feedback["capture"]["windowTitle"]
assert "windowTitle" not in private_feedback["capture"]
assert {key: value for key, value in capture_feedback["capture"].items() if key != "windowTitle"} == private_feedback["capture"]
validate("project-v3.schema.json", export("project-capture.json"))
capture_video = export("video-feedback-capture.json")
validate("video-feedback-v1.schema.json", capture_video)
assert capture_video["capture"]["pageSource"] == "agent"
for frame in capture_video["frames"]:
    validate("feedback-minimal-v4.schema.json", frame["feedback"])
    check_image(frame["feedback"])
capture_video_project = export("video-project-capture.json")
validate("video-project-v1.schema.json", capture_video_project)
assert capture_video_project["capture"] == capture_video["capture"]
print("PASS: real acquisition/agent page context, privacy-filtered title, editable project and per-frame video context")
