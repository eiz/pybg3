import os
import re
import time
import numpy as np
from numpy.lib import recfunctions as rfn
from dataclasses import dataclass
from pathlib import Path
from pybg3 import pak, lsf, _pybg3
from pxr import Usd, UsdGeom, Vt, Gf


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


def dump_granny(root, indent=0):
    if isinstance(root, _pybg3._GrannyPtr):
        for name in dir(root):
            child = root.__getattr__(name)
            if isinstance(child, _pybg3._GrannyDirectSpan):
                array = np.array(child, copy=False)
                print(f"{indent * ' '}{name}: {array.shape} {array}")
            else:
                print(f"{indent * ' '}{name}: {child}")
                dump_granny(child, indent + 4)
    elif isinstance(root, _pybg3._GrannyPtrSpan):
        for index, child in enumerate(root):
            print(f"{indent * ' '}[{index}]:")
            dump_granny(child, indent + 4)


class MeshConverter:
    def __init__(self):
        self._converted = {}

    def convert(self, path, name):
        if path not in self._converted:
            self._converted[path] = {}
        path_meshes = self._converted[path]
        if name in path_meshes:
            return path_meshes[name]
        path_meshes[name] = None  # only try to convert once.
        try:
            granny = _pybg3._GrannyReader.from_data(MODELS.file_data(path))
            for mesh in granny.root.Meshes:
                if mesh.Name == name:
                    path_meshes[name] = self._do_convert(path, name, mesh)
                    return path_meshes[name]
            raise ValueError(f"mesh not found: {name}")
        except Exception as e:
            print(f"failed to convert {path}: {repr(e)}")
            return None

    def _do_convert(self, path, name, mesh):
        g_vertices = np.array(mesh.PrimaryVertexData.Vertices, copy=False)
        if len(mesh.PrimaryTopology.Indices16) > 0:
            g_indices = np.array(
                mesh.PrimaryTopology.Indices16, copy=False, dtype=np.uint16
            )
        else:
            g_indices = np.array(mesh.PrimaryTopology.Indices, copy=False)
        stage_path = f"out/Meshes/{path}/{name}.usdc"
        os.makedirs(f"out/Meshes/{path}", exist_ok=True)
        u_stage = Usd.Stage.CreateNew(stage_path)
        u_mesh = UsdGeom.Mesh.Define(u_stage, "/mesh")
        u_points = u_mesh.CreatePointsAttr()
        g_positions = rfn.structured_to_unstructured(
            g_vertices[["f0", "f1", "f2"]], copy=False
        )
        vt_vertices = Vt.Vec3fArray.FromNumpy(g_positions)
        vt_indices = Vt.IntArray.FromNumpy(g_indices)
        vt_face_counts = Vt.IntArray.FromNumpy(
            np.full(g_indices.shape[0] // 3, 3, dtype=np.int32)
        )
        u_points.Set(vt_vertices)
        u_vertex_face_counts = u_mesh.CreateFaceVertexCountsAttr()
        u_vertex_face_counts.Set(vt_face_counts)
        u_vertex_indices = u_mesh.CreateFaceVertexIndicesAttr()
        u_vertex_indices.Set(vt_indices)
        u_stage.SetDefaultPrim(u_mesh.GetPrim())
        u_stage.GetRootLayer().Save()
        return stage_path


class PatchConverter:
    def __init__(self):
        self._converted = {}

    def convert(self, path):
        if path in self._converted:
            return self._converted[path]
        self._converted[path] = None
        try:
            patch = _pybg3._PatchFile.from_data(GUSTAV.file_data(path))
            self._converted[path] = self._do_convert(path, patch)
            return self._converted[path]
        except Exception as e:
            print(f"failed to convert {path}: {repr(e)}")
            return None

    def _do_convert(self, path, patch):
        usdc_path = f"out/Terrains/{path}.usdc"
        u_stage = Usd.Stage.CreateNew(usdc_path)
        height = np.array(patch.heightfield, copy=False)
        np_points = np.dstack(
            (
                np.repeat(
                    np.arange(0, height.shape[1]).reshape(1, -1), height.shape[0], axis=0
                ),
                height,
                np.repeat(
                    np.arange(0, height.shape[0]).reshape(-1, 1), height.shape[1], axis=1
                ),
            )
        ).reshape(-1, 3)
        # copilot lawl
        np_indices = np.arange(patch.local_rows * patch.local_cols).reshape(
            patch.local_rows, patch.local_cols
        )
        np_triangles = np.array(
            [
                np_indices[1:, :-1],
                np_indices[:-1, 1:],
                np_indices[:-1, :-1],
                #
                np_indices[1:, 1:],
                np_indices[:-1, 1:],
                np_indices[1:, :-1],
            ]
        )
        np_index_buffer = np_triangles.transpose(1, 2, 0).reshape(-1)
        np_face_counts = np.full(np_index_buffer.shape[0] // 3, 3, dtype=np.int32)
        u_mesh = UsdGeom.Mesh.Define(u_stage, "/terrain")
        u_points = u_mesh.CreatePointsAttr()
        u_points.Set(Vt.Vec3fArray.FromNumpy(np_points))
        u_vertex_indices = u_mesh.CreateFaceVertexIndicesAttr()
        u_vertex_indices.Set(Vt.IntArray.FromNumpy(np_index_buffer))
        u_vertex_face_counts = u_mesh.CreateFaceVertexCountsAttr()
        u_vertex_face_counts.Set(Vt.IntArray.FromNumpy(np_face_counts))
        u_stage.SetDefaultPrim(u_mesh.GetPrim())
        u_stage.GetRootLayer().Save()
        return usdc_path


class LevelConverter:
    def __init__(self, *, mesh_converter, patch_converter):
        self._mesh_converter = mesh_converter
        self._patch_converter = patch_converter
        self._converted_levels = {}
        self._visuals = ASSETS.get_or_create("Visual")
        self._materials = ASSETS.get_or_create("Material")
        pass

    def convert(self, level_name):
        if level_name in self._converted_levels:
            return self._converted_levels[level_name]
        result = self._do_convert(level_name)
        self._converted_levels[level_name] = result
        return result

    def _do_convert_terrain(self, obj, level_name, source, stage, name):
        terrain_vis = obj.component("Visual")
        if terrain_vis is None:
            return
        uuid = obj.attrs["MapKey"]
        height = terrain_vis.attrs["Height"].value
        width = terrain_vis.attrs["Width"].value
        chunks_x = width // 64
        chunks_y = height // 64
        if width % 64 != 0:
            chunks_x += 1
        if height % 64 != 0:
            chunks_y += 1
        print(f"TERRAIN {height} {width}")
        u_terrain_key = f"/Levels/{level_name}/Terrains/{name}"
        u_terrain = UsdGeom.Xform.Define(stage, u_terrain_key)
        for y in range(chunks_y):
            for x in range(chunks_x):
                patch_path = f"Mods/{source.mod_name}/Levels/{level_name}/Terrains/{uuid}_{x}_{y}.patch"
                patch_usdc_path = self._patch_converter.convert(patch_path)
                u_mesh = UsdGeom.Mesh.Define(stage, f"{u_terrain_key}/patch_{x}_{y}")
                u_mesh.GetPrim().GetReferences().AddReference(patch_usdc_path)
                translate = u_mesh.AddTranslateOp()
                translate.Set((x * 64, 0, y * 64))
        return u_terrain

    def _convert_visual_lod0(self, visual):
        source_file = visual.node.attrs["SourceFile"]
        for node in visual.node.children:
            if node.name == "Objects" and node.attrs["LOD"].value == 0:
                material_id = node.attrs["MaterialID"]
                material = self._materials.by_uuid.get(material_id)
                if material is None:
                    print(f"missing material: {material_id}")
                else:
                    print(
                        f"material: {material.node.attrs['SourceFile'].value} "
                        f"type {material.node.attrs['MaterialType'].value}"
                    )
                object_id = node.attrs["ObjectID"].split(".")[1]
                return self._mesh_converter.convert(source_file.value, object_id)
        print(f"no LOD0 for {source_file.value}")

    def _do_convert_object(self, obj, level_name, source, stage, used_names):
        visual_template = obj.attrs.get("VisualTemplate")
        root_template = ROOT_TEMPLATES.by_uuid.get(obj.attrs["TemplateName"])
        key = obj.attrs["MapKey"]
        objtype = obj.attrs["Type"]
        name = (
            obj.attrs["Name"]
            .value.strip()
            .replace("-", "_")
            .replace(" ", "_")
            .replace("[", "")
            .replace("]", "")
            .replace("(", "")
            .replace(")", "")
            .replace(".", "")
            .replace("'", "")
            .replace(",", "_")
            .replace("?", "")
            .replace("\\", "_")
        )
        name = "_" + name
        if name in used_names:
            name = name + "_" + to_pxr_uuid(key)
        used_names.add(name)
        is_placeholder = False
        u_obj = None
        u_obj_key = f"/Levels/{level_name}/{source.type}/{name}"
        if objtype == "terrain":
            u_obj = self._do_convert_terrain(obj, level_name, source, stage, name)
        else:
            mesh_usd_path = None
            visual = None
            if visual_template is None:
                visual_template = root_template.inherited_attribute("VisualTemplate")
            if visual_template is not None and len(visual_template) > 0:
                visual = self._visuals.by_uuid.get(visual_template)
                if visual is None:
                    pass
                    # Weird: there seem to be a bunch of scenery objects with
                    # VisualTemplates which are from an EffectBank, not a
                    # VisualBank.
                    # _pybg3.log(f"missing visual: {name}")
                    # pprint.pp(root_template.node)
                    # pprint.pp(index.query(visual_template))
                else:
                    if "SourceFile" in visual.node.attrs:
                        mesh_usd_path = self._convert_visual_lod0(visual)
            if mesh_usd_path is not None:
                u_obj = UsdGeom.Mesh.Define(stage, f"{u_obj_key}/mesh")
                u_obj.GetPrim().GetReferences().AddReference(mesh_usd_path)
            else:
                u_obj = UsdGeom.Sphere.Define(stage, f"{u_obj_key}/missing")
                is_placeholder = True
                # obj.CreateRadiusAttr().Set(0.001)
        transform = obj.component("Transform")
        if transform is not None:
            translate = u_obj.AddTranslateOp()
            a_translate = transform.attrs["Position"]
            translate.Set((a_translate.x, a_translate.y, a_translate.z))
            orient = u_obj.AddOrientOp()
            a_orient = transform.attrs["RotationQuat"]
            orient.Set(Gf.Quatf(a_orient.w, a_orient.x, a_orient.y, a_orient.z))
            if not is_placeholder:
                scale = u_obj.AddScaleOp()
                a_scale = transform.attrs["Scale"]
                scale.Set(Gf.Vec3f(a_scale.value, a_scale.value, a_scale.value))

    def _do_convert(self, level_name):
        level = LEVELS[level_name]
        os.makedirs(f"out/Levels/{level_name}", exist_ok=True)
        stage_path = f"out/Levels/{level_name}/_merged.usda"
        stage = Usd.Stage.CreateNew(stage_path)
        total_xforms = 0
        used_names = set()
        for source in level.sources:
            templates, _ = lsf.Node.parse_node(source.lsf, 0)
            for obj in templates.children:
                total_xforms += 1
                self._do_convert_object(obj, level_name, source, stage, used_names)
        level_prim = UsdGeom.Xform.Define(stage, f"/Levels/{level_name}")
        level_prim.AddScaleOp().Set(Gf.Vec3f(1.0, 1.0, -1.0))
        stage.GetRootLayer().Save()
        print(f"Total xforms: {total_xforms}")
        return stage_path


def process_nautiloid():
    mesh_converter = MeshConverter()
    patch_converter = PatchConverter()
    level_converter = LevelConverter(
        mesh_converter=mesh_converter, patch_converter=patch_converter
    )
    return level_converter.convert("WLD_Crashsite_D")


# checktime("nautiloid", process_nautiloid)

gts = _pybg3._GtsReader.from_path(
    "tmp/Generated/Public/VirtualTextures/Albedo_Normal_Physical_0.gts"
)
gts.dump()
