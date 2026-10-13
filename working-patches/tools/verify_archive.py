#!/usr/bin/env python3
"""Read-only verification of archived files, symlinks and inventory."""
from pathlib import Path
import hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def digest(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
 return h.hexdigest()
def main():
 manifest=json.loads((ROOT/'manifest.json').read_text());errors=[]
 try:
  tracked=subprocess.check_output(['git','-C',str(ROOT.parent),'ls-files','--cached','-z','--','working-patches'],stderr=subprocess.PIPE).decode().split('\0')
 except (OSError,subprocess.CalledProcessError) as e:
  print('ERROR: run this verifier inside a Git checkout:',e);return 2
 tracked={p.removeprefix('working-patches/') for p in tracked if p.startswith('working-patches/') and p not in {'working-patches/manifest.json','working-patches/SHA256SUMS'}}
 listed=set(manifest['files'])
 for name in sorted(tracked-listed):errors.append(name+': tracked file missing from manifest')
 for name in sorted(listed-tracked):errors.append(name+': manifest entry is not tracked by Git')
 for name,record in manifest['files'].items():
  p=ROOT/name
  if record['type']=='symlink':
   if not p.is_symlink() or str(p.readlink())!=record['target']:errors.append(name+': wrong/missing symlink');continue
   try:p.resolve(strict=True).relative_to(ROOT)
   except (ValueError,FileNotFoundError):errors.append(name+': broken or external symlink')
  elif not p.is_file() or p.is_symlink() or p.stat().st_size!=record['size'] or digest(p)!=record['sha256']:errors.append(name+': wrong/missing file')
 if errors:
  print('\n'.join(errors));return 1
 print('PASS:',len(manifest['files']),'files/symlinks verified; no live files changed.');return 0
if __name__=='__main__':sys.exit(main())
