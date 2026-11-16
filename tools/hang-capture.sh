#!/bin/bash
# SPDX-License-Identifier: MIT
# One-shot, read-only diagnostics. No display user-client open, mode change,
# installation, login service, workload, or application termination.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
exec python3 - "$REPO" "$@" <<'PY'
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

repo = Path(sys.argv[1])
parser = argparse.ArgumentParser(description='本机一次性只读采集；原始证据仅保存在 local/')
parser.add_argument('--dry-run', action='store_true', help='只列项目，不运行采集或创建目录')
parser.add_argument('--minutes', type=int, default=15, help='历史日志和报告窗口，1–1440 分钟；重启后可用 60')
args = parser.parse_args(sys.argv[2:])
if not 1 <= args.minutes <= 1440:
    parser.error('--minutes 必须在 1–1440 之间')

ctl = repo / 'probe/TGLSyncFlip/build/tglsync-ctl'
capture = repo / 'build/tgl-capture'
predicate = ('process == "WindowServer" OR '
             'senderImagePath CONTAINS[c] "Intel" OR '
             'eventMessage CONTAINS[c] "GPU" OR '
             'eventMessage CONTAINS[c] "IOAccel" OR '
             'eventMessage CONTAINS[c] "WEX" OR '
             'eventMessage CONTAINS[c] "tglsync"')
commands = [
    ('boot-time', 5, ['/usr/sbin/sysctl', 'kern.boottime']),
    ('os-version', 5, ['/usr/bin/sw_vers']),
    ('processes', 5, ['/bin/ps', '-axo', 'pid=,ppid=,%cpu=,rss=,etime=,comm=']),
    ('cpu-memory', 8, ['/usr/bin/top', '-l', '1', '-n', '30', '-o', 'cpu',
                       '-stats', 'pid,command,cpu,mem,threads,state']),
    ('registry-root', 8, ['/usr/sbin/ioreg', '-a', '-d', '1', '-p', 'IOService']),
    ('registry-syncflip', 8, ['/usr/sbin/ioreg', '-a', '-r', '-c', 'TGLSyncFlip']),
    ('registry-displayprobe', 8, ['/usr/sbin/ioreg', '-a', '-r', '-c', 'TGLDisplayProbe']),
    ('sync-status', 5, ['/usr/bin/sudo', '-n', str(ctl), 'status']),
    ('gpu-log', 30, ['/usr/bin/log', 'show', '--last', f'{args.minutes}m',
                      '--style', 'ndjson', '--info', '--predicate', predicate]),
    ('loaded-kexts', 15, ['/usr/bin/kmutil', 'showloaded']),
    ('power-settings', 5, ['/usr/bin/pmset', '-g', 'custom']),
    ('thermal-status', 5, ['/usr/bin/pmset', '-g', 'therm']),
    ('power-log', 10, ['/usr/bin/pmset', '-g', 'log']),
    ('displays', 15, ['/usr/sbin/system_profiler', 'SPDisplaysDataType', '-json']),
]
if args.dry_run:
    print(f'只读；最近 {args.minutes} 分钟；输出 local/hang-时间戳/（不会创建）')
    print('先复制窗口内诊断报告：最多 24 个、每个 16 MiB、合计 64 MiB；超限记录为 skipped')
    for name, limit, command in commands:
        print(f'{name}: timeout={limit}s ' + json.dumps(command, ensure_ascii=False))
    print('cpu-registry: tgl-capture 默认 CPU registry 模式，1秒；不使用 --display/--probe')
    print('footprint: 上述进程列表中的 WindowServer / UU / Windows App / VTDecoder，每进程最多8秒')
    print('无 sudo 交互：权限不足、超时、缺工具均记录 unavailable，绝不补零')
    sys.exit(0)

os.umask(0o077)
local = repo / 'local'
if local.is_symlink() or (local.exists() and not local.is_dir()):
    sys.exit('拒绝写入：local 不是本仓库的普通目录')
local.mkdir(mode=0o700, exist_ok=True)
out = local / ('hang-' + dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
out.mkdir(mode=0o700)
manifest = {'schema': 1, 'started_at': dt.datetime.now().astimezone().isoformat(),
            'minutes': args.minutes, 'read_only': True, 'mode_changed': False,
            'reports': [], 'commands': [],
            'limits': {'files': 24, 'per_file_bytes': 16*1024**2, 'total_bytes': 64*1024**2},
            'limitations': ['不增加内核观测能力；没有 IRQ 或等待队列实时快照的承诺',
                            '重启后 registry 是新启动；仅历史日志/报告可能覆盖故障',
                            '无进程或不可读不代表 CPU/footprint 为零']}

def save_manifest():
    with (out / 'manifest.json').open('w') as stream:
        json.dump(manifest, stream, indent=2, ensure_ascii=False)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())

def run(name, limit, command):
    record = {'name': name, 'command': command, 'started_at': dt.datetime.now().astimezone().isoformat()}
    started = time.monotonic()
    print(f'采集 {name}', flush=True)
    with (out / (name + '.out')).open('wb') as stdout, (out / (name + '.err')).open('wb') as stderr:
        try:
            result = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=stdout,
                                    stderr=stderr, timeout=limit, check=False)
            record.update(state='ok' if result.returncode == 0 else 'unavailable', returncode=result.returncode)
        except subprocess.TimeoutExpired:
            # subprocess terminates only its own diagnostic command, never an application.
            record.update(state='timeout', returncode=None)
        except OSError as error:
            record.update(state='unavailable', returncode=None, error=str(error))
    record['duration_seconds'] = round(time.monotonic() - started, 3)
    manifest['commands'].append(record)
    save_manifest()

save_manifest()
print(f'原始证据目录：{out}', flush=True)
# Save existing reports before slower diagnostics, including optional Retired.
report_out = out / 'reports'
report_out.mkdir(mode=0o700)
cutoff = time.time() - args.minutes * 60
copied = total = 0
for source in [Path('/Library/Logs/DiagnosticReports'),
               Path('/Library/Logs/DiagnosticReports/Retired'),
               Path.home() / 'Library/Logs/DiagnosticReports']:
    try:
        candidates = sorted(source.iterdir(), key=lambda p: p.name)
    except OSError as error:
        manifest['reports'].append({'source': str(source), 'state': 'unavailable', 'error': str(error)})
        continue
    for path in candidates:
        try:
            if path.is_symlink() or not path.is_file() or path.suffix not in {'.gpuRestart', '.ips', '.panic', '.spin', '.hang', '.diag', '.tailspin'}:
                continue
            stat = path.stat()
            if stat.st_mtime < cutoff:
                continue
            record = {'source': str(path), 'bytes': stat.st_size}
            if copied >= 24 or stat.st_size > 16*1024**2 or total + stat.st_size > 64*1024**2:
                record['state'] = 'skipped-limit'
            else:
                target = report_out / f'{copied:02d}-{path.name}'
                shutil.copyfile(path, target)
                copied += 1
                total += stat.st_size
                record.update(state='copied', destination=target.name)
        except OSError as error:
            record = {'source': str(path), 'state': 'unavailable', 'error': str(error)}
        manifest['reports'].append(record)
save_manifest()

for name, limit, command in commands:
    run(name, limit, command)
    if name == 'processes':
        # Use names rather than arguments to avoid collecting credentials from argv.
        selected = []
        for row in (out / 'processes.out').read_text(errors='replace').splitlines():
            fields = row.split(None, 5)
            if len(fields) == 6 and any(token in Path(fields[5]).name for token in
                                       ('WindowServer', 'UU', 'Windows App', 'VTDecoder')):
                selected.append((fields[0], Path(fields[5]).name))
        manifest['selected_processes'] = [{'pid': pid, 'name': name} for pid, name in selected]

# Default mode reads existing registry properties, without publishing display recorders.
if capture.is_file() and os.access(capture, os.X_OK):
    run('cpu-registry', 8, [str(capture), str(out / 'cpu-registry'), '1'])
else:
    manifest['commands'].append({'name': 'cpu-registry', 'state': 'unavailable', 'error': 'build/tgl-capture missing'})
for process in manifest.get('selected_processes', [])[:12]:
    pid = process['pid']
    run('footprint-' + pid, 8, ['/usr/bin/sudo', '-n', '/usr/bin/footprint', '-p', pid])
manifest['finished_at'] = dt.datetime.now().astimezone().isoformat()
save_manifest()
print(f'采集完成：{out}；不可用项见 manifest.json。原始文件不要推送。', flush=True)
PY
