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
import json
import struct
from pathlib import Path

from jsonschema import Draft202012Validator, FormatChecker

root = Path(__file__).resolve().parents[1]
artifacts = root / "artifacts" / "native-ui"

SCHEMAS = (
    "feedback-minimal.schema.json",
    "feedback-v0.7.schema.json",
    "feedback-v1.schema.json",
    "feedback-v1.1.schema.json",
    "feedback-v2.schema.json",
    "project-v3.schema.json",
)


def schema(name):
    return json.loads((root / "schema" / name).read_text(encoding="utf-8"))


def export(name):
    return json.loads((artifacts / name).read_text(encoding="utf-8"))


def validate(name, document):
    Draft202012Validator(schema(name), format_checker=FormatChecker()).validate(document)


def check_objects(document):
    for item in document["objects"]:
        assert set(item) == {"source", "movements", "annotations"}, item


for name in SCHEMAS:
    Draft202012Validator.check_schema(schema(name))
print(f"PASS: {len(SCHEMAS)} published schemas load")

for version, artifact in (("v1", "feedback-v1.json"), ("v1.1", "feedback-v1.1.json"),
                          ("v2", "feedback-v2.json")):
    validate(f"feedback-{version}.schema.json", export(artifact))
    print(f"PASS: project export {artifact}")

# The current project format, including a region made by hand.
validate("project-v3.schema.json", export("project-v3.json"))
print("PASS: project export project-v3.json")

# A project document written by the 0.2 desktop reader, kept for compatibility.
validate("feedback-v1.schema.json",
         json.loads((root / "tests" / "fixtures" / "desktop-review.json").read_text(encoding="utf-8")))
print("PASS: desktop 0.2 compatibility fixture")

# Current compact feedback, with and without the embedded original.
embedded = export("feedback.json")
plain = export("feedback-no-image.json")
validate("feedback-minimal.schema.json", embedded)
validate("feedback-minimal.schema.json", plain)
assert {key: value for key, value in embedded.items() if key != "image"} == \
       {key: value for key, value in plain.items() if key != "image"}
check_objects(embedded)
print(f"PASS: current feedback, {len(embedded['objects'])} object(s), with and without the image")

# The tag-less sample is what the importer accepts without annotationSpace, so it cannot be
# validated against the schema that requires the tag; its structure is asserted instead.
tagless = export("feedback-notag.json")
assert "annotationSpace" not in tagless and tagless["objects"], tagless
check_objects(tagless)
print(f"PASS: current feedback without the annotationSpace tag, {len(tagless['objects'])} object(s)")

png = base64.b64decode(embedded["image"].removeprefix("data:image/png;base64,"), validate=True)
assert png.startswith(b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR")
width, height = struct.unpack(">II", png[16:24])
assert 0 < width <= 32767 and 0 < height <= 32767 and width * height <= 32_000_000
assert embedded["annotationSpace"] == "result"
print(f"PASS: standalone original PNG {width}x{height}, result-coordinate notes")
