import sexpdata
import os
from pathlib import Path
from pybg3 import pak, lsf

BG3_ROOT = Path(os.environ.get("BG3_DATA", os.path.expanduser("~/l/bg3/Data")))
GUSTAV = pak.PakFile(BG3_ROOT / "Gustav.pak")
SHARED = pak.PakFile(BG3_ROOT / "Shared.pak")
SHARED_ROOT_TEMPLATES = lsf.loads(SHARED.file_data("Public/Shared/RootTemplates/_merged.lsf"))
GUSTAV_ROOT_TEMPLATES = lsf.loads(GUSTAV.file_data("Public/Gustav/RootTemplates/_merged.lsf"))
GUSTAVDEV_ROOT_TEMPLATES = lsf.loads(GUSTAV.file_data("Public/GustavDev/RootTemplates/_merged.lsf"))
BANKS = {}

def load_asset_banks(pak):
  for name in pak.files():
    if "[PAK]" in name and name.endswith(".lsf"):
      BANKS[name] = lsf.loads(pak.file_data(name))

load_asset_banks(SHARED)
load_asset_banks(GUSTAV)

ROOT_TEMPLATES_BY_UUID = {}
ROOT_TEMPLATES_BY_NAME = {}

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
