"""Package a deployed Windows folder as a per-user .NET Framework setup EXE.

No downloads, administrator access, or third-party installer compiler required.
Usage: python installer/build-glass-fix.py --payload dist/Tablet-Pro-Radial-VERSION
"""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import zipfile

project = pathlib.Path(__file__).resolve().parent.parent
version = re.search(r'project\(TabletProRadial VERSION ([0-9]+\.[0-9]+\.[0-9]+)', (project / 'CMakeLists.txt').read_text()).group(1)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--payload', type=pathlib.Path, required=True)
parser.add_argument('--csc', default=r'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe')
parser.add_argument('--repository', default='justicefrangipane/Tablet-Pro-Radial', help='Public GitHub owner/repository for future releases.')
args = parser.parse_args()
if args.repository and not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9-]{0,38}/[A-Za-z0-9][A-Za-z0-9_.-]{0,99}', args.repository):
    raise SystemExit('Use owner/repository, without a URL or credentials.')
payload = args.payload.resolve()
if not (payload / 'tabletpro_radial.exe').is_file():
    raise SystemExit('Deploy the complete Windows app before packaging.')
if not (payload / 'lib/TabletProGlass.dll').is_file():
    raise SystemExit('The Windows composition DLL is required for this Glass build.')
for required in ['Qt6Widgets.dll', 'Qt6Network.dll', 'tls/qschannelbackend.dll']:
    if not (payload / required).is_file():
        raise SystemExit(f'Missing tray/update runtime dependency: {required}')
work = project / 'build' / 'glass-installer'
work.mkdir(parents=True, exist_ok=True)
(payload / 'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=.\nQmlImports=qml\n', encoding='utf-8')
def template(source, destination):
    destination.write_text(source.read_text(encoding='utf-8').replace('@APP_VERSION@', version), encoding='utf-8')
template(project / 'installer/uninstall-glass-fix.ps1', payload / 'Uninstall.ps1')
template(project / 'installer/install-glass-fix.ps1', work / 'install.ps1')
template(project / 'installer/GlassFixSetup.cs', work / 'Setup.cs')
shutil.copy2(project / 'docs/TRAY-AND-UPDATES.md', payload / 'READ-ME-FIRST.txt')
(payload / 'update-source.json').write_text(json.dumps({'repository': args.repository}) + '\n', encoding='utf-8')
shutil.copytree(project / 'examples', payload / 'examples', dirs_exist_ok=True)
source_zip = payload / f'Tablet-Pro-Radial-{version}-Source.zip'
with zipfile.ZipFile(source_zip, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for name in ['.github','app','assets','core','platform','qml','shaders','ui','tests','scripts','installer','vendor','docs','examples']:
        for path in sorted((project / name).rglob('*')):
            if path.is_file() and '__pycache__' not in path.parts:
                archive.write(path, 'Tablet-Pro-Radial/' + path.relative_to(project).as_posix())
    for name in ['CMakeLists.txt','README.md','.gitignore']:
        archive.write(project / name, 'Tablet-Pro-Radial/' + name)
checksums = []
for path in sorted(payload.rglob('*')):
    if path.is_file() and path.name != 'SHA256SUMS.txt':
        checksums.append(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.relative_to(payload).as_posix())
(payload / 'SHA256SUMS.txt').write_text('\n'.join(checksums) + '\n', encoding='utf-8')
bundle = work / 'payload.zip'
with zipfile.ZipFile(bundle, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for path in sorted(payload.rglob('*')):
        if path.is_file():
            archive.write(path, path.relative_to(payload).as_posix())
digest = hashlib.sha256(bundle.read_bytes()).hexdigest()
hash_source = work / 'PayloadHash.cs'
hash_source.write_text('internal static class PayloadHash { internal const string Value = "' + digest + '"; }\n')
output = project / 'dist' / f'Tablet-Pro-Radial-{version}-Windows-x64-Setup.exe'
subprocess.run([args.csc, '/nologo', '/target:winexe', '/platform:x64', '/optimize+',
    '/win32icon:' + str(project / 'assets/radial.ico'),
    '/reference:System.Windows.Forms.dll', '/reference:System.Drawing.dll',
    '/out:' + str(output), '/resource:' + str(bundle) + ',payload.zip',
    '/resource:' + str(work / 'install.ps1') + ',install.ps1',
    str(work / 'Setup.cs'), str(hash_source)], check=True)
result = {'installer': str(output), 'bytes': output.stat().st_size,
          'sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
          'payloadFiles': len(checksums), 'sourceArchive': str(source_zip)}
(work / 'build-result.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
