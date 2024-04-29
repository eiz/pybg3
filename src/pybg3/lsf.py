from . import _pybg3


def loads(data: bytes):
    return _pybg3._LsofFile.from_data(data)
