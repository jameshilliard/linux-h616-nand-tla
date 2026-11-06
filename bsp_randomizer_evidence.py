# SPDX-License-Identifier: GPL-2.0-or-later
"""Check immutable public BSP evidence; never infer hardware from its callers."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import subprocess
import urllib.request

from run_core import extract

HERE = Path(__file__).resolve().parent
MANIFEST = HERE / 'reference/randomizer/source.json'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def audit(out, seeds, download=False):
    pin = json.loads(MANIFEST.read_text())
    bundle = (HERE / 'reference/randomizer/bsp-functions.c').read_text()
    for name, expected in pin['functions'].items():
        assert sha(extract(bundle, name).encode()) == expected, name
    unit = (HERE / 'tests/bsp_randomizer.c').read_text().replace('/* @functions@ */', bundle)
    (out / 'bsp-check.c').write_text(unit)
    build = subprocess.run(['gcc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-no-pie', 'bsp-check.c', '-o', 'bsp-check'], cwd=out,
                           text=True, capture_output=True, timeout=60)
    (out / 'bsp-build.log').write_text(build.stdout + build.stderr)
    assert build.returncode == 0, build.stderr
    run = subprocess.run(['./bsp-check'], cwd=out, text=True, capture_output=True, timeout=60)
    (out / 'bsp-check.log').write_text(run.stdout + run.stderr)
    assert run.returncode == 0 and run.stdout == '8388608 BSP register sequences passed\n' and not run.stderr
    report = dict(manifest_sha256=sha(MANIFEST.read_bytes()),
                  bundled_functions_checked=True, register_sequences=8388608,
                  public_files_checked={}, public_audit_requested=download)
    if not download:
        return report
    cache = HERE / '.tools/randomizer-bsp'
    cache.mkdir(parents=True, exist_ok=True)
    jobs = [(variant, path, checksum) for variant, files in pin['variants'].items()
            for path, checksum in files.items() if path != 'soc']

    def fetch(job):
        variant, path, expected = job
        file = cache / (variant + '-' + Path(path).name)
        if not file.exists():
            url = ('https://raw.githubusercontent.com/allwinner-zh/linux-3.4-sunxi/' +
                   pin['commit'] + '/modules/nand/nand_' + variant + '/lib/src/' + path)
            with urllib.request.urlopen(url, timeout=30) as response:
                data = response.read()
            assert sha(data) == expected, 'download checksum mismatch: ' + url
            file.write_bytes(data)
        data = file.read_bytes()
        assert sha(data) == expected, 'cache checksum mismatch: ' + file.name
        return variant, path, data.decode('latin1')

    with ThreadPoolExecutor(max_workers=4) as pool:
        for variant, path, source in pool.map(fetch, jobs):
            report['public_files_checked'][variant + '/' + path] = pin['variants'][variant][path]
            if path.endswith('/nfc_r.c'):
                for name, expected in pin['functions'].items():
                    assert sha(extract(source, name).encode('latin1')) == expected
            elif path.endswith('/nand_simple_r.c'):
                table = re.search(r'const __u16 random_seed\[128\] = \{(.*?)\};', source, re.S)[1]
                values = tuple(int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]+', table))
                assert values == seeds
            else:
                # Check the actual fixed-byte predicates and their page guards.
                for row in pin['spare_signatures']:
                    tokens = ''.join(f'.*?==\\s*0x{v:02x}\\b' for v in row['decoded_ff'])
                    guard = (r'if\(readop->page == 0\)' if row['page'] == 0 else
                             r'if\(\(readop->page%128\) == 127\)')
                    assert re.search(guard + r'\s*\{\s*if\(' + tokens, source, re.S), variant
                assert re.search(r'NFC_SetRandomSeed\(random_seed\);\s*NFC_RandomEnable\(\);', source)
    assert len(report['public_files_checked']) == 12
    return report
