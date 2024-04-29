from . import _pybg3

from dataclasses import dataclass


@dataclass
class LSString:
    string: str


@dataclass
class I32:
    value: int


@dataclass
class I64:
    value: int


@dataclass
class IVec2:
    x: int
    y: int


@dataclass
class IVec3:
    x: int
    y: int
    z: int


@dataclass
class IVec4:
    x: int
    y: int
    z: int
    w: int


@dataclass
class F32:
    value: float


@dataclass
class Vec2:
    x: float
    y: float


@dataclass
class Vec3:
    x: float
    y: float
    z: float


@dataclass
class Vec4:
    x: float
    y: float
    z: float
    w: float


class Node:
    def __init__(self, name, attrs, children):
        self.name = name
        self.attrs = attrs
        self.children = children

    def __add__(self, other):
        return Node(self.name, self.attrs, self.children + other)

    def __iadd__(self, other):
        self.children += other
        return self

    def __repr__(self):
        return f"Node({self.name}, {self.attrs}, {self.children})"


class NodeFactory:
    def __getattr__(self, name):
        def wrapper(**kwargs):
            return Node(name, kwargs, [])

        return wrapper


node = NodeFactory()


def loads(data: bytes):
    return _pybg3._LsofFile.from_data(data)
