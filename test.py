import os
import pprint
import re
import time
from dataclasses import dataclass
from pathlib import Path
from pybg3 import pak, lsf, _pybg3
from pxr import Usd, UsdGeom


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

    def __getitem__(self, name):
        return self.levels[name]

    def __contains__(self, name):
        return name in self.levels

    def __iter__(self):
        return iter(self.levels)

    def __len__(self):
        return len(self.levels)

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


class Asset:
    def __init__(self, asset_banks, node_idx):
        self._asset_banks = asset_banks
        self._node_idx = node_idx
        self._node = None

    @property
    def node(self):
        if self._node is None:
            self._node, _ = lsf.Node.parse_node(self._asset_banks, self._node_idx)
        return self._node


class AssetSet:
    def __init__(self):
        self.by_uuid = {}
        self.by_name = {}


class AssetTypeSet:
    def __init__(self):
        self._types = {}

    def _index_asset_banks(self, asset_banks):
        asset_banks.ensure_sibling_pointers()
        n = asset_banks.num_nodes()
        if n == 0:
            return
        node_idx = 0
        while node_idx != -1:
            name, _, next, _ = asset_banks.node(node_idx)
            asset_type = name.replace("Bank", "")
            if node_idx + 1 < n:
                _, next_parent, _, _ = asset_banks.node(node_idx + 1)
                if next_parent == node_idx:
                    self._index_bank_native(
                        self.get_or_create(asset_type), asset_banks, node_idx + 1
                    )
            node_idx = next

    def _index_bank_native(self, asset_set, asset_banks, node_idx):
        by_uuid = asset_set.by_uuid
        by_name = asset_set.by_name

        def handle(idx, name, uuid):
            asset = Asset(asset_banks, idx)
            by_uuid[uuid] = asset
            by_name[name] = asset

        asset_banks.scan_unique_objects(handle, "Name", "ID", node_idx)

    def get_or_create(self, name):
        if name not in self._types:
            self._types[name] = AssetSet()
        return self._types[name]

    def load_pak(self, pak):
        for name in pak.files():
            if "[PAK]" in name and name.endswith(".lsf"):
                asset_banks = lsf.loads(pak.file_data(name))
                self._index_asset_banks(asset_banks)


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

    def inherited_attribute(self, name):
        cur = self
        while True:
            val = cur.node.attrs.get(name)
            if val is not None:
                return val
            parent_id = cur.node.attrs.get("ParentTemplateId")
            if parent_id is None:
                break
            cur = ROOT_TEMPLATES.by_uuid[parent_id]


class RootTemplateSet:
    def __init__(self):
        self.by_name = {}
        self.by_uuid = {}

    def _index_root_templates(self, root_templates):
        by_uuid = self.by_uuid
        by_name = self.by_name

        def handle(idx, name_value, uuid_value):
            template = RootTemplate(uuid_value, name_value, root_templates, idx)
            by_uuid[uuid_value] = template
            by_name[name_value] = template

        root_templates.scan_unique_objects(handle, "Name", "MapKey", 1)

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


def index_all_root_templates():
    checktime("shared root templates", lambda: ROOT_TEMPLATES.load_mod(SHARED, "Shared"))
    checktime(
        "shareddev root templates", lambda: ROOT_TEMPLATES.load_mod(SHARED, "SharedDev")
    )
    checktime("gustav root templates", lambda: ROOT_TEMPLATES.load_mod(GUSTAV, "Gustav"))
    checktime(
        "gustavdev root templates", lambda: ROOT_TEMPLATES.load_mod(GUSTAV, "GustavDev")
    )


BG3_ROOT = Path(os.environ.get("BG3_DATA", os.path.expanduser("~/l/bg3/Data")))
GUSTAV = checktime("Gustav.pak", lambda: pak.PakFile(BG3_ROOT / "Gustav.pak"))
SHARED = checktime("Shared.pak", lambda: pak.PakFile(BG3_ROOT / "Shared.pak"))
ENGINE = checktime("Engine.pak", lambda: pak.PakFile(BG3_ROOT / "Engine.pak"))
MODELS = checktime("Models.pak", lambda: pak.PakFile(BG3_ROOT / "Models.pak"))
ROOT_TEMPLATES = RootTemplateSet()
ASSETS = AssetTypeSet()
BANKS = {}
LEVEL_OBJECT_FILE_RE = re.compile(
    r"Mods/(?P<mod_name>[^/]+)/Levels/(?P<level_name>[^/]+)/(?P<type>LevelTemplates|Characters|Decals|Items|FogVolumes|TileConstructions|Terrains|Triggers|Lights|LightProbes|CombinedLights|Scenery|Splines)/(?P<file_name>[^/]+).lsf"
)
LEVELS = LevelSet()
checktime("engine assets", lambda: ASSETS.load_pak(ENGINE))
checktime("shared assets", lambda: ASSETS.load_pak(SHARED))
checktime("gustav assets", lambda: ASSETS.load_pak(GUSTAV))
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
for asset_type, asset_set in ASSETS._types.items():
    print(f"{asset_type}: {len(asset_set.by_uuid)} ({len(asset_set.by_name)} names)")


def to_pxr_uuid(uuid):
    return f"U{uuid.replace('-', '_')}"


def test_granny(path):
    # ya, we def need a vfs here lol. tew dew
    try:
        data = MODELS.file_data(path)
        granny = _pybg3._GrannyReader.from_data(data)
        root = granny.root
        print(f"{root.ArtToolInfo.FromArtToolName}: {root.FromFileName}")
        # _pybg3.log(f"parsed {path}")
    except Exception as e:
        _pybg3.log(f"failed to load {path}: {repr(e)}")


def process_nautiloid():
    level_name = "Cre_GithCreche_D"
    level = LEVELS[level_name]
    os.makedirs(f"out/Levels/{level_name}", exist_ok=True)
    stage = Usd.Stage.CreateNew(f"out/Levels/{level_name}/_merged.usda")
    total_xforms = 0
    visuals = ASSETS.get_or_create("Visual")
    for source in level.sources:
        templates, _ = lsf.Node.parse_node(source.lsf, 0)
        for obj in templates.children:
            visual_template = obj.attrs.get("VisualTemplate")
            root_template = ROOT_TEMPLATES.by_uuid.get(obj.attrs["TemplateName"])
            if visual_template is None:
                visual_template = root_template.inherited_attribute("VisualTemplate")
            if visual_template is not None and len(visual_template) > 0:
                visual = visuals.by_uuid.get(visual_template)
                if visual is None:
                    # Weird: there seem to be a bunch of scenery objects with
                    # VisualTemplates which are from an EffectBank, not a
                    # VisualBank.
                    _pybg3.log(f"missing visual: {visual_template}")
                    # pprint.pp(root_template.node)
                    # pprint.pp(index.query(visual_template))
                else:
                    source_file = visual.node.attrs.get("SourceFile")
                    # _pybg3.log(f"visual: {visual_template} ({source_file})")
                    if source_file is not None:
                        test_granny(source_file.value)
            key = obj.attrs["MapKey"]
            transform = obj.component("Transform")
            if transform is not None:
                total_xforms += 1
                xform_key = f"/Levels/{level_name}/{source.type}/{to_pxr_uuid(key)}"
                xform = UsdGeom.Xform.Define(stage, xform_key)
                sphere = UsdGeom.Sphere.Define(stage, f"{xform_key}/placeholder")
                translate = xform.AddTranslateOp()
                a_translate = transform.attrs["Position"]
                translate.Set((a_translate.x, a_translate.y, a_translate.z))
    stage.GetRootLayer().Save()
    print(f"Total xforms: {total_xforms}")


checktime("nautiloid", process_nautiloid)
