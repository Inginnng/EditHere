"""Packaging regressions: stale builds, private session data and archive collisions."""
import importlib.util
import json
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

PROJECT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_distribution", PROJECT / "scripts/prepare-distribution.py")
distribution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(distribution)


class DistributionTest(unittest.TestCase):
    def setUp(self):
        parent = PROJECT / "web/tests/output/distribution-tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="fixture-", dir=parent)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "source"
        self.root.mkdir()
        self.write("CMakeLists.txt", "project(EditHere VERSION 0.10.5 LANGUAGES CXX)")
        for name in distribution.LEGAL:
            self.write(name, "license fixture")
        for name in distribution.WEB_RUNTIME:
            self.write("web/" + name, "public runtime fixture\n")
        for name in distribution.WEB_GUIDES:
            self.write("web/" + name, "first installation instructions")
        for name in distribution.MCP_REQUIRED_SOURCES:
            self.write(name, "MCP source fixture\n")
        self.write("web/mcp/build-info.json", json.dumps({
            "schemaVersion": 1,
            "hashEncoding": "utf8-lf",
            "sources": {name: distribution.mcp_hash(self.root / name) for name in distribution.MCP_REQUIRED_SOURCES},
            "artifacts": {name: distribution.mcp_hash(self.root / name) for name in distribution.MCP_REQUIRED_ARTIFACTS}
        }))
        for names in distribution.PACKAGE_FILES.values():
            for name in names:
                self.write(name, "published component fixture")
        self.write("web/skills/edithere-web/SKILL.md", "published web skill")
        self.write("web/skills/edithere-web/references/local-installation.md", "../../../agent.cjs")
        self.write("web/skills/edithere-web/references/capture.md", "generic capture instructions")
        self.write("web/tests/output/agent/server-key", "PRIVATE-SESSION-SECRET")
        self.write("web/tests/output/agent/session.json", "PRIVATE-IMAGE-DATA")
        self.write("web/mcp/node_modules/package/private.json", "PRIVATE-DEPENDENCY-FILE")
        self.write("web/mcp/.env", "PRIVATE-MCP-ENV")
        self.write("connector/package.json", '{"version":"0.2.0"}')
        self.write("connector/mcp-server.mjs", "public MCP fixture")
        self.write("connector/README.md", "MCP instructions")
        self.write("trae-plugin/package.json", '{"version":"0.8.22"}')
        self.write("trae-plugin/.trae-plugin/plugin.json", "hidden required metadata")
        self.write("skills/edithere/SKILL.md", "desktop fixture")
        self.write("docs/VIDEO-ANNOTATION.md", "video instructions")
        self.addCleanup(patch.stopall)
        patch.object(distribution, "revision", return_value=("abc12345" + "0" * 32, False)).start()
        patch.object(distribution.subprocess, "run").start()

    def write(self, relative, contents):
        file = self.root / relative
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(contents, encoding="utf-8")
        return file

    def native_zip(self, relative, built="0.10.5", payload="native fixture"):
        file = self.root / relative
        file.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(file, "w") as archive:
            archive.writestr("version.txt", built)
            archive.writestr("EditHere.exe", payload)
        return file

    def output(self, name="分发"):
        return self.root.parent / name

    def test_separates_platforms_keeps_duplicate_names_and_excludes_private_data(self):
        old = self.native_zip("dist/EditHere-0.10.3-win-x64.zip", "0.10.3", "first")
        different = self.native_zip("dist/archive/previous/EditHere-0.10.3-win-x64.zip", "0.10.3", "second")
        report = distribution.assemble(self.root, self.output(), include_history=True)
        self.assertFalse(report["completeNativeSet"])
        self.assertFalse(report["published"])
        self.assertTrue((self.output() / "windows/0.10.5/README.md").is_file())
        archived = list((self.output() / "archive/windows/0.10.3").rglob("*.zip"))
        self.assertEqual(len(archived), 2)
        self.assertEqual({distribution.sha256(file) for file in archived}, {distribution.sha256(old), distribution.sha256(different)})
        self.assertTrue(old.is_file() and different.is_file(), "source artifacts stay untouched")
        agent = next((self.output() / "web").rglob("*-web-agent-*.zip"))
        with zipfile.ZipFile(agent) as archive:
            names = archive.namelist()
            self.assertIn("web/src/feedback.js", names)
            self.assertIn("web/install-skill.cjs", names)
            self.assertIn("web/EditHere.mcp.html", names)
            self.assertIn("web/mcp/server.bundle.cjs", names)
            self.assertIn("web/mcp/mcp.json", names)
            self.assertIn("web/mcp/build-info.json", names)
            self.assertIn("web/MCP-THIRD-PARTY-NOTICES.md", names)
            self.assertIn("web/src/layout.js", names)
            self.assertIn("AI-SETUP.md", names)
            self.assertIn("AI-SETUP.en.md", names)
            self.assertIn("web/skills/edithere-web/references/capture.md", names)
            self.assertFalse(any("tests/output" in name or "server-key" in name for name in names))
            self.assertFalse(any("node_modules" in name or name.endswith(".env") for name in names))
            self.assertFalse(any(b"PRIVATE-" in archive.read(name) for name in names))
        trae = next((self.output() / "integrations/trae-plugin").rglob("*.zip"))
        with zipfile.ZipFile(trae) as archive:
            self.assertIn("trae-plugin/.trae-plugin/plugin.json", archive.namelist())
        for line in (self.output() / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines():
            expected, relative = line.split("  ", 1)
            self.assertEqual(distribution.sha256(self.output() / relative), expected)

    def test_incomplete_mcp_runtime_fails_before_creating_distribution(self):
        (self.root / "web/mcp/server.bundle.cjs").unlink()
        with self.assertRaisesRegex(ValueError, "Web runtime file missing: mcp/server.bundle.cjs"):
            distribution.assemble(self.root, self.output())
        self.assertFalse(self.output().exists())

    def test_stale_mcp_sources_or_artifacts_cannot_be_distributed(self):
        for relative in ("web/src/mcp-app.js", "web/mcp/server.bundle.cjs"):
            original = (self.root / relative).read_text(encoding="utf-8")
            self.write(relative, "updated but not built")
            with self.assertRaisesRegex(ValueError, "Stale MCP build file"):
                distribution.assemble(self.root, self.output())
            self.assertFalse(self.output().exists())
            self.write(relative, original)

    def test_mcp_build_manifest_cannot_reference_an_outside_file(self):
        manifest_file = self.root / "web/mcp/build-info.json"
        manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
        manifest["sources"]["../private.json"] = "0" * 64
        manifest_file.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Unsafe MCP build manifest path"):
            distribution.assemble(self.root, self.output())
        self.assertFalse(self.output().exists())

    def test_windows_git_line_endings_do_not_invalidate_mcp_build(self):
        for name in distribution.MCP_REQUIRED_SOURCES | distribution.MCP_REQUIRED_ARTIFACTS:
            file = self.root / name
            file.write_bytes(file.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
        report = distribution.assemble(self.root, self.output())
        agent = self.output() / next(item["path"] for item in report["packages"] if item["product"] == "web-agent")
        with zipfile.ZipFile(agent) as archive:
            self.assertEqual(archive.read("web/mcp/server.bundle.cjs"), (self.root / "web/mcp/server.bundle.cjs").read_bytes())
        self.assertEqual(distribution.sha256(agent), next(item["sha256"] for item in report["packages"] if item["product"] == "web-agent"))

    def test_strict_missing_platforms_fails_before_creating_catalog(self):
        with self.assertRaisesRegex(ValueError, "Current native packages missing"):
            distribution.assemble(self.root, self.output(), require_all=True)
        self.assertFalse(self.output().exists())

    def test_unlisted_component_files_and_personal_paths_never_ship(self):
        for name in ("connector/.env", "connector/keys/secret.pem", "trae-plugin/local.json",
                     "skills/edithere/references/local-installation.md",
                     "web/skills/edithere-web/references/local-installation.md",
                     "web/skills/edithere-web/references/personal-notes.md"):
            self.write(name, "PRIVATE-UNTRACKED-SECRET C:/Users/PersonalUser/private")
        distribution.assemble(self.root, self.output())
        for file in self.output().rglob("*.zip"):
            with zipfile.ZipFile(file) as archive:
                for name in archive.namelist():
                    self.assertNotIn(b"PRIVATE-", archive.read(name), f"Leaked private file: {name}")
                if "web-agent" in file.name:
                    self.assertEqual(archive.read("web/skills/edithere-web/references/local-installation.md").decode("utf-8"),
                                     distribution.GENERIC_WEB_INSTALLATION)

    def test_installer_test_directory_cannot_promote_itself_to_current(self):
        self.native_zip("dist/installer-user-flow/EditHere-0.10.5-win-x64.zip")
        self.write("dist/installer-user-flow/version.txt", "0.10.5")
        report = distribution.assemble(self.root, self.output(), include_history=True)
        self.assertIn("windows/portable", report["missingCurrentNative"])
        self.assertEqual(report["existingArtifacts"][0]["classification"], "historical-unverified")

    def test_filename_cannot_upgrade_an_old_zip(self):
        self.native_zip("dist/EditHere-0.10.5-win-x64.zip", "0.10.3")
        with self.assertRaisesRegex(ValueError, "ZIP build version differs"):
            distribution.assemble(self.root, self.output())
        self.assertFalse(self.output().exists())

    def test_current_files_are_collected_byte_exactly_and_integrity_mismatch_blocks(self):
        native = self.native_zip("dist/EditHere-0.10.5-win-x64.zip")
        self.write("dist/EditHere-0.10.5-win-x64-setup.exe", "installer fixture")
        self.write("dist/EditHere-0.10.5-macos-universal.dmg", "DMG fixture")
        appimage = self.write("dist/linux-0.10.5/EditHere-linux-x86_64.AppImage", "AppImage fixture")
        self.write("dist/linux-0.10.5/edithere-cli", "matching wrapper")
        self.write("dist/linux-0.10.5/version.txt", "0.10.5")
        self.write("dist/linux-0.10.5/EditHere-linux-x86_64.AppImage.sha256", distribution.sha256(appimage) + "  " + appimage.name)
        report = distribution.assemble(self.root, self.output(), require_all=True)
        self.assertTrue(report["completeNativeSet"])
        self.assertEqual(distribution.sha256(native), distribution.sha256(self.output() / "windows/0.10.5" / native.name))
        self.assertTrue((self.output() / "linux/0.10.5/edithere-cli").is_file())
        self.assertEqual([p["state"] for p in report["packages"] if p["product"] == "macos"], ["collected-unverified"])
        self.write("dist/linux-0.10.5/EditHere-linux-x86_64.AppImage.sha256", "0" * 64)
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            distribution.assemble(self.root, self.output("invalid"))
        self.assertFalse(self.output("invalid").exists())

    def test_output_never_overwrites_source_or_existing_distribution(self):
        self.output().mkdir()
        marker = self.output() / "unique.txt"
        marker.write_text("keep me", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "already exists"):
            distribution.assemble(self.root, self.output())
        self.assertEqual(marker.read_text(), "keep me")
        with self.assertRaisesRegex(ValueError, "under dist"):
            distribution.assemble(self.root, self.root / "app/new-output")

    def test_packaging_checks_skip_build_cache_without_hiding_product_scripts(self):
        check_spec = importlib.util.spec_from_file_location("check_packaging", PROJECT / "scripts/check-packaging.py")
        checks = importlib.util.module_from_spec(check_spec)
        check_spec.loader.exec_module(checks)
        self.write("build-agent-completion/build-task.ps1", "# 构建缓存，不是产品脚本")
        self.write("web/tests/output/capture/build-task.ps1", "# 运行时测试数据，不是产品脚本")
        self.write("scripts/build-product.ps1", "# 产品脚本仍需要编码检查")
        with patch.object(checks, "ROOT", self.root):
            problems = checks.check_ps1_utf8_bom()
        self.assertEqual(len(problems), 1)
        self.assertIn("scripts/build-product.ps1", problems[0])


if __name__ == "__main__":
    unittest.main()
