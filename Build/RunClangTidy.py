#!/usr/bin/env python3
"""Runs the pinned clang-tidy over every hand-written translation unit, with the switches its project sets.

  python Build/RunClangTidy.py                            # every .cpp in the tree
  python Build/RunClangTidy.py NeuronCore/Foo.cpp          # only the files named
  python Build/RunClangTidy.py --dry-run                  # print the commands, run nothing
  python Build/RunClangTidy.py --configuration Release

Run it from a Developer PowerShell, or anywhere INCLUDE and VCINSTALLDIR are set: clang's MSVC driver finds the CRT
and the Windows SDK through INCLUDE, and the unit-test framework lives under VCINSTALLDIR. CI imports both before it
calls this script (AGENTS.md §6).

Each file is linted with the switches of the .vcxproj that owns it, read from that file, so a setting changed there
is linted as it is built. A compiler setting this script does not know how to translate stops it with a message
rather than being dropped: a switch clang-tidy does not see is a difference between what is built and what is
linted. Directories outside the repository (the unit-test framework) are passed as system directories, so their
headers and the macros they define are Microsoft's code, not ours.
"""

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent
CONFIGURATIONS = ('Debug', 'Release')
CONDITION = re.compile(r"^\s*'\$\(Configuration\)\|\$\(Platform\)'\s*==\s*'([^']+)'\s*$")

# Project settings and what clang-cl is told for them.
SWITCHES = {
  ('LanguageStandard', 'stdcpplatest'): ['/std:c++latest'],
  ('ConformanceMode', 'true'): ['/permissive-'],
  ('EnableEnhancedInstructionSet', 'AdvancedVectorExtensions2'): ['/arch:AVX2'],
  ('FloatingPointModel', 'Precise'): ['/fp:precise'],
  ('RuntimeLibrary', 'MultiThreadedDebugDLL'): ['/MDd'],
  ('RuntimeLibrary', 'MultiThreadedDLL'): ['/MD'],
  ('ExceptionHandling', 'Sync'): ['/EHsc'],
}
PASSED_OPTIONS = {'/utf-8'}

# Settings that change code generation, diagnostics policy or build mechanics, but not what the code means.
IRRELEVANT = {'PrecompiledHeader', 'PrecompiledHeaderFile', 'WarningLevel', 'TreatWarningAsError', 'SDLCheck',
              'DebugInformationFormat', 'Optimization', 'FunctionLevelLinking', 'IntrinsicFunctions',
              'MultiProcessorCompilation', 'UseFullPaths', 'ObjectFileName', 'ProgramDataBaseFileName'}


def local_name(tag):
  return tag.split('}', 1)[1] if tag.startswith('{') else tag


def tree_sources():
  listing = subprocess.run(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT,
                           capture_output=True, check=True).stdout.decode('utf-8')
  return sorted({name for name in listing.split('\0') if name.endswith('.cpp') and (ROOT / name).is_file()})


def applies(element, configuration):
  condition = element.get('Condition')
  if condition is None:
    return True
  match = CONDITION.match(condition)
  return bool(match) and match.group(1) == f'{configuration}|x64'


def project_settings(vcxproj, configuration, source_name):
  """ClCompile settings for one file in one configuration, the project's CharacterSet, and the build-path macros its
  settings may use: $(IntDir) is where the build writes generated headers, the shaders' among them (ADR-005)."""
  root = ET.parse(vcxproj).getroot()
  settings = {}
  character_set = None
  macros = {'SolutionDir': str(ROOT) + os.sep, 'Configuration': configuration, 'Platform': 'x64',
            'ProjectName': vcxproj.stem}
  for group in root:
    tag = local_name(group.tag)
    if not applies(group, configuration):
      continue
    if tag == 'PropertyGroup':
      for prop in group:
        name = local_name(prop.tag)
        if name == 'CharacterSet' and applies(prop, configuration):
          character_set = (prop.text or '').strip()
        elif name in ('IntDir', 'OutDir') and applies(prop, configuration):
          macros[name] = (prop.text or '').strip()
    elif tag == 'ItemDefinitionGroup':
      for tool in group:
        if local_name(tool.tag) == 'ClCompile' and applies(tool, configuration):
          for setting in tool:
            if applies(setting, configuration):
              settings[local_name(setting.tag)] = (setting.text or '').strip()
    elif tag == 'ItemGroup':
      for item in group:
        if local_name(item.tag) == 'ClCompile' and (item.get('Include') or '').replace('\\', '/') == source_name:
          for setting in item:
            if applies(setting, configuration):
              settings[local_name(setting.tag)] = (setting.text or '').strip()
  return settings, character_set, macros


def expand(entry, dry_run, macros):
  def replace(match):
    name = match.group(1)
    if name in macros:
      return expand(macros[name], dry_run, {key: value for key, value in macros.items() if key != name})
    value = os.environ.get(name.upper())
    if value is None:
      if dry_run:
        return f'<{name}>' + os.sep
      raise SystemExit(f'RunClangTidy: $({name}) is not set; run from a Developer PowerShell (AGENTS.md §3).')
    return value if value.endswith(('\\', '/')) else value + os.sep

  return re.sub(r'\$\(([A-Za-z_]+)\)', replace, entry)


def switches_for(source, configuration, dry_run):
  folder = PurePosixPath(source).parent.as_posix()
  vcxproj = ROOT / folder / f'{folder}.vcxproj'
  if not vcxproj.is_file():
    raise SystemExit(f'RunClangTidy: {source} has no project at {folder}/{folder}.vcxproj (AGENTS.md §2).')
  settings, character_set, macros = project_settings(vcxproj, configuration, PurePosixPath(source).name)
  settings.setdefault('ExceptionHandling', 'Sync')

  switches = ['--driver-mode=cl']
  problems = []
  for name, value in sorted(settings.items()):
    if (name, value) in SWITCHES:
      switches += SWITCHES[(name, value)]
    elif name == 'PreprocessorDefinitions':
      switches += [f'/D{define}' for define in value.split(';') if define and not define.startswith('%(')]
    elif name == 'AdditionalIncludeDirectories':
      for entry in value.split(';'):
        if not entry or entry.startswith('%('):
          continue
        directory = os.path.normpath(expand(entry, dry_run, macros).replace('\\', os.sep))
        inside = os.path.normcase(directory).startswith(os.path.normcase(str(ROOT)))
        switches += ['/I', directory] if inside else ['/imsvc', directory]
    elif name == 'AdditionalOptions':
      for option in value.split():
        if option.startswith('%('):
          continue
        if option in PASSED_OPTIONS:
          switches.append(option)
        else:
          problems.append(f'AdditionalOptions {option}')
    elif name not in IRRELEVANT:
      problems.append(f'{name}={value}')
  if character_set == 'Unicode':
    switches += ['/DUNICODE', '/D_UNICODE']
  elif character_set is not None:
    problems.append(f'CharacterSet={character_set}')
  if problems:
    raise SystemExit(f'RunClangTidy: {vcxproj.relative_to(ROOT).as_posix()} sets {", ".join(problems)}, which this '
                     'script does not translate for clang-cl. Teach it, or the linter sees different code from the '
                     'compiler.')
  return switches


def pinned_version():
  text = (ROOT / '.github' / 'workflows' / 'build.yml').read_text(encoding='utf-8')
  match = re.search(r'CLANG_TIDY_VERSION:\s*([0-9.]+)', text)
  return match.group(1) if match else None


def main():
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument('files', nargs='*', help='translation units to lint; default: every .cpp in the tree')
  parser.add_argument('--configuration', choices=CONFIGURATIONS, default='Debug')
  parser.add_argument('--clang-tidy', default='clang-tidy', help='the clang-tidy binary to run')
  parser.add_argument('--dry-run', action='store_true', help='print the commands and run nothing')
  arguments = parser.parse_args()

  if not arguments.dry_run and 'INCLUDE' not in os.environ:
    print('RunClangTidy: INCLUDE is not set, so clang cannot see the CRT or the Windows SDK. Run it from a Developer '
          'PowerShell (AGENTS.md §3).')
    return 2

  if arguments.files:
    sources = sorted({Path(name).resolve().relative_to(ROOT).as_posix() for name in arguments.files})
  else:
    sources = tree_sources()
  commands = [(source, [arguments.clang_tidy, '--quiet', str(ROOT / source), '--']
               + switches_for(source, arguments.configuration, arguments.dry_run)) for source in sources]

  if arguments.dry_run:
    for _, command in commands:
      print(subprocess.list2cmdline(command))
    return 0

  try:
    banner = subprocess.run([arguments.clang_tidy, '--version'], capture_output=True, text=True, check=True).stdout
  except (OSError, subprocess.CalledProcessError) as error:
    print(f'RunClangTidy: cannot run {arguments.clang_tidy}: {error}')
    return 2
  match = re.search(r'LLVM version (\d+\.\d+\.\d+)', banner)
  found = match.group(1) if match else banner.strip()
  pinned = pinned_version()
  print(f'RunClangTidy: clang-tidy {found}, {arguments.configuration}|x64, {len(sources)} translation unit(s)')
  if found != pinned:
    print(f'RunClangTidy: warning: CI pins clang-tidy {pinned}; another version may report different findings.')

  def run(entry):
    source, command = entry
    completed = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
    return source, completed.returncode, (completed.stdout + completed.stderr).strip()

  failures = 0
  with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
    for source, returncode, output in pool.map(run, commands):
      if returncode != 0:
        failures += 1
        print(f'\n{source}: clang-tidy exited with {returncode}\n{output}')
      elif output:
        print(f'\n{source}:\n{output}')
  if failures:
    print(f'\nRunClangTidy: {failures} of {len(sources)} translation unit(s) have findings.')
    return 1
  print('RunClangTidy: clean.')
  return 0


if __name__ == '__main__':
  sys.exit(main())
