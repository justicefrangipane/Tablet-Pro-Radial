#!/usr/bin/env python3
"""Build an ordinary NSIS per-user installer from an audited portable directory.

Requires Python 3 and NSIS 3.09+ (makensis). Does not execute the installer.
Example:
  python installer/build-installer.py --payload /path/to/portable-folder \
      --output /path/to/Tablet-Pro-Radial-Setup.exe --makensis /path/to/makensis
Set NSISDIR if using an extracted rather than installed NSIS toolchain.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
PROJECT = HERE.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--payload', required=True, type=pathlib.Path)
parser.add_argument('--output', required=True, type=pathlib.Path)
parser.add_argument('--makensis', default='makensis')
parser.add_argument('--work', type=pathlib.Path, default=PROJECT / 'build-installer')
args = parser.parse_args()
source = args.payload.resolve()
work = args.work.resolve()
output = args.output.resolve()
if not (source / 'tabletpro_radial.exe').is_file():
    raise SystemExit('The payload must be the complete audited Windows portable directory.')
audit = json.loads((source / 'SYMBOL-AUDIT.json').read_text())
if audit.get('errors') or not audit.get('allX64'):
    raise SystemExit('The payload static dependency audit has not passed.')
if work == source or source.is_relative_to(work):
    raise SystemExit('The build working directory must not contain the source payload.')
payload = work / 'payload'
generated = work / 'generated'
if payload.exists():
    shutil.rmtree(payload)
payload.mkdir(parents=True)
generated.mkdir(parents=True, exist_ok=True)
shutil.copytree(source, payload, dirs_exist_ok=True)
shutil.copy2(HERE / 'READ-ME-FIRST.txt', payload / 'READ-ME-FIRST.txt')
shutil.copytree(PROJECT / 'docs', payload / 'docs', dirs_exist_ok=True)
compiler_version = subprocess.check_output([args.makensis, '-VERSION'], text=True).strip()
provenance = {
    'product': 'Tablet Pro Radial 0.3.0', 'installer': 'Standard NSIS Unicode LZMA stub',
    'compiler': compiler_version, 'requestedExecutionLevel': 'user',
    'installLocation': r'%LOCALAPPDATA%\Programs\TabletProRadial\0.3.0',
    'payloadValidation': 'Cross-compiled Windows x64; static dependencies and symbols audited.',
    'runtimeValidation': 'Installer and Windows application not executed in the Linux build environment.',
    'uninstall': 'Explicit packaged-file list; empty directories only; profiles and settings preserved.'
}
(payload / 'INSTALLER-PROVENANCE.json').write_text(json.dumps(provenance, indent=2) + '\n')
nsis_license = HERE / 'NSIS-LICENSE.txt'
if nsis_license.is_file():
    shutil.copy2(nsis_license, payload / 'licenses/NSIS-LICENSE.txt')
if (HERE / 'NSIS-TOOLCHAIN.json').is_file():
    shutil.copy2(HERE / 'NSIS-TOOLCHAIN.json', payload / 'licenses/NSIS-TOOLCHAIN.json')

def nsis(value):
    return str(value).replace('$', '$$').replace('"', '$\\"')

# Refresh the manifest after replacing the installer-specific instructions.
checksums = []
for path in sorted(payload.rglob('*')):
    if path.is_file() and path.name != 'SHA256SUMS.txt':
        checksums.append(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.relative_to(payload).as_posix())
(payload / 'SHA256SUMS.txt').write_text('\n'.join(checksums) + '\n')
files = sorted(p for p in payload.rglob('*') if p.is_file())
install = []
uninstall = []
directories = set()
manifest = []
for path in files:
    relative = path.relative_to(payload)
    if any('\n' in part or '\r' in part for part in relative.parts):
        raise SystemExit('Payload filenames must not contain line breaks.')
    output_dir = relative.parent.as_posix().replace('/', '\\')
    suffix = '' if output_dir == '.' else '\\' + nsis(output_dir)
    preserve_profile = path.name.lower().endswith('.radial.json')
    install.extend(['SetOverwrite off' if preserve_profile else 'SetOverwrite try',
                    'ClearErrors', f'SetOutPath "$INSTDIR{suffix}"',
                    f'File "${{PAYLOAD_DIR}}/{nsis(relative.as_posix())}"',
                    'IfErrors install_failed'])
    if not preserve_profile:
        destination = f'$INSTDIR\\{nsis(str(relative).replace(chr(47), chr(92)))}'
        uninstall.extend([f'IfFileExists "{destination}" 0 +4',
                          'ClearErrors', f'Delete "{destination}"',
                          'IfErrors uninstall_failed'])
    parent = relative.parent
    while str(parent) != '.':
        directories.add(parent)
        parent = parent.parent
    manifest.append({'path': relative.as_posix(), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                     'bytes': path.stat().st_size, 'preservedOnUninstall': path.name.lower().endswith('.radial.json')})
for directory in sorted(directories, key=lambda p: (-len(p.parts), str(p))):
    uninstall.append(f'RMDir "$INSTDIR\\{nsis(str(directory).replace(chr(47), chr(92)))}"')
uninstall.extend(['Goto uninstall_files_done', 'uninstall_failed:',
                  'MessageBox MB_OK|MB_ICONSTOP "Some application files could not be removed. Close Tablet Pro Radial and run the uninstaller again. Your settings and saved profiles have not been removed."',
                  'SetErrorLevel 2', 'Abort', 'uninstall_files_done:'])
(generated / 'install-files.nsh').write_text('\n'.join(install) + '\n')
(generated / 'uninstall-files.nsh').write_text('\n'.join(uninstall) + '\n')
(work / 'payload-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
output.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([args.makensis, '-V2', '-NOCD', f'-DPAYLOAD_DIR={payload.as_posix()}',
                f'-DGENERATED_DIR={generated.as_posix()}', f'-DOUTPUT_EXE={output.as_posix()}',
                f'-DPAYLOAD_KIB={(sum(p.stat().st_size for p in files) + 1023) // 1024}',
                str(HERE / 'TabletProRadial.nsi')], check=True)
print(json.dumps({'installer': str(output), 'bytes': output.stat().st_size,
                  'sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
                  'payloadFiles': len(files), 'manifest': str(work / 'payload-manifest.json')}, indent=2))
