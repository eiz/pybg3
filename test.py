import os
import re
from dataclasses import dataclass
from pathlib import Path
from pybg3 import pak, lsf, _pybg3

BG3_ROOT = Path(os.environ.get("BG3_DATA", os.path.expanduser("~/l/bg3/Data")))
GUSTAV = pak.PakFile(BG3_ROOT / "Gustav.pak")
SHARED = pak.PakFile(BG3_ROOT / "Shared.pak")
SHARED_ROOT_TEMPLATES = lsf.loads(
    SHARED.file_data("Public/Shared/RootTemplates/_merged.lsf")
)
GUSTAV_ROOT_TEMPLATES = lsf.loads(
    GUSTAV.file_data("Public/Gustav/RootTemplates/_merged.lsf")
)
GUSTAVDEV_ROOT_TEMPLATES = lsf.loads(
    GUSTAV.file_data("Public/GustavDev/RootTemplates/_merged.lsf")
)
BANKS = {}
LEVEL_OBJECT_FILE_RE = re.compile(
    r"Mods/(?P<mod_name>[^/]+)/Levels/(?P<level_name>[^/]+)/(?P<type>LevelTemplates|Characters|Decals|Items|FogVolumes|TileConstructions|Terrains|Triggers|Lights|LightProbes|CombinedLights|Scenery|Splines)/(?P<file_name>[^/]+).lsf"
)


def load_asset_banks(pak):
    for name in pak.files():
        if "[PAK]" in name and name.endswith(".lsf"):
            BANKS[name] = lsf.loads(pak.file_data(name))


load_asset_banks(SHARED)
load_asset_banks(GUSTAV)

ROOT_TEMPLATES_BY_UUID = {}
ROOT_TEMPLATES_BY_NAME = {}


@dataclass
class LevelObjectSource:
    mod_name: str
    level_name: str
    type: str
    file_name: str
    pak: pak.PakFile
    lsf: _pybg3._LsofFile


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


class Level:
    def __init__(self):
        self.sources = []


LEVELS = LevelSet()


def index_root_templates(root_templates):
    n = root_templates.num_nodes()
    n_attr = root_templates.num_attrs()
    wide = root_templates.is_wide()
    for i in range(root_templates.num_nodes()):
        name, parent, next, attrs = root_templates.node(i)
        if name == "GameObjects" and parent == 0:
            attr_index = attrs
            while attr_index != -1 and attr_index < n_attr:
                a_name, a_type, a_next, a_owner, a_value = root_templates.attr(attr_index)
                if not wide and a_owner != i:
                    break
                if a_name == "ID":
                    ROOT_TEMPLATES_BY_UUID[a_value] = (root_templates, i)
                if a_name == "Name":
                    ROOT_TEMPLATES_BY_NAME[a_value] = (root_templates, i)
                if wide:
                    attr_index = a_next
                else:
                    attr_index += 1


print(SHARED_ROOT_TEMPLATES.node(0))
print(SHARED_ROOT_TEMPLATES.is_wide())
index_root_templates(SHARED_ROOT_TEMPLATES)
index_root_templates(GUSTAV_ROOT_TEMPLATES)
index_root_templates(GUSTAVDEV_ROOT_TEMPLATES)
for name in ROOT_TEMPLATES_BY_NAME.keys():
    print(name)

index = _pybg3._IndexReader(os.path.expanduser("~/code/libbg3/tmp/bg3.idx"))
print(index.query("mysterious artefact"))
print(index.query("infernalbox"))

LEVELS.load_pak(GUSTAV)
for name in sorted(LEVELS.levels.keys()):
    print(name)
    for source in LEVELS.levels[name].sources:
        print(f"  {source.mod_name}/{name}/{source.type}/{source.file_name}.lsf")


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
