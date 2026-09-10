"""Independent validation of the actual C# serializer's exports, not a mirror serializer."""
import base64
import hashlib
import json
from pathlib import Path
from jsonschema import Draft202012Validator, FormatChecker

root = Path(__file__).resolve().parents[1]
schema = json.loads((root / 'schema/feedback-v1.schema.json').read_text(encoding='utf-8'))
Draft202012Validator.check_schema(schema)
validator = Draft202012Validator(schema, format_checker=FormatChecker())
for name in ('export.json', 'review.json'):
    data = json.loads((root / 'artifacts/self-test' / name).read_text(encoding='utf-8'))
    validator.validate(data)
    capture = data['capture']
    assert [n['number'] for n in data['annotations']] == list(range(1, len(data['annotations']) + 1))
    assert len({n['id'] for n in data['annotations']}) == len(data['annotations'])
    for note in data['annotations']:
        if note['kind'] == 'point':
            assert 0 <= note['point']['x'] < capture['width']
            assert 0 <= note['point']['y'] < capture['height']
        else:
            rect = note['rectangle']
            assert 0 <= rect['x1'] < rect['x2'] <= capture['width']
            assert 0 <= rect['y1'] < rect['y2'] <= capture['height']
    if capture['pngBase64']:
        image = base64.b64decode(capture['pngBase64'], validate=True)
        assert image.startswith(b'\x89PNG\r\n\x1a\n')
        assert hashlib.sha256(image).hexdigest() == capture['sha256']
    print(f'PASS {name}: JSON Schema, coordinates, IDs, image integrity')
