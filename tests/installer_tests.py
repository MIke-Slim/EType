"""Exercise the same installer policy without registering a global TSF service.

The separately compiled validation EXE has a private AppId and no regserver
entries. Production has no runtime switch that can bypass registration.
"""
import hashlib
import ctypes
import json
import os
from pathlib import Path
import subprocess
import time
import uuid
import winreg

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'build/standalone/EType'
EXE = ROOT / 'build/installer-tests/EType-Setup-Validation.exe'
KEY = r'Software\Microsoft\Windows\CurrentVersion\Uninstall\EType.InstallerValidation_is1'
CLSID = r'Software\Classes\CLSID\{B61C1452-3E9A-4616-9EA3-18B4E5862CA4}'
TIP = r'Software\Microsoft\CTF\TIP\{B61C1452-3E9A-4616-9EA3-18B4E5862CA4}'
LEGACY_CLSID = r'Software\Classes\CLSID\{DC168F35-18EA-4EC5-B391-C4430C3F3ED9}'
LEGACY_TIP = r'Software\Microsoft\CTF\TIP\{DC168F35-18EA-4EC5-B391-C4430C3F3ED9}'


def registry_tree(hive, path, view):
    try:
        with winreg.OpenKey(hive, path, 0, winreg.KEY_READ | view) as key:
            values, children = {}, {}
            count_children, count_values, _ = winreg.QueryInfoKey(key)
            for index in range(count_values):
                name, value, kind = winreg.EnumValue(key, index)
                values[name] = [repr(value), kind]
            for index in range(count_children):
                name = winreg.EnumKey(key, index)
                children[name] = registry_tree(hive, path + '\\' + name, view)
            return {'values': values, 'children': children}
    except FileNotFoundError:
        return None


def system_snapshot():
    return [registry_tree(hive, key, view)
            for hive in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER)
            for key in (CLSID, TIP, LEGACY_CLSID, LEGACY_TIP)
            for view in (winreg.KEY_WOW64_64KEY, winreg.KEY_WOW64_32KEY)]


def installed_path():
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, KEY, 0,
                            winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
            return winreg.QueryValueEx(key, 'InstallLocation')[0]
    except FileNotFoundError:
        return None


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def conflicts_with_registered_service(directory):
    for view, architecture in ((winreg.KEY_WOW64_64KEY, 'x64'),
                               (winreg.KEY_WOW64_32KEY, 'x86')):
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, CLSID + r'\InprocServer32',
                                0, winreg.KEY_READ | view) as key:
                registered = winreg.QueryValueEx(key, '')[0]
                if registered and os.path.normcase(os.path.abspath(registered)) != os.path.normcase(
                        str((directory / architecture / 'EType.dll').resolve())):
                    return True
        except FileNotFoundError:
            pass
    return False


def main():
    if not EXE.is_file():
        raise RuntimeError('Compile scripts/build-installer.ps1 -Validation first.')
    if installed_path():
        raise RuntimeError('A prior validation installation exists; remove that test installation first.')
    run_root = (ROOT / 'build/installer-tests' / ('run-' + uuid.uuid4().hex[:12])).resolve()
    if not run_root.is_relative_to((ROOT / 'build/installer-tests').resolve()):
        raise RuntimeError('Invalid test workspace.')
    run_root.mkdir()
    selected = run_root / '用户 选择 路径' / 'EType (& Test)'
    foreign = run_root / '已有 用户文件'
    foreign.mkdir()
    sentinel = foreign / 'keep.txt'
    sentinel.write_text('Keep this user file.', encoding='utf-8')
    sentinel_hash = sha(sentinel)
    initial_registry = system_snapshot()
    checks = []
    counter = 0
    completed = False

    def check(name, passed):
        checks.append({'name': name, 'passed': bool(passed)})
        if not passed:
            raise AssertionError(name)

    def run(executable, *arguments):
        nonlocal counter
        counter += 1
        log = run_root / f'operation-{counter}.log'
        result = subprocess.run([str(executable), '/VERYSILENT', '/SUPPRESSMSGBOXES',
                                 '/NORESTART', '/SP-', '/LANG=chinesesimp', f'/LOG={log}',
                                 *arguments], timeout=300, capture_output=True)
        return result.returncode

    def install(path):
        return run(EXE, f'/DIR={path}')

    try:
        check('reject_nonempty_foreign_directory', install(foreign) != 0)
        check('foreign_user_file_unchanged', sha(sentinel) == sentinel_hash and
              not (foreign / 'EType.exe').exists())
        nocancel_target = run_root / '禁止取消参数'
        check('reject_nocancel_that_disables_failure_rollback', run(EXE, f'/DIR={nocancel_target}', '/NOCANCEL') != 0)
        check('nocancel_rejection_copies_no_payload', not (nocancel_target / 'EType.exe').exists())
        failed_target = run_root / '注册失败回滚'
        check('injected_failure_aborts_installation', run(EXE, f'/DIR={failed_target}', '/FailAfterCopy=1') != 0)
        failure_log = (run_root / f'operation-{counter}.log').read_text(encoding='utf-8-sig', errors='replace')
        check('failure_was_injected_after_actual_file_copy', 'injected failure after all payload files' in failure_log)
        check('failure_enters_installer_rollback', 'Rolling back changes' in failure_log)
        check('rollback_removes_copied_payload_and_marker', not (failed_target / 'EType.exe').exists()
              and not (failed_target / 'x64/EType.dll').exists()
              and not (failed_target / 'x86/EType.dll').exists()
              and not (failed_target / 'etype-installation.id').exists())
        check('rollback_leaves_no_application_record', installed_path() is None)
        check('install_custom_chinese_space_metacharacter_path', install(selected) == 0)
        check('registry_records_selected_path', os.path.normcase(os.path.normpath(installed_path() or '')) ==
              os.path.normcase(os.path.normpath(str(selected))))
        manifest = json.loads((PACKAGE / 'package-manifest.json').read_text(encoding='utf-8'))
        check('installed_payload_matches_every_manifest_hash', all(
            (selected / name).is_file() and sha(selected / name) == evidence['sha256']
            for name, evidence in manifest['files'].items()))
        check('uninstaller_placed_in_selected_directory', (selected / 'unins000.exe').is_file())
        for script in ('install.ps1', 'uninstall.ps1'):
            result = subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                     '-File', str(selected / script), '-Quiet', '-ValidateOnly',
                                     '-InstallDir', str(selected)], capture_output=True, timeout=30)
            expected_conflict = script == 'install.ps1' and conflicts_with_registered_service(selected)
            check('legacy_path_validation_' + script, (result.returncode != 0 and
                  b'registered at another path' in result.stderr) if expected_conflict else result.returncode == 0)
        env = dict(os.environ, ETYPE_HEADLESS_TEST='1',
                   ETYPE_TEST_SETTINGS=str(run_root / 'isolated-test-settings.ini'))
        report = run_root / 'installed-ui-selftest.json'
        preview = subprocess.run([str(selected / 'EType.exe'), '--ui-selftest', str(report)],
                                 env=env, timeout=45, capture_output=True)
        check('actual_preview_runs_from_custom_path', preview.returncode == 0 and
              json.loads(report.read_text(encoding='utf-8')).get('passed'))
        check('reject_relocation_of_existing_installation', install(run_root / 'other') != 0)
        check('relocation_rejection_preserves_original_installation', (selected / 'EType.exe').is_file()
              and not (run_root / 'other/EType.exe').exists())
        original_dll_hash = sha(selected / 'x64/EType.dll')
        user_file = selected / '我的笔记.txt'
        user_file.write_text('用户后来保存的文件必须保留。', encoding='utf-8')
        user_hash = sha(user_file)
        check('reject_unsupported_overwrite_upgrade', install(selected) != 0)
        check('upgrade_rejection_preserves_dll_bytes', sha(selected / 'x64/EType.dll') == original_dll_hash)
        check('upgrade_rejection_preserves_user_added_file', sha(user_file) == user_hash)
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                     ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        kernel.CreateFileW.restype = ctypes.c_void_p
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        kernel.CloseHandle.restype = ctypes.c_int
        handle = kernel.CreateFileW(str(selected / 'x64/EType.dll'), 0x80000000, 1, None, 3, 0x80, None)
        if handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            check('reject_uninstall_while_component_is_locked', run(selected / 'unins000.exe') != 0)
            check('locked_uninstall_preserves_payload_and_installation_record',
                  sha(selected / 'x64/EType.dll') == original_dll_hash and
                  (selected / 'EType.exe').is_file() and installed_path() is not None)
        finally:
            kernel.CloseHandle(handle)
        service = selected / 'runtime/ETypeService.exe'
        handle = kernel.CreateFileW(str(service), 0x80000000, 1, None, 3, 0x80, None)
        if handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            check('reject_uninstall_while_local_service_is_locked', run(selected / 'unins000.exe') != 0)
            check('service_lock_rejection_preserves_models_and_registration_record',
                  (selected / 'models/Qwen3-4B-Q4_K_M.gguf').is_file() and installed_path() is not None)
        finally:
            kernel.CloseHandle(handle)
        marker = selected / 'etype-installation.id'
        original_marker = marker.read_bytes()
        marker.write_text('wrong-app', encoding='ascii')
        check('reject_uninstall_without_correct_marker', run(selected / 'unins000.exe') != 0)
        check('failed_uninstall_preserves_executable', (selected / 'EType.exe').is_file())
        marker.write_bytes(original_marker)
        check('uninstall_from_chosen_directory', run(selected / 'unins000.exe') == 0)
        for _ in range(20):
            if not (selected / 'unins000.exe').exists():
                break
            time.sleep(0.1)
        check('uninstall_removes_all_owned_payload', all(not (selected / name).exists()
              for name in manifest['files']))
        check('uninstall_preserves_user_added_file', sha(user_file) == user_hash)
        check('uninstall_removes_private_application_record', installed_path() is None)
        second_path = run_root / '重新 选择安装位置'
        check('reinstall_at_new_path_after_uninstall', install(second_path) == 0)
        check('second_uninstall_completes', run(second_path / 'unins000.exe') == 0)
        check('global_etype_registration_unchanged', system_snapshot() == initial_registry)
        completed = True
    finally:
        evidence = {'passed': completed and all(item['passed'] for item in checks),
                    'checks': checks, 'test_workspace': str(run_root),
                    'mode': 'same-source compile-time validation; no global TSF registration',
                    'installer_sha256': sha(EXE),
                    'installer_source_sha256': sha(ROOT / 'installer/EType.iss'),
                    'package_manifest_sha256': sha(PACKAGE / 'package-manifest.json')}
        (ROOT / 'build/installer-test-results.json').write_text(
            json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'checks': len(checks), 'failures': sum(not c['passed'] for c in checks),
                      'test_workspace': str(run_root)}, ensure_ascii=False))


if __name__ == '__main__':
    main()
