"""Run with python3 test/test_item_downloadability.py (requires a host C++ compiler).

Pins PluginCatalogActivity::itemDownloadable() against the manifest shapes the
row marking depends on: only a catalog that declares download.require_url treats
an empty mapped url field as "no file for this row" (the YBX update notice, whose
download template resolves from {id}). Every other catalog keeps its action.
"""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/activities/plugins/PluginCatalogActivity.cpp").read_text()

marker = "bool PluginCatalogActivity::itemDownloadable(const Item& item) const {"
method = marker + source.split(marker, 1)[1].split("\nvoid PluginCatalogActivity::downloadItem", 1)[0]

harness = r"""
#include <cassert>
#include <string>
#include <vector>

struct Item {
  std::string url;
  std::vector<std::string> files;
};

struct Manifest {
  std::string bundleFilesPath;  // set: manifest.isBundle()
  std::string dlUrlPath;        // set: an API hop resolves the file URL
  std::string urlPath;          // the mapped browse field the row's url comes from
  bool dlRequireUrl = false;
  bool isBundle() const { return !bundleFilesPath.empty(); }
};

struct PluginCatalogActivity {
  Manifest manifest;
  bool itemDownloadable(const Item& item) const;
};
""" + method + r"""
int main() {
  PluginCatalogActivity activity;

  // The YBX catalog: "url": "download_url" mapped, require_url declared, and a
  // download built from {id}. The notice row carries no file; a book row does.
  activity.manifest = {"", "", "download_url", true};
  assert(!activity.itemDownloadable(Item{"", {}}));
  assert(activity.itemDownloadable(Item{"https://host/file.epub", {}}));

  // The same shape without require_url (a third-party catalog whose rows carry
  // no url because {id} resolves the file) keeps its download action.
  activity.manifest.dlRequireUrl = false;
  assert(activity.itemDownloadable(Item{"", {}}));

  // No mapped url field: the row cannot be missing one.
  activity.manifest = {"", "", "", true};
  assert(activity.itemDownloadable(Item{"", {}}));

  // An API hop resolves the file URL, so the row's own field never decides.
  activity.manifest = {"", "url", "download_url", true};
  assert(activity.itemDownloadable(Item{"", {}}));

  // A bundle item installs from its file list; one with neither files nor a url
  // has no source in either mode.
  activity.manifest = {"files", "", "download_url", true};
  assert(activity.itemDownloadable(Item{"", {"manifest.json", "plugin.js"}}));
  assert(activity.itemDownloadable(Item{"https://host/file.epub", {}}));
  assert(!activity.itemDownloadable(Item{"", {}}));
}
"""

with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "downloadability.cpp"
    executable = Path(directory) / "downloadability"
    cpp.write_text(harness)
    subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

print("Item downloadability checks passed")
