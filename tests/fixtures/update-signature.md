# Update signature fixtures

`update-signature.txt` signs ASCII `payload` as the installer for version
99.0.0, using Node's native crypto module independently of the release signer.
The public P-256 coordinates are in `update-signature-public.h`.
`update-signature-wrong-key.txt` has the same payload and identity, signed by
another independently generated P-256 key. Both private keys existed only in
generator memory and were discarded. Neither public test key is included in
the desktop library or distributables; only isolated test executables compile
`EDITHERE_UPDATE_TEST_TRUST`.

The text fixtures are byte-sensitive ASCII/LF. Node tests verify the fixtures
independently; Windows tests verify them using native CNG and exercise the real
install entry, including rejection before confirmation/launch. The separate
certificate-policy executable compiles a certificate pin and proves that an
otherwise valid project signature cannot bypass failed Authenticode.
