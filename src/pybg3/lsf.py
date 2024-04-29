from . import _pybg3

from dataclasses import dataclass
from enum import Enum


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


class DataType(int, Enum):
    NONE = 0x00
    UINT8 = 0x01
    INT16 = 0x02
    UINT16 = 0x03
    INT32 = 0x04
    UINT32 = 0x05
    FLOAT = 0x06
    DOUBLE = 0x07
    IVEC2 = 0x08
    IVEC3 = 0x09
    IVEC4 = 0x0A
    VEC2 = 0x0B
    VEC3 = 0x0C
    VEC4 = 0x0D
    MAT2 = 0x0E
    MAT3 = 0x0F
    MAT3X4 = 0x10
    MAT4X3 = 0x11
    MAT4 = 0x12
    BOOL = 0x13
    STRING = 0x14
    PATH = 0x15
    FIXEDSTRING = 0x16
    LSSTRING = 0x17
    UINT64 = 0x18
    SCRATCHBUFFER = 0x19
    LONG = 0x1A
    INT8 = 0x1B
    TRANSLATEDSTRING = 0x1C
    WSTRING = 0x1D
    LSWSTRING = 0x1E
    UUID = 0x1F
    INT64 = 0x20
    TRANSLATEDFSSTRING = 0x21


class Node:
    def __init__(self, name, attrs, children):
        self.name = name
        self.attrs = attrs
        self.children = children

    @staticmethod
    def parse_node(file, node_idx):
        wide = file.is_wide()
        num_nodes = file.num_nodes()
        num_attrs = file.num_attrs()
        idx_stack = []
        node_stack = []
        is_root = True
        while node_idx < num_nodes:
            name, parent, next, attr_index = file.node(node_idx)
            while not is_root and parent != idx_stack[-1]:
                idx_stack.pop()
                child = node_stack.pop()
                if len(node_stack) == 0:
                    return (child, node_idx)
                node_stack[-1].children.append(child)
            idx_stack.append(node_idx)
            node = Node(name, {}, [])
            node_stack.append(node)
            is_root = False
            while attr_index != -1 and attr_index < num_attrs:
                a_name, a_type, a_next, a_owner, a_value = file.attr(attr_index)
                val = None
                if not wide and a_owner != node_idx:
                    break
                match a_type:
                    case DataType.INT32:
                        val = I32(a_value)
                    case DataType.LSSTRING:
                        val = LSString(a_value)
                    case DataType.FIXEDSTRING:
                        val = a_value
                    case DataType.FLOAT:
                        val = F32(a_value)
                node.attrs[a_name] = val
                if wide:
                    attr_index = a_next
                else:
                    attr_index += 1
            node_idx += 1
        while len(node_stack) > 1:
            idx_stack.pop()
            child = node_stack.pop()
            node_stack[-1].children.append(child)
        return (node_stack[0], node_idx)

    @staticmethod
    def parse_file(file):
        node_idx = 0
        num_nodes = file.num_nodes()
        nodes = []
        while node_idx < num_nodes:
            node, node_idx = Node.parse_node(file, node_idx)
            nodes.append(node)
        return nodes

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
