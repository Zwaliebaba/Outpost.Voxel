#!/usr/bin/env python3
"""Checks, or with --fix rewrites, the layout of every hand-written C++ file against /.clang-format.

  python Build/CheckFormat.py                                   # check; exit 1 if any file is unformatted
  python Build/CheckFormat.py --fix                             # rewrite the offenders in place
  python Build/CheckFormat.py --clang-format clang-format-18    # name the binary, as CI does

CI runs it in its own Linux job on a pinned clang-format (AGENTS.md §4, §6): 18.1.3 exactly, because releases
disagree about where a long argument list breaks. The script prints the version it used and says so when it is not
the pinned one; if a local run disagrees with CI, compare that line first. HLSL is not checked yet: whether
clang-format formats it acceptably is decided when the first shader lands (Design/SampleRenderer.md, ADR-005).
"""

import argparse
import difflib
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PINNED_VERSION = '18.1.3'
EXTENSIONS = ('.cpp', '.h')
DIFF_LINES_PER_FILE = 40


def tree_files():
  """Every file git would track: tracked plus untracked-but-not-ignored, as repository-relative POSIX paths."""
  listing = subprocess.run(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT,
                           capture_output=True, check=True).stdout.decode('utf-8')
  return sorted({name for name in listing.split('\0') if name.endswith(EXTENSIONS) and (ROOT / name).is_file()})


def version_of(binary):
  output = subprocess.run([binary, '--version'], capture_output=True, text=True, check=True).stdout
  match = re.search(r'version (\d+\.\d+\.\d+)', output)
  return match.group(1) if match else output.strip()


def main():
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument('--clang-format', default='clang-format', help='the clang-format binary to run')
  parser.add_argument('--fix', action='store_true', help='rewrite unformatted files in place')
  arguments = parser.parse_args()

  try:
    found = version_of(arguments.clang_format)
  except (OSError, subprocess.CalledProcessError) as error:
    print(f'CheckFormat: cannot run {arguments.clang_format}: {error}')
    return 2
  print(f'CheckFormat: clang-format {found} ({arguments.clang_format})')
  if found != PINNED_VERSION:
    print(f'CheckFormat: warning: CI pins clang-format {PINNED_VERSION}; another version may break lines elsewhere, '
          'and the Linux job is the answer that counts.')

  files = tree_files()
  unformatted = []
  for path in files:
    original = (ROOT / path).read_bytes()
    formatted = subprocess.run([arguments.clang_format, '--style=file', str(ROOT / path)], capture_output=True,
                               check=True).stdout
    if formatted != original:
      unformatted.append((path, original, formatted))

  if arguments.fix:
    for path, _, formatted in unformatted:
      (ROOT / path).write_bytes(formatted)
    print(f'CheckFormat: rewrote {len(unformatted)} of {len(files)} file(s).')
    return 0

  for path, original, formatted in unformatted:
    diff = list(difflib.unified_diff(original.decode('utf-8', 'replace').splitlines(),
                                     formatted.decode('utf-8', 'replace').splitlines(),
                                     f'{path} (as written)', f'{path} (as formatted)', lineterm=''))
    print('\n'.join(diff[:DIFF_LINES_PER_FILE]))
    if len(diff) > DIFF_LINES_PER_FILE:
      print(f'... {len(diff) - DIFF_LINES_PER_FILE} more diff line(s)')
  if unformatted:
    print(f'\nCheckFormat: {len(unformatted)} of {len(files)} file(s) need formatting. '
          'Run python Build/CheckFormat.py --fix and commit the result.')
    return 1
  print(f'CheckFormat: clean. {len(files)} file(s) checked.')
  return 0


if __name__ == '__main__':
  sys.exit(main())
