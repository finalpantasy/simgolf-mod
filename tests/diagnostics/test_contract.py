"""Portable checks; does not claim Windows or PowerShell runtime coverage."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class DiagnosticsTests(unittest.TestCase):
    def test_actual_rotation_helper(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / 'test_rotation'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(ROOT / 'tests/diagnostics'),
                            '-I' + str(ROOT / 'port/src'),
                            str(ROOT / 'tests/diagnostics/test_rotation.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], cwd=directory, check=True)

    def test_actual_crash_writer(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / 'test_crash'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-D_M_IX86', '-I' + str(ROOT / 'tests/diagnostics'),
                            '-I' + str(ROOT / 'port/src'),
                            str(ROOT / 'tests/diagnostics/test_crash.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], cwd=directory, check=True)

    def test_packaging_and_non_destructive_host(self):
        package = (ROOT / 'tools/release/package.sh').read_text()
        self.assertIn('cp tools/diagnostics/Collect-Diagnostics.ps1 "$A/"', package)
        self.assertIn('cp docs/DIAGNOSTICS.md "$A/DIAGNOSTICS.txt"', package)
        host = (ROOT / 'port/src/port/host_win32.c').read_text()
        self.assertIn('host_rotate_log(g_log_path)', host)
        self.assertNotIn('fopen(g_log_path, "w")', host)
        crash = (ROOT / 'port/src/port/crash_record.h').read_text()
        self.assertIn('FILE_APPEND_DATA', crash)
        self.assertIn('OPEN_ALWAYS', crash)
        self.assertNotIn('CREATE_ALWAYS', crash)

    def test_collector_safety_contract(self):
        script = (ROOT / 'tools/diagnostics/Collect-Diagnostics.ps1').read_text()
        self.assertLess(script.index('if ($PreviewOnly)'), script.index('New-Item -ItemType Directory'))
        self.assertLess(script.index("-cne 'EXPORT'"), script.index('New-Item -ItemType Directory'))
        self.assertIn('[string[]]$SavePath = @()', script)
        self.assertIn('raw_stack_included = $false', script)
        self.assertIn('[switch]$IncludeSaveCompanions', script)
        self.assertIn("@('.camera-tiles', '.course-lights', '.path-styles', '.living-club.json')", script)
        self.assertIn("$_.Binary -and $_.Name.EndsWith('.sve')", script)
        self.assertIn('-IncludeSaveCompanions requires at least one explicit -SavePath.', script)
        for forbidden in ('Invoke-WebRequest', 'Invoke-RestMethod', 'Start-BitsTransfer', 'MiniDumpWriteDump'):
            self.assertNotIn(forbidden, script)

    def test_path_resolution_and_redaction_discovery_contract(self):
        script = (ROOT / 'tools/diagnostics/Collect-Diagnostics.ps1').read_text()
        self.assertIn('GetUnresolvedProviderPathFromPSPath', script)
        self.assertIn('$LogPath = Resolve-NativePath $LogPath $install', script)
        self.assertIn('[IO.Path]::GetPathRoot($Base)', script)
        self.assertIn('Cannot resolve drive-relative path', script)
        self.assertIn('$OutputDirectory = Resolve-FileSystemPath $OutputDirectory', script)
        self.assertIn('(Resolve-FileSystemPath $save)', script)
        self.assertIn('Resolve-NativePath $env:SIMGOLF_GAME $install', script)
        self.assertIn('Resolve-NativePath $env:SIMGOLF_USER_DIR $GameRoot', script)
        self.assertNotIn("Add-Root $env:SIMGOLF_GAME", script)
        self.assertNotIn("Add-Root $env:SIMGOLF_USER_DIR", script)
        self.assertLess(script.index('Find-TextRoots $textContents[$item.Name]'),
                        script.index('New-Item -ItemType Directory -Path $stage'))
        self.assertIn('(Redact $textContents[$item.Name])', script)
        self.assertIn('if ($trimmed)', script)

    def test_manual_issue_submission_contract(self):
        script = (ROOT / 'tools/diagnostics/Collect-Diagnostics.ps1').read_text()
        self.assertIn("$issuesUrl = 'https://github.com/finalpantasy/simgolf-mod/issues'", script)
        self.assertIn("-ceq 'OPEN'", script)
        self.assertEqual(script.count('Start-Process'), 1)
        self.assertIn('Start-Process -FilePath $issuesUrl -ErrorAction Stop', script)
        browser = script[script.index('$issuesUrl ='):]
        self.assertNotIn('Remove-Item', browser)
        self.assertLess(script.rindex('Remove-Item'), script.index('$issuesUrl ='))
        self.assertIn('Nothing is submitted by this script.', browser)
        self.assertIn('Wait until the attachment finishes uploading', browser)
        self.assertIn('attachments are public', browser)

if __name__ == '__main__':
    unittest.main()
