#pragma once

namespace h2d::updateauth {
// Public P-256 X || Y coordinates, 32 bytes each, big endian, lowercase hex.
// Provision only after the separately recorded production-credential approval.
// Empty means no project trust root. Never put a private key in this file.
inline constexpr char ProjectPublicKeyHex[] = "8f6ecc50494b489189cec4597c83b2c09bb389cc9e0a42384cbcb221f6f1490147ffe9dc3632e511429eeb73d2e65af05f8317ae06f7bb217a657b266acfbe6c";
}
