"""Validate real C++ exports independently against the published JSON Schemas."""
import json
from pathlib import Path
from jsonschema import Draft202012Validator, FormatChecker
root = Path(__file__).resolve().parents[1]
for version in ("v1", "v1.1", "v2", "minimal", "v0.7"):
    schema = json.loads((root / "schema" / f"feedback-{version}.schema.json").read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    document = json.loads((root / "artifacts/native-ui" / f"feedback-{version}.json").read_text(encoding="utf-8"))
    Draft202012Validator(schema, format_checker=FormatChecker()).validate(document)
    print(f"PASS: native feedback-{version}.json")
schema = json.loads((root / "schema/feedback-v1.schema.json").read_text(encoding="utf-8"))
legacy = json.loads((root / "tests/fixtures/desktop-review.json").read_text(encoding="utf-8"))
Draft202012Validator(schema, format_checker=FormatChecker()).validate(legacy)
print("PASS: desktop 0.2 compatibility fixture")

schema = json.loads((root / "schema/feedback-v0.7.schema.json").read_text(encoding="utf-8"))
embedded = json.loads((root / "artifacts/native-ui/feedback-v0.7.json").read_text(encoding="utf-8"))
plain = json.loads((root / "artifacts/native-ui/feedback-v0.7-no-image.json").read_text(encoding="utf-8"))
Draft202012Validator(schema).validate(plain)
assert {key: value for key, value in embedded.items() if key != "image"} == plain
import base64
import struct
png = base64.b64decode(embedded["image"].removeprefix("data:image/png;base64,"), validate=True)
assert png.startswith(b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR")
width, height = struct.unpack(">II", png[16:24])
assert 0 < width <= 32767 and 0 < height <= 32767 and width * height <= 32_000_000
assert embedded["annotationSpace"] == "result"
assert len(embedded["annotations"]) == 2 and len(embedded["changes"]) == 1
print(f"PASS: standalone original PNG {width}x{height}, result-coordinate notes, and no-image variant")
