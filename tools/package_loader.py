from pathlib import Path
from zipfile import ZipFile,ZIP_DEFLATED
import json
root=Path(__file__).resolve().parents[1]
version=json.loads((root/'Info.json').read_text())['version']
with ZipFile(root/'dist'/f'KernelLoader-{version}.zip','w',ZIP_DEFLATED) as z:
    for p in [root/'Info.json',root/'Manifest.json',root/'dist/kernelloader.tefpkg']:z.write(p,p.name)
