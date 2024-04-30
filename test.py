import os
import re
import time
from dataclasses import dataclass
from pathlib import Path
from pybg3 import pak, lsf, _pybg3


@dataclass
class LevelObjectSource:
    mod_name: str
    level_name: str
    type: str
    file_name: str
    pak: pak.PakFile
    lsf: _pybg3._LsofFile


class Level:
    def __init__(self):
        self.sources = []


class LevelSet:
    def __init__(self):
        self.levels = {}

    def get_or_create(self, name):
        if name not in self.levels:
            self.levels[name] = Level()
        return self.levels[name]

    def load_pak(self, pak):
        for name in pak.files():
            match = LEVEL_OBJECT_FILE_RE.match(name)
            if match:
                mod_name = match.group("mod_name")
                level_name = match.group("level_name")
                type = match.group("type")
                file_name = match.group("file_name")
                level = self.get_or_create(level_name)
                level.sources.append(
                    LevelObjectSource(
                        mod_name,
                        level_name,
                        type,
                        file_name,
                        pak,
                        lsf.loads(pak.file_data(name)),
                    )
                )


class RootTemplate:
    def __init__(self, uuid, name, file, node_idx):
        self.name = name
        self.uuid = uuid
        self._file = file
        self._node_idx = node_idx
        self._node = None

    @property
    def node(self):
        if self._node is None:
            self._node, _ = lsf.Node.parse_node(self._file, self._node_idx)
        return self._node


class RootTemplateSet:
    def __init__(self):
        self.by_name = {}
        self.by_uuid = {}

    def _index_root_templates(self, root_templates):
        n = root_templates.num_nodes()
        if n == 0:
            return
        _string_bytes, node_bytes, attr_bytes, _value_bytes = root_templates.stats()
        ROOT_TEMPLATE_INDEX_STATS["total_node_bytes"] += node_bytes
        ROOT_TEMPLATE_INDEX_STATS["total_attr_bytes"] += attr_bytes
        ROOT_TEMPLATE_INDEX_STATS["total_possible_nodes"] += n
        root_templates.ensure_sibling_pointers()
        node_idx = 1
        by_uuid = self.by_uuid
        by_name = self.by_name
        while node_idx != -1:
            ROOT_TEMPLATE_INDEX_STATS["total_nodes"] += 1
            name, parent, next, attrs = root_templates.node(node_idx)
            assert name == "GameObjects" and parent == 0
            attr_index = attrs
            n_found = 0
            uuid_value = None
            name_value = None
            while n_found < 2 and attr_index != -1:
                a_name, a_type, a_next, a_owner, a_value = root_templates.attr(attr_index)
                if a_name == "MapKey":
                    uuid_value = a_value
                    n_found += 1
                if a_name == "Name":
                    name_value = a_value
                    n_found += 1
                attr_index = a_next
            if n_found == 2:
                template = RootTemplate(uuid_value, name_value, root_templates, node_idx)
                by_uuid[uuid_value] = template
                by_name[name_value] = template
            node_idx = next

    def load_mod(self, pak, mod_name):
        root_templates = lsf.loads(
            pak.file_data(f"Public/{mod_name}/RootTemplates/_merged.lsf")
        )
        self._index_root_templates(root_templates)


def checktime(msg, cb):
    t_start = time.time()
    rv = cb()
    t_end = time.time()
    print(f"{msg}: {t_end - t_start}")
    return rv


def load_asset_banks(pak):
    for name in pak.files():
        if "[PAK]" in name and name.endswith(".lsf"):
            BANKS[name] = lsf.loads(pak.file_data(name))


def check_for_multiple_roots_wide(pak):
    for name in pak.files():
        if not name.endswith(".lsf"):
            continue
        try:
            reader = lsf.loads(pak.file_data(name))
            if not reader.is_wide():
                continue
            num_roots = 0
            first_root = None
            for node_idx in range(reader.num_nodes()):
                node_name, parent, next, attrs = reader.node(node_idx)
                if parent == -1:
                    if first_root is None:
                        first_root = node_idx
                    num_roots += 1
                if num_roots > 1:
                    _, _, first_next, _ = reader.node(first_root)
                    print(f"{name} has multiple roots, first node's next is {first_next}")
                    break
        except Exception:
            pass


def index_all_root_templates():
    checktime("shared root templates", lambda: ROOT_TEMPLATES.load_mod(SHARED, "Shared"))
    checktime(
        "shareddev root templates", lambda: ROOT_TEMPLATES.load_mod(SHARED, "SharedDev")
    )
    checktime("gustav root templates", lambda: ROOT_TEMPLATES.load_mod(GUSTAV, "Gustav"))
    checktime(
        "gustavdev root templates", lambda: ROOT_TEMPLATES.load_mod(GUSTAV, "GustavDev")
    )
    print(ROOT_TEMPLATE_INDEX_STATS)


BG3_ROOT = Path(os.environ.get("BG3_DATA", os.path.expanduser("~/l/bg3/Data")))
GUSTAV = checktime("Gustav.pak", lambda: pak.PakFile(BG3_ROOT / "Gustav.pak"))
SHARED = checktime("Shared.pak", lambda: pak.PakFile(BG3_ROOT / "Shared.pak"))
ROOT_TEMPLATES = RootTemplateSet()
BANKS = {}
LEVEL_OBJECT_FILE_RE = re.compile(
    r"Mods/(?P<mod_name>[^/]+)/Levels/(?P<level_name>[^/]+)/(?P<type>LevelTemplates|Characters|Decals|Items|FogVolumes|TileConstructions|Terrains|Triggers|Lights|LightProbes|CombinedLights|Scenery|Splines)/(?P<file_name>[^/]+).lsf"
)
LEVELS = LevelSet()
ROOT_TEMPLATE_INDEX_STATS = {
    "total_attr_bytes": 0,
    "total_node_bytes": 0,
    "total_nodes": 0,
    "total_possible_nodes": 0,
}
# check_for_multiple_roots_wide(SHARED)
checktime("shared assets", lambda: load_asset_banks(SHARED))
checktime("gustav assets", lambda: load_asset_banks(GUSTAV))
checktime("root templates", index_all_root_templates)
index = checktime(
    "load index",
    lambda: _pybg3._IndexReader(os.path.expanduser("~/code/libbg3/tmp/bg3.idx")),
)
# print(index.query("mysterious artefact"))
# print(index.query("infernalbox"))
checktime("levels", lambda: LEVELS.load_pak(GUSTAV))
AIN_Main_A_Characters = lsf.node.Templates() + [
    lsf.node.GameObjects(
        AnubisConfigName="Patroller",
        Archetype="deathShepherd",
        Flag=lsf.I32(1),
        LevelName="AIN_Main_A",
        MapKey="886b6ed8-4b76-59a4-7daa-187ba6c23f6c",
        Name=lsf.LSString("S_AIN_DeathBoi"),
        TemplateName="f05367f6-78f2-4631-996e-7e21912bbb78",
        Type="character",
        _OriginalFileVersion_=lsf.I64(144115207403209026),
    )
    + [
        lsf.node.LocomotionParams(),
        lsf.node.Transform(
            Position=lsf.Vec3(-3.0, 0.0, 0.0),
            RotationQuat=lsf.Vec4(0.0, 0.469850, 0.0, 0.882746),
            Scale=lsf.F32(1.5),
        ),
    ]
]
# checktime("query1", lambda: print(index.query("f05367f6-78f2-4631-996e-7e21912bbb78")))
death_shepherd = ROOT_TEMPLATES.by_uuid["f05367f6-78f2-4631-996e-7e21912bbb78"]
t = death_shepherd.node
print(len(t.children))
print(t.attrs)
