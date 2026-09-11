#!/usr/bin/env python3
"""Download hash-pinned public build inputs; no Git, no driver installation."""
import argparse, hashlib, io, json, pathlib, shutil, tarfile, tempfile, urllib.request
R = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--archive-cache', type=pathlib.Path, help='use only these already-downloaded archives; missing files or hash mismatches fail')
a = p.parse_args()
lock = json.loads((R/'Config/dependencies.lock.json').read_text())
deps = R/'.deps'; deps.mkdir(exist_ok=True)
for name, item in lock.items():
    dest = deps/name
    if dest.exists():
        raise SystemExit(f'{dest} exists; use a fresh dependency directory to avoid silently trusting modified inputs')
    cache = a.archive_cache/(name+'.tar.gz') if a.archive_cache else None
    if cache is not None:
        if not cache.is_file(): raise SystemExit(f'{name}: missing cached archive: {cache}; no network fallback')
        data = cache.read_bytes()
    else:
        with urllib.request.urlopen(item['url'], timeout=90) as response: data = response.read()
    if hashlib.sha256(data).hexdigest() != item['sha256']:
        raise SystemExit(f'{name}: archive SHA-256 mismatch')
    with tempfile.TemporaryDirectory(dir=deps) as temporary:
        root = pathlib.Path(temporary).resolve()
        with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
            for member in archive.getmembers():
                target = (root/member.name).resolve()
                if not target.is_relative_to(root) or member.isdev() or member.isfifo():
                    raise SystemExit('Unsafe archive member')
                if member.issym() or member.islnk():
                    link = ((target.parent if member.issym() else root)/member.linkname).resolve()
                    if not link.is_relative_to(root): raise SystemExit('Unsafe archive link')
            archive.extractall(root)
        children = list(root.iterdir())
        if len(children) != 1 or not children[0].is_dir(): raise SystemExit('Unexpected archive root')
        shutil.move(str(children[0]), dest)
    print(f'PASS {name}: {item["commit"]}, archive hash verified')
