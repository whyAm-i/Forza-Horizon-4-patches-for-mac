#!/usr/bin/env python3
"""Dry-run first; copy one verified file only after explicit target/hash checks."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,sys,tempfile,uuid
from datetime import datetime
ROOT=Path(__file__).resolve().parents[1]
def digest(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
 return h.hexdigest()
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('source',type=Path);ap.add_argument('target',type=Path)
 ap.add_argument('--expected',action='append',required=True,help='Reviewed current target SHA-256; may repeat')
 ap.add_argument('--source-sha256',help='Required only for an external rollback backup')
 ap.add_argument('--apply',action='store_true');ap.add_argument('--game-closed',action='store_true')
 a=ap.parse_args();source=a.source if a.source.is_absolute() else ROOT/a.source;target=a.target.expanduser()
 if target.is_symlink():raise ValueError('Refusing symlink target; identify the intended real file first.')
 if not source.is_file() or not target.is_file():raise ValueError('Source and target must be existing regular files.')
 if source.resolve()==target.resolve():raise ValueError('Source and target are the same file.')
 try:name=str(source.resolve().relative_to(ROOT))
 except ValueError:name=None
 if name is not None:
  record=json.loads((ROOT/'manifest.json').read_text())['files'].get(name)
  if not record or record['type']!='file':raise ValueError('Source is not an archived regular file.')
  expected_source=record['sha256']
 else:
  if not a.source_sha256:raise ValueError('External rollback source requires --source-sha256.')
  expected_source=a.source_sha256.lower()
 sh=digest(source);th=digest(target)
 if sh!=expected_source:raise ValueError('Source checksum mismatch.')
 print('Source SHA-256:',sh);print('Target SHA-256:',th)
 if sh==th:print('Already matches; nothing written.');return
 if th not in {h.lower() for h in a.expected}:raise ValueError('Target checksum is not an approved prior state; nothing written.')
 if not a.apply:print('DRY RUN: eligible. Add --apply --game-closed only after closing Forza and reviewing the target.');return
 if not a.game_closed:raise ValueError('Close Forza and bottle applications, then explicitly pass --game-closed.')
 backup=target.with_name(target.name+'.bak_fh4_archive_'+datetime.now().strftime('%Y%m%d-%H%M%S')+'_'+uuid.uuid4().hex[:8])
 shutil.copy2(target,backup)
 if digest(backup)!=th:raise ValueError('Backup verification failed; target unchanged.')
 fd,tmp=tempfile.mkstemp(prefix='.fh4-archive-',dir=target.parent)
 try:
  with os.fdopen(fd,'wb') as out,source.open('rb') as inp:
   shutil.copyfileobj(inp,out,1024*1024);out.flush();os.fsync(out.fileno())
  os.chmod(tmp,target.stat().st_mode & 0o777)
  if digest(Path(tmp))!=sh:raise ValueError('Staged copy checksum mismatch; target unchanged.')
  if target.is_symlink() or digest(target)!=th:raise ValueError('Target changed during preparation; target unchanged.')
  os.replace(tmp,target)
 finally:
  if os.path.exists(tmp):os.unlink(tmp)
 print('APPLIED:',target);print('Backup:',backup);print('Backup SHA-256:',th)
if __name__=='__main__':
 try:main()
 except (ValueError,OSError,KeyError) as e:print('REFUSED:',e,file=sys.stderr);sys.exit(1)
