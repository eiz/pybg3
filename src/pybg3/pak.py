# pybg3
#
# Copyright (C) 2024 Mackenzie Straight.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the “Software”), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

from typing import Iterable
from pathlib import Path
from . import _pybg3


class PakFile:
    _lspk: _pybg3._LspkFile
    _index: dict[str, int]

    def __init__(self, path: Path):
        self._lspk = _pybg3._LspkFile(str(path))
        self._index = {}
        for i in range(self._lspk.num_files()):
            self._index[self._lspk.file_name(i)] = i
        num_parts = self._lspk.num_parts()
        if num_parts > 1:
            for i in range(1, num_parts):
                part_path = path.with_name(f"{path.stem}_{i}{path.suffix}")
                self._lspk.attach_part(i, str(part_path))

    def files(self) -> Iterable[str]:
        return self._index.keys()

    def file_data(self, name: str) -> bytes:
        return self._lspk.file_data(self._index[name])

    def file_part(self, name: str) -> int:
        return self._lspk.file_part(self._index[name])
