import ctypes
import os
import shutil
import uuid
import unittest
from pathlib import Path

from portable_install import DATA_MARKER, latest_legacy_data, publish


class PortableInstallTests(unittest.TestCase):
    def setUp(self):
        self.test_area = Path(__file__).resolve().parents[1] / "build" / "portable-install-tests"
        self.test_area.mkdir(parents=True, exist_ok=True)
        self.root = (self.test_area / ("auralis-update-test-" + uuid.uuid4().hex)).resolve()
        self.root.mkdir()
        self.dist = self.root / "dist"
        self.install = self.dist / "Auralis"
        self.staging = self.root / "staging"
        self.backup = self.root / "backup"
        self.dist.mkdir()
        self.staging.mkdir()
        self.write(self.staging / "Auralis.exe", b"new tested executable")
        self.write(self.staging / "README.md", b"new documentation")

    def tearDown(self):
        assert self.root.parent == self.test_area.resolve()
        assert self.root.name.startswith("auralis-update-test-")
        shutil.rmtree(self.root)

    @staticmethod
    def write(path, content, timestamp=None):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        if timestamp is not None:
            os.utime(path, (timestamp, timestamp))

    def legacy(self, version, timestamp):
        data = self.dist / f"Auralis-{version}" / "Data"
        self.write(data / "audio-device.xml", version.encode(), timestamp)
        return data

    def test_initial_migration_preserves_recordings_and_uses_recent_preferences(self):
        old = self.legacy("0.4.30", 200)
        self.legacy("0.4.31", 100)
        self.write(old / "Pitch" / "keep.wav", b"conflicting legacy audio")
        self.write(old / "Pitch" / "new.wav", b"recent pitch audio")
        self.write(old / "plugins.xml", b"latest registry", 200)
        self.write(self.install / "Auralis.exe", b"old executable")
        self.write(self.install / "Data" / "audio-device.xml", b"old device", 50)
        self.write(self.install / "Data" / "plugins.xml", b"old registry", 50)
        self.write(self.install / "Data" / "Pitch" / "keep.wav", b"original recording")
        self.write(self.install / "my-project.auralis", b"private project")
        source = publish(self.staging, self.install, self.backup)
        self.assertEqual(source, old)
        self.assertEqual((self.install / "Auralis.exe").read_bytes(), b"new tested executable")
        self.assertEqual((self.install / "Data" / "audio-device.xml").read_bytes(), b"0.4.30")
        self.assertEqual((self.install / "Data" / "plugins.xml").read_bytes(), b"latest registry")
        self.assertEqual((self.install / "Data" / "Pitch" / "keep.wav").read_bytes(), b"original recording")
        self.assertEqual((self.install / "Data" / "Pitch" / "new.wav").read_bytes(), b"recent pitch audio")
        self.assertEqual((self.install / "my-project.auralis").read_bytes(), b"private project")
        self.assertEqual((self.backup / "audio-device.xml").read_bytes(), b"old device")

    def test_future_updates_keep_data_and_the_same_installation_path(self):
        self.legacy("0.4.31", 100)
        publish(self.staging, self.install, self.backup)
        self.write(self.install / "Data" / "audio-device.xml", b"my current settings", 300)
        self.write(self.install / "Data" / "Take.wav", b"my new recording")
        before = {p.relative_to(self.install / "Data"): p.read_bytes() for p in (self.install / "Data").rglob("*") if p.is_file()}
        self.legacy("0.4.32", 400)
        self.write(self.staging / "Auralis.exe", b"next tested executable")
        self.assertIsNone(publish(self.staging, self.install, self.backup))
        after = {p.relative_to(self.install / "Data"): p.read_bytes() for p in (self.install / "Data").rglob("*") if p.is_file()}
        self.assertEqual(before, after)
        self.assertEqual((self.install / "Auralis.exe").read_bytes(), b"next tested executable")
        self.assertFalse((self.dist / "Auralis-0.4.32" / "Auralis.exe").exists())

    def test_newer_fixed_folder_settings_are_not_replaced_on_first_migration(self):
        self.legacy("0.4.31", 100)
        self.write(self.install / "Data" / "audio-device.xml", b"newer fixed settings", 200)
        publish(self.staging, self.install, self.backup)
        self.assertEqual((self.install / "Data" / "audio-device.xml").read_bytes(), b"newer fixed settings")
        self.assertTrue((self.install / "Data" / DATA_MARKER).is_file())

    def test_legacy_version_breaks_equal_timestamp_ties(self):
        self.legacy("0.4.9", 100)
        expected = self.legacy("0.4.31", 100)
        self.assertEqual(latest_legacy_data(self.dist), expected)

    @unittest.skipUnless(os.name == "nt", "Windows executable locking")
    def test_running_executable_lock_leaves_the_installation_and_data_untouched(self):
        self.write(self.install / "Auralis.exe", b"running executable")
        self.write(self.install / "README.md", b"old documentation")
        self.write(self.install / "Data" / "audio-device.xml", b"live settings", 50)
        self.legacy("0.4.31", 200)
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        kernel.CreateFileW.restype = ctypes.c_void_p
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.CreateFileW(str(self.install / "Auralis.exe"), 0x80000000, 1, None, 3, 0, None)
        self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
        try:
            with self.assertRaisesRegex(RuntimeError, "Close Auralis"):
                publish(self.staging, self.install, self.backup)
        finally:
            kernel.CloseHandle(handle)
        self.assertEqual((self.install / "Auralis.exe").read_bytes(), b"running executable")
        self.assertEqual((self.install / "README.md").read_bytes(), b"old documentation")
        self.assertEqual((self.install / "Data" / "audio-device.xml").read_bytes(), b"live settings")
        self.assertFalse((self.install / "Data" / DATA_MARKER).exists())
        self.assertFalse((self.install / "Auralis.exe.update").exists())


if __name__ == "__main__":
    unittest.main()
