#!/usr/bin/env python3
"""Checks the build shape and the rules of AGENTS.md that .clang-tidy cannot express.

Run from anywhere; CI runs it before the build (AGENTS.md §6):

  python Build/CheckProjectFiles.py

It checks, over the whole tree:

  - the solution: exactly one .slnx at the root, x64 only, listing every .vcxproj in the tree and nothing else;
  - the project registry: .clang-tidy's HeaderFilterRegex matches every project and names no project that is gone;
  - configurations: Debug|x64 and Release|x64, and nothing else (§3);
  - the settings §3 fixes, stated explicitly in both configurations;
  - Debug/Release alignment: the two differ only in the properties §3 lists, and in _DEBUG against NDEBUG;
  - OutDir and IntDir anchored on $(SolutionDir) (§3);
  - include directories: a project never lists its own folder, lists another project's folder only as
    $(SolutionDir)<Project>, and only with a reference to that project (§3);
  - edges: no cycles, and nothing references an application or a test suite (§2, R9);
  - registration: every source file is in its project's .vcxproj and .filters, the two agree, and nothing
    listed is missing (§2);
  - directory shape: C++ directly in its project's folder, HLSL in its project's Shader folder, and no source file
    anywhere else (§2, R17);
  - R2 type affixes, R7 file names, R11 spellings, R17 HLSL files;
  - every *Tests project holds at least one TEST_METHOD, because vstest reports an empty suite as a pass (§3).

Exit status 0 when clean, 1 with one line per finding otherwise. It needs only Python 3.10+ and git.
"""

import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent
MSBUILD_NAMESPACE = '{http://schemas.microsoft.com/developer/msbuild/2003}'

CONFIGURATIONS = ('Debug|x64', 'Release|x64')
CPP_EXTENSIONS = {'.cpp', '.h'}
HLSL_EXTENSIONS = {'.hlsl', '.hlsli'}
SOURCE_EXTENSIONS = CPP_EXTENSIONS | HLSL_EXTENSIONS
ITEM_TYPE_FOR_EXTENSION = {'.cpp': 'ClCompile', '.h': 'ClInclude', '.hlsl': 'FxCompile', '.hlsli': 'None'}
SHADER_FOLDER = 'Shader'  # AGENTS.md §2: a library's HLSL lives in <Project>/Shader; C++ stays flat
BANNED_EXTENSIONS = {'.hpp', '.hh', '.hxx', '.h++', '.cc', '.cxx', '.c++', '.inl', '.ipp', '.tpp', '.ixx', '.cppm',
                     '.fx', '.fxh'}
R7_EXCEPTIONS = {'pch.h', 'pch.cpp', 'framework.h', 'targetver.h', 'Resource.h'}
PASCAL_CASE_STEM = re.compile(r'^[A-Z][A-Za-z0-9]*$')

# AGENTS.md §3: what every configuration of every project states explicitly.
REQUIRED_SETTINGS = {
  ('Configuration', 'PlatformToolset'): 'v145',
  ('Configuration', 'CharacterSet'): 'Unicode',
  ('ClCompile', 'LanguageStandard'): 'stdcpplatest',
  ('ClCompile', 'ConformanceMode'): 'true',
  ('ClCompile', 'WarningLevel'): 'Level4',
  ('ClCompile', 'TreatWarningAsError'): 'true',
  ('ClCompile', 'FloatingPointModel'): 'Precise',
  ('ClCompile', 'EnableEnhancedInstructionSet'): 'AdvancedVectorExtensions2',
}

# AGENTS.md §3: the whole list of what may differ between Debug and Release, besides _DEBUG against NDEBUG.
MAY_DIFFER = {'Optimization', 'FunctionLevelLinking', 'IntrinsicFunctions', 'UseDebugLibraries', 'RuntimeLibrary',
              'LinkIncremental', 'WholeProgramOptimization', 'EnableCOMDATFolding', 'OptimizeReferences'}
DEFINES_MAY_DIFFER_IN = {'ClCompile', 'ResourceCompile'}

NOT_FILE_ITEMS = {'ProjectConfiguration', 'ProjectReference', 'Reference', 'PackageReference', 'Filter'}
OWN_FOLDER_MACROS = ('$(ProjectDir)', '$(MSBuildProjectDirectory)', '$(MSBuildThisFileDirectory)')

# R2: a type name carries no prefix or affix. clang-tidy can require an absent prefix but cannot see these.
R2_AFFIXES = (
  (re.compile(r'^[CEIS][A-Z][a-z0-9]'), 'a C, E, I or S prefix'),
  (re.compile(r'^(Base|Abstract)[A-Z]'), 'a Base or Abstract prefix'),
  (re.compile(r'[a-z0-9](Base|Abstract|Impl)$'), 'a Base, Abstract or Impl suffix'),
  (re.compile(r'_t$'), 'a _t suffix'),
)
TYPE_DECLARATION = re.compile(
  r'\b(?:class|struct|union|concept)\s+(?:alignas\s*\([^)]*\)\s*|\[\[[^\]]*\]\]\s*)*([A-Za-z_]\w*)'
  r'|\benum\s+(?:class\s+|struct\s+)?([A-Za-z_]\w*)'
  r'|\busing\s+([A-Za-z_]\w*)\s*='
  r'|\btypedef\b[^;{}]*?([A-Za-z_]\w*)\s*;')

# R11: one spelling per family, and it is the SDK's. Identifiers only; prose is not checked.
R11_SPELLINGS = {
  'colour': 'color', 'initialis': 'initializ', 'serialis': 'serializ', 'normalis': 'normaliz', 'quantis': 'quantiz',
  'synchronis': 'synchroniz', 'behaviour': 'behavior', 'neighbour': 'neighbor', 'centre': 'center', 'grey': 'gray',
  'cancelled': 'canceled', 'cancelling': 'canceling',
}
IDENTIFIER = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')


class Findings:
  def __init__(self):
    self.lines = []

  def add(self, where, rule, message):
    self.lines.append(f'{where}: [{rule}] {message}')


def local_name(tag):
  return tag.split('}', 1)[1] if tag.startswith('{') else tag


def tree_files():
  """Every file git would track: tracked plus untracked-but-not-ignored, as repository-relative POSIX paths."""
  listing = subprocess.run(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT,
                           capture_output=True, check=True).stdout.decode('utf-8')
  return sorted({name for name in listing.split('\0') if name and (ROOT / name).is_file()})


def read_text(path):
  return (ROOT / path).read_text(encoding='utf-8', errors='replace')


def strip_comments_and_literals(text):
  """Blanks out comments, string literals and character literals, keeping line breaks so line numbers survive."""
  out = []
  i = 0
  n = len(text)
  while i < n:
    c = text[i]
    nxt = text[i + 1] if i + 1 < n else ''
    if c == '/' and nxt == '/':
      end = text.find('\n', i)
      i = n if end < 0 else end
      out.append(' ')
    elif c == '/' and nxt == '*':
      end = text.find('*/', i + 2)
      end = n if end < 0 else end + 2
      out.append(' ' + '\n' * text.count('\n', i, end))
      i = end
    elif c == 'R' and nxt == '"' and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
      open_paren = text.find('(', i + 2)
      delimiter = text[i + 2:open_paren] if open_paren >= 0 else ''
      end = text.find(')' + delimiter + '"', open_paren + 1) if open_paren >= 0 else -1
      end = n if end < 0 else end + len(delimiter) + 2
      out.append('""' + '\n' * text.count('\n', i, end))
      i = end
    elif c == '"' or c == "'":
      previous = text[i - 1] if i > 0 else ''
      if c == "'" and previous.isalnum() and nxt.isalnum():  # a digit separator: 1'000'000
        i += 1
        continue
      j = i + 1
      while j < n and text[j] != c and text[j] != '\n':
        j += 2 if text[j] == '\\' else 1
      out.append(c + c)
      i = j + 1
    else:
      out.append(c)
      i += 1
  return ''.join(out)


def line_of(text, index):
  return text.count('\n', 0, index) + 1


# ── Solution and projects ─────────────────────────────────────────────────────────────────────────────


class Project:
  def __init__(self, name, path):
    self.name = name
    self.path = path  # repository-relative POSIX path of the .vcxproj
    self.folder = PurePosixPath(path).parent.as_posix()
    self.xml = None
    self.settings = {configuration: {} for configuration in CONFIGURATIONS}
    self.configurations = set()
    self.items = set()  # (item type, repository-relative path)
    self.references = []  # project names
    self.kind = None
    self.is_test_suite = name.endswith('Tests')


CONDITION = re.compile(r"^\s*'\$\(Configuration\)\|\$\(Platform\)'\s*==\s*'([^']+)'\s*$")
# NuGet imports a package's targets only if the file is there: <Import Project="X" Condition="Exists('X')" />. The
# test comes out the same in both configurations, so it cannot set them apart; a package that was never restored
# fails the build from the EnsureNuGetPackageBuildImports target NuGet writes beside the import.
IMPORT_GUARD = re.compile(r"^\s*exists\s*\(\s*'([^']+)'\s*\)\s*$", re.IGNORECASE)


def resolve_item_path(project, include):
  path = include.replace('\\', '/')
  if path.startswith('$(SolutionDir)'):
    return PurePosixPath(path[len('$(SolutionDir)'):]).as_posix()
  if '$(' in path:
    return None
  parts = []
  for part in PurePosixPath(project.folder, path).parts:
    if part == '..':
      if parts:
        parts.pop()
    elif part != '.':
      parts.append(part)
  return '/'.join(parts)


def configurations_of(element, inherited, project, findings):
  condition = element.get('Condition')
  if condition is None:
    return inherited
  guard = IMPORT_GUARD.match(condition)
  if guard and local_name(element.tag) == 'Import' and guard.group(1) == element.get('Project'):
    return inherited
  match = CONDITION.match(condition)
  if not match:
    findings.add(project.path, '§3', f'unrecognised condition "{condition}"; conditions name one configuration, '
                 "as '$(Configuration)|$(Platform)'=='Debug|x64'")
    return set()
  if match.group(1) not in CONFIGURATIONS:
    findings.add(project.path, '§3', f'condition for {match.group(1)}; x64 Debug and Release are the only '
                 'configurations')
    return set()
  return {match.group(1)} & inherited


def load_project(project, findings):
  try:
    project.xml = ET.parse(ROOT / project.path).getroot()
  except ET.ParseError as error:
    findings.add(project.path, 'xml', f'does not parse: {error}')
    return

  def put(configurations, key, value):
    for configuration in configurations:
      project.settings[configuration][key] = (value or '').strip()

  every = set(CONFIGURATIONS)
  for group in project.xml:
    tag = local_name(group.tag)
    group_configurations = configurations_of(group, every, project, findings)
    if tag == 'PropertyGroup':
      label = group.get('Label') or 'Properties'
      for prop in group:
        put(configurations_of(prop, group_configurations, project, findings), (label, local_name(prop.tag)), prop.text)
    elif tag == 'ItemDefinitionGroup':
      for tool in group:
        tool_configurations = configurations_of(tool, group_configurations, project, findings)
        for setting in tool:
          put(configurations_of(setting, tool_configurations, project, findings),
              (local_name(tool.tag), local_name(setting.tag)), setting.text)
    elif tag in ('ImportGroup', 'Import'):
      imports = [group] if tag == 'Import' else list(group)
      for element in imports:
        put(configurations_of(element, group_configurations, project, findings), ('Import', element.get('Project')),
            'imported')
    elif tag == 'ItemGroup':
      for item in group:
        item_type = local_name(item.tag)
        include = item.get('Include') or ''
        if item_type == 'ProjectConfiguration':
          project.configurations.add(include)
          continue
        item_configurations = configurations_of(item, group_configurations, project, findings)
        if item_type == 'ProjectReference':
          project.references.append(PurePosixPath(include.replace('\\', '/')).stem)
          continue
        if item_type in NOT_FILE_ITEMS:
          continue
        path = resolve_item_path(project, include)
        if path is not None:
          project.items.add((item_type, path))
        put(item_configurations, (f'{item_type}:{include}', '(listed)'), 'yes')
        for metadata in item:
          put(configurations_of(metadata, item_configurations, project, findings),
              (f'{item_type}:{include}', local_name(metadata.tag)), metadata.text)
  project.kind = project.settings['Debug|x64'].get(('Configuration', 'ConfigurationType'))


def load_filters(project, findings):
  path = project.path + '.filters'
  if not (ROOT / path).is_file():
    findings.add(project.path, '§2', f'has no {PurePosixPath(path).name}; every project keeps one')
    return None
  try:
    root = ET.parse(ROOT / path).getroot()
  except ET.ParseError as error:
    findings.add(path, 'xml', f'does not parse: {error}')
    return None
  items = set()
  for group in root:
    if local_name(group.tag) != 'ItemGroup':
      continue
    for item in group:
      item_type = local_name(item.tag)
      if item_type in NOT_FILE_ITEMS:
        continue
      resolved = resolve_item_path(project, item.get('Include') or '')
      if resolved is not None:
        items.add((item_type, resolved))
  return items


def load_solution(files, findings):
  solutions = [name for name in files if '/' not in name and name.endswith(('.slnx', '.sln'))]
  if len(solutions) != 1 or not solutions[0].endswith('.slnx'):
    findings.add('.', 'solution', f'expected exactly one .slnx at the root, found {solutions or "none"}')
    return []
  solution = solutions[0]
  try:
    root = ET.parse(ROOT / solution).getroot()
  except ET.ParseError as error:
    findings.add(solution, 'xml', f'does not parse: {error}')
    return []
  platforms = {element.get('Name') for element in root.iter() if local_name(element.tag) == 'Platform'}
  if platforms != {'x64'}:
    findings.add(solution, '§3', f'declares platforms {sorted(platforms)}; x64 is the only platform')
  listed = []
  for element in root.iter():
    if local_name(element.tag) != 'Project':
      continue
    path = (element.get('Path') or '').replace('\\', '/')
    name = PurePosixPath(path).stem
    if PurePosixPath(path).parent.as_posix() != name or not path.endswith('.vcxproj'):
      findings.add(solution, '§2', f'lists {path}; a project lives at <Name>/<Name>.vcxproj')
    if path not in files:
      findings.add(solution, 'solution', f'lists {path}, which does not exist')
      continue
    listed.append(Project(name, path))
  on_disk = {name for name in files if name.endswith('.vcxproj')}
  for missing in sorted(on_disk - {project.path for project in listed}):
    findings.add(missing, 'solution', f'is not in {solution}; a project outside the solution is built by nothing')
  return listed


# ── Checks ────────────────────────────────────────────────────────────────────────────────────────────


def check_header_filter(projects, findings):
  text = read_text('.clang-tidy')
  match = re.search(r"^HeaderFilterRegex:\s*'([^']*)'\s*$", text, re.MULTILINE)
  if not match:
    findings.add('.clang-tidy', 'registry', 'has no single-quoted HeaderFilterRegex')
    return
  pattern = match.group(1)
  regex = re.compile(pattern)
  for project in projects:
    if not (regex.search(f'{project.name}/Probe.h') and regex.search(f'{project.name}\\Probe.h')):
      findings.add('.clang-tidy', 'registry', f'HeaderFilterRegex does not match {project.name}; its headers '
                   'would be checked by nothing (AGENTS.md §2)')
  alternation = re.match(r'^\(([^()]*)\)', pattern)
  names = {project.name for project in projects}
  for entry in (alternation.group(1).split('|') if alternation else []):
    if re.fullmatch(r'[A-Za-z0-9_]+', entry) and entry not in names:
      findings.add('.clang-tidy', 'registry', f'HeaderFilterRegex names {entry}, which is not a project')


def defines(value):
  return {entry.strip() for entry in (value or '').split(';') if entry.strip() and not entry.strip().startswith('%(')}


def check_settings(project, findings):
  if project.configurations != set(CONFIGURATIONS):
    findings.add(project.path, '§3', f'has configurations {sorted(project.configurations)}; exactly '
                 f'{", ".join(CONFIGURATIONS)}')
  for configuration in CONFIGURATIONS:
    settings = project.settings[configuration]
    for key, expected in REQUIRED_SETTINGS.items():
      actual = settings.get(key)
      if actual != expected:
        findings.add(project.path, '§3', f'{configuration} {key[1]} is {actual or "not stated"}; must be stated '
                     f'as {expected}')
    for directory in ('OutDir', 'IntDir'):
      value = settings.get(('Properties', directory), '')
      if not value.startswith('$(SolutionDir)'):
        findings.add(project.path, '§3', f'{configuration} {directory} is "{value}"; it must be anchored on '
                     '$(SolutionDir)')

  debug = project.settings['Debug|x64']
  release = project.settings['Release|x64']
  for key in sorted(set(debug) | set(release)):
    group, name = key
    if name == 'PreprocessorDefinitions' and group in DEFINES_MAY_DIFFER_IN:
      debug_defines = defines(debug.get(key))
      release_defines = defines(release.get(key))
      if ('_DEBUG' not in debug_defines or 'NDEBUG' in debug_defines or 'NDEBUG' not in release_defines
          or '_DEBUG' in release_defines or debug_defines - {'_DEBUG'} != release_defines - {'NDEBUG'}):
        findings.add(project.path, '§3', f'{group} PreprocessorDefinitions may differ only in _DEBUG (Debug) '
                     f'against NDEBUG (Release): Debug has {sorted(debug_defines)}, Release {sorted(release_defines)}')
    elif name not in MAY_DIFFER and debug.get(key) != release.get(key):
      findings.add(project.path, '§3', f'{group} {name} differs between Debug ({debug.get(key, "absent")}) and '
                   f'Release ({release.get(key, "absent")}); only the properties AGENTS.md §3 lists may')


def check_edges(projects, findings):
  by_name = {project.name: project for project in projects}
  for project in projects:
    for reference in project.references:
      target = by_name.get(reference)
      if target is None:
        findings.add(project.path, '§2', f'references {reference}, which is not a project in the solution')
      elif target.is_test_suite:
        findings.add(project.path, '§2', f'references the test suite {reference}')
      elif target.kind != 'StaticLibrary':
        findings.add(project.path, '§2', f'references {reference} ({target.kind}); only static libraries are '
                     'referenced')
    for configuration in CONFIGURATIONS:
      value = project.settings[configuration].get(('ClCompile', 'AdditionalIncludeDirectories'), '')
      for entry in (part.strip() for part in value.split(';')):
        if not entry or entry.startswith('%('):
          continue
        normalized = entry.replace('\\', '/').rstrip('/')
        if normalized.startswith(OWN_FOLDER_MACROS) or normalized in ('.', ''):
          findings.add(project.path, '§3', f'{configuration} lists its own folder ({entry}) on the include path')
        elif normalized.startswith('$(SolutionDir)'):
          folder = normalized[len('$(SolutionDir)'):]
          if folder == project.name:
            findings.add(project.path, '§3', f'{configuration} lists its own folder ({entry}) on the include path')
          elif folder not in by_name:
            findings.add(project.path, '§3', f'{configuration} includes {entry}, which is not a project folder')
          elif folder not in project.references:
            findings.add(project.path, '§2', f'{configuration} includes {folder} without referencing it; an '
                         'include is an edge, and edges are declared')
        elif not entry.startswith('$('):
          findings.add(project.path, '§3', f'{configuration} includes "{entry}"; project folders are listed as '
                       '$(SolutionDir)<Project>')

  state = {}

  def visit(name, trail):
    if state.get(name) == 'done':
      return
    if state.get(name) == 'active':
      findings.add(by_name[name].path, '§2', f'reference cycle: {" -> ".join(trail + [name])}')
      return
    state[name] = 'active'
    for reference in by_name[name].references:
      if reference in by_name:
        visit(reference, trail + [name])
    state[name] = 'done'

  for project in projects:
    visit(project.name, [])


def home_of(project_folder, suffix):
  """The one folder a source file of this kind may live in (AGENTS.md §2)."""
  return f'{project_folder}/{SHADER_FOLDER}' if suffix in HLSL_EXTENSIONS else project_folder


def check_registration(projects, files, findings):
  folders = {project.folder: project for project in projects}
  for project in projects:
    for item_type, path in sorted(project.items):
      if not (ROOT / path).is_file():
        findings.add(project.path, '§2', f'lists {path} ({item_type}), which does not exist')
      suffix = PurePosixPath(path).suffix.lower()
      if suffix in SOURCE_EXTENSIONS:
        home = home_of(project.folder, suffix)
        if PurePosixPath(path).parent.as_posix() != home:
          findings.add(project.path, '§2', f'lists {path}, which does not live in {home}')
        elif item_type != ITEM_TYPE_FOR_EXTENSION[suffix]:
          findings.add(project.path, '§2', f'lists {path} as {item_type}; a {suffix} file is '
                       f'{ITEM_TYPE_FOR_EXTENSION[suffix]}')
    filter_items = load_filters(project, findings)
    if filter_items is not None:
      for item_type, path in sorted(project.items - filter_items):
        findings.add(project.path + '.filters', '§2', f'does not list {path} ({item_type}); the .vcxproj does')
      for item_type, path in sorted(filter_items - project.items):
        findings.add(project.path + '.filters', '§2', f'lists {path} ({item_type}); the .vcxproj does not')

  listed = {path for project in projects for _, path in project.items}
  for path in files:
    posix = PurePosixPath(path)
    suffix = posix.suffix.lower()
    if suffix in BANNED_EXTENSIONS:
      findings.add(path, 'R7', f'{suffix} is not used here; C++ is .h and .cpp, HLSL is .hlsl and .hlsli')
      continue
    if suffix not in SOURCE_EXTENSIONS:
      continue
    top = posix.parts[0]
    if top not in folders or posix.parent.as_posix() != home_of(top, suffix):
      if suffix in HLSL_EXTENSIONS:
        findings.add(path, '§2', f'is not in a project\'s {SHADER_FOLDER} folder; HLSL lives in '
                     f'<Project>/{SHADER_FOLDER}, beside nothing but other HLSL (R17)')
      else:
        where = 'below its project folder' if top in folders else 'outside every project folder'
        findings.add(path, '§2', f'is {where}; C++ lives directly in its project\'s folder, where '
                     '.clang-tidy\'s HeaderFilterRegex can see it')
    elif path not in listed:
      findings.add(path, '§2', f'is not listed in {folders[top].path}; a file the project does not list is not built')
    if suffix in CPP_EXTENSIONS and posix.name in R7_EXCEPTIONS:
      continue
    if not PASCAL_CASE_STEM.match(posix.stem):
      findings.add(path, 'R7', 'file names are PascalCase, named for their primary type')


def check_sources(projects, files, findings):
  for path in files:
    suffix = PurePosixPath(path).suffix.lower()
    if suffix not in SOURCE_EXTENSIONS:
      continue
    raw = read_text(path)
    code = strip_comments_and_literals(raw)
    for match in TYPE_DECLARATION.finditer(code):
      name = next(group for group in match.groups() if group)
      for pattern, description in R2_AFFIXES:
        if pattern.search(name):
          findings.add(f'{path}:{line_of(code, match.start())}', 'R2', f'type {name} carries {description}; name '
                       'the concept and nothing else')
    for match in IDENTIFIER.finditer(code):
      lowered = match.group(0).lower()
      for british, american in R11_SPELLINGS.items():
        if british in lowered:
          findings.add(f'{path}:{line_of(code, match.start())}', 'R11', f'{match.group(0)} spells "{british}"; '
                       f'identifiers use the SDK\'s "{american}"')
    if suffix == '.hlsl':
      check_hlsl_entry_file(path, code, findings)

  sources_by_folder = {}
  for path in files:
    if PurePosixPath(path).suffix == '.cpp':
      sources_by_folder.setdefault(PurePosixPath(path).parent.as_posix(), []).append(path)
  for project in projects:
    if not project.is_test_suite:
      continue
    if project.kind != 'DynamicLibrary':
      findings.add(project.path, '§3', f'is a test suite but builds a {project.kind}; vstest runs DLLs')
    if not any('TEST_METHOD(' in strip_comments_and_literals(read_text(path))
               for path in sources_by_folder.get(project.folder, [])):
      findings.add(project.path, '§3', 'has no TEST_METHOD; vstest reports an empty suite as a pass, so keep '
                   'SuiteSmoke until the first real test lands')


def check_hlsl_entry_file(path, code, findings):
  """R17: a .hlsl file holds one entry point and nothing but switches and an include."""
  includes = 0
  for number, line in enumerate(code.split('\n'), start=1):
    stripped = line.strip()
    if not stripped or stripped.startswith('#define'):
      continue
    if stripped.startswith('#include'):
      includes += 1
      continue
    findings.add(f'{path}:{number}', 'R17', 'an entry-point file holds only #define switches and one #include; '
                 'code belongs in a .hlsli')
  if includes != 1:
    findings.add(path, 'R17', f'has {includes} #include lines; an entry-point file has exactly one')


def main():
  findings = Findings()
  files = tree_files()
  projects = load_solution(files, findings)
  for project in projects:
    load_project(project, findings)
  loaded = [project for project in projects if project.xml is not None]
  check_header_filter(loaded, findings)
  for project in loaded:
    check_settings(project, findings)
  check_edges(loaded, findings)
  check_registration(loaded, files, findings)
  check_sources(loaded, files, findings)

  if findings.lines:
    for line in sorted(set(findings.lines)):
      print(line)
    print(f'\nCheckProjectFiles: {len(set(findings.lines))} finding(s) in {len(loaded)} project(s).')
    return 1
  print(f'CheckProjectFiles: clean. {len(loaded)} project(s), {len(files)} file(s) checked.')
  return 0


if __name__ == '__main__':
  sys.exit(main())
