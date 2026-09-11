"""Validate real C++ exports independently against the published JSON Schemas."""
import json
from pathlib import Path
from jsonschema import Draft202012Validator, FormatChecker
root = Path(__file__).resolve().parents[1]
for version in ("v1", "v1.1", "v2"):
    schema = json.loads((root / "schema" / f"feedback-{version}.schema.json").read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    document = json.loads((root / "artifacts/native-ui" / f"feedback-{version}.json").read_text(encoding="utf-8"))
    Draft202012Validator(schema, format_checker=FormatChecker()).validate(document)
    print(f"PASS: native feedback-{version}.json")
schema = json.loads((root / "schema/feedback-v1.schema.json").read_text(encoding="utf-8"))
legacy = json.loads((root / "tests/fixtures/desktop-review.json").read_text(encoding="utf-8"))
Draft202012Validator(schema, format_checker=FormatChecker()).validate(legacy)
print("PASS: desktop 0.2 compatibility fixture")
