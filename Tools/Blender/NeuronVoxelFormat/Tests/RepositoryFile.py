"""Finds the repository's files for the tests, as NeuronCoreTests/RepositoryFile.cpp does for the C++ suite."""

from pathlib import Path


def find_repository_file(relative):
  """`relative` in the nearest directory above this file that holds it: the golden file, the assets, the C++ sources."""
  for directory in Path(__file__).resolve().parents:
    if (directory / relative).is_file():
      return directory / relative
  raise FileNotFoundError(f'{relative} is not above {Path(__file__).parent}')


def read_repository_file(relative):
  return find_repository_file(relative).read_bytes()
