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

import tempfile
import sexpdata
from pathlib import Path
from . import _pybg3

def decompile(input_path, output_path):
    status = _pybg3.osiris_decompile_path(input_path, output_path)
    if status != 0:
        raise Exception(f"Failed to decompile {input_path} to {output_path}")

def load_binary(input_path):
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_output_path = f"{temp_dir}/output"
        decompile(input_path, temp_output_path)
        text = Path(temp_output_path).read_text()
        return sexpdata.loads("(" + text + ")")
