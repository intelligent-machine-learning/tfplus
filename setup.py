# Copyright 2022 The TF-plus Authors. All Rights Reserved.
"""Setup for pip package."""
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import fnmatch
import os
import shutil
from distutils.util import convert_path

import sys

from setuptools import find_packages
from setuptools import setup
from setuptools.dist import Distribution

USING_SO_FILES = (
    "libtfplus.so",
    "libtfplus_opdef.so",
    "_antfin_ops.so",
    "_dataset_ops.so",
    "_decode_ops.so",
    "libzdfs.so",
    "_dfs_ops.so",
    "_feature_column_ext_ops.so",
    "_kv_variable_ops.so",
    "_oss_ops.so",
    "_pangu_ops.so",
    "_zero_out_ops.so",
    "_string_to_number_ext_ops.so",
    "_grappler.so"
)

main_ns = {}
ver_path = convert_path('tfplus/version.py')
with open(ver_path) as ver_file:
  exec(ver_file.read(), main_ns)  # pylint: disable=exec-used
version = main_ns['__version__']

if "ACI_VAR_VERSION" in os.environ:
  print("Use version from environ")
  version = os.environ["ACI_VAR_VERSION"]

REQUIRED_PACKAGES = []
generated_files = []

project_name = "tfplus"

project = 'tfplus'
if '--package-version' in sys.argv:
  print(package)
  sys.exit(0)

if '--nightly' in sys.argv:
  nightly_idx = sys.argv.index('--nightly')
  version = version + ".dev" + sys.argv[nightly_idx + 1]
  project = 'tfplus-nightly'
  sys.argv.remove('--nightly')
  sys.argv.pop(nightly_idx)

if '--paitf' in sys.argv:
  idx = sys.argv.index('--paitf')
  project = 'tfplus-paitf'
  sys.argv.remove('--paitf')


class BinaryDistribution(Distribution):
  """This class is needed in order to create OS specific wheels."""
  def has_ext_modules(self):
    """has_ext_modules"""
    return True


datapath = None
if '--data' in sys.argv:
  data_idx = sys.argv.index('--data')
  datapath = sys.argv[data_idx + 1]
  sys.argv.remove('--data')
  sys.argv.pop(data_idx)
else:
  datapath = os.environ.get('TFPLUS_DATAPATH')

datapath = datapath or "bazel-bin"

paitf_so_path = os.environ.get('PAITF_SO_PATH', None)
paitf_so_prefix = os.environ.get('PAITF_SO_PREFIX', None)
xdl_so_path = os.environ.get('XDL_SO_PATH', None)
xdl_so_prefix = os.environ.get('XDL_SO_PREFIX', None)
eflops_so_path = os.environ.get('EFLOPS_SO_PATH', None)
eflops_so_prefix = os.environ.get('EFLOPS_SO_PREFIX', None)

package_data = {}

if datapath is not None:
  for rootname, _, filenames in os.walk(os.path.join(datapath, "tfplus")):
    if (not fnmatch.fnmatch(rootname, "*test*")
        and not fnmatch.fnmatch(rootname, "*runfiles*")):
      for filename in fnmatch.filter(filenames, "*.so"):
        if filename not in USING_SO_FILES:
          print("The tfplus doesn't use {}. Skipped.".format(filename))
          continue
        src = os.path.join(rootname, filename)
        dst = src[len(datapath) + 1:]
        print("Copy src: ", src, " dst: ", dst)
        if paitf_so_path:
          pai_dst = dst + ".pai"
          pai_src = os.path.join(paitf_so_prefix, pai_dst)
          shutil.copyfile(pai_src, pai_dst)
          print("Copy paitf so src: ", pai_src, " dst: ", pai_dst)
        if xdl_so_path:
          xdl_dst = dst + ".xdl"
          xdl_src = os.path.join(xdl_so_prefix, xdl_dst)
          shutil.copyfile(xdl_src, xdl_dst)
          print("Copy xdl so src: ", xdl_src, " dst: ", xdl_dst)
        if eflops_so_path:
          eflops_dst = dst + ".eflops"
          eflops_src = os.path.join(eflops_so_prefix, eflops_dst)
          shutil.copyfile(eflops_src, eflops_dst)
          print("Copy eflops so src: ", eflops_src, " dst: ", eflops_dst)
        shutil.copyfile(src, dst)
        generated_files.append(dst)
        package_data.setdefault("tfplus", [])
        package_data["tfplus"].append(dst[len("tfplus") + 1:]+"*")

genfile_path = "bazel-genfiles"
for rootname, _, filenames in os.walk(os.path.join(genfile_path, "tfplus")):
  if (not fnmatch.fnmatch(rootname, "*test*")
      and not fnmatch.fnmatch(rootname, "*runfiles*")):
    for filename in fnmatch.filter(filenames, "*.py"):
      src = os.path.join(rootname, filename)
      dst = src[len(genfile_path) + 1:]
      key = "tfplus"
      shutil.copyfile(src, dst)
      generated_files.append(dst)
      package_data.setdefault(key, [])
      package_data[key].append(dst[len(key) + 1:])

# package specific files
libdfs_rel_path = os.path.join(
    "dfs", "python", "ops", "libzdfs.so"
)
libdfs_dst = os.path.join("tfplus", libdfs_rel_path)
libdfs_src = "bazel-bin/external/com_antfin_libzdfs/build/libzdfs.so"
if os.path.exists(libdfs_dst):
  os.remove(libdfs_dst)
shutil.copyfile(libdfs_src, libdfs_dst)
print("Copy src: ", libdfs_src, " dst: ", libdfs_dst)
if paitf_so_path:
  pai_dst = libdfs_dst + ".pai"
  pai_src = os.path.join(paitf_so_prefix, pai_dst)
  shutil.copyfile(pai_src, pai_dst)
  print("Copy paitf so src: ", pai_src, " dst: ", pai_dst)
if xdl_so_path:
  xdl_dst = libdfs_dst + ".xdl"
  xdl_src = os.path.join(xdl_so_prefix, xdl_dst)
  shutil.copyfile(xdl_src, xdl_dst)
  print("Copy xdl so src: ", xdl_src, " dst: ", xdl_dst)

package_data["tfplus"].append(libdfs_rel_path+"*")
penrose_io = "antfin/penrose-1.0-SNAPSHOT-jar-with-dependencies.jar"
package_data["tfplus"].append(penrose_io)
package_data["tfplus"] = list(set(package_data["tfplus"]))
print("PACKAGE_DATA: ", package_data)


def install():
  setup(
      name=project_name,
      version=version,
      description=('TFPlus'),
      author='Ant Financial Inc.',
      author_email='xxx@antfin.com',
      # Contained modules and scripts.
      packages=find_packages(),
      install_requires=REQUIRED_PACKAGES,
      zip_safe=False,
      distclass=BinaryDistribution,
      package_data=package_data,
      # PyPI package information.
      classifiers=[
          'Development Status :: 4 - Beta', 'Intended Audience :: Developers',
          'Intended Audience :: Education',
          'Intended Audience :: Science/Research',
          'License :: OSI Approved :: Apache Software License',
          'Programming Language :: Python :: 2.7',
          'Programming Language :: Python :: 3.4',
          'Programming Language :: Python :: 3.5',
          'Programming Language :: Python :: 3.6',
          'Topic :: Scientific/Engineering :: Mathematics',
          'Topic :: Software Development :: Libraries :: Python Modules',
          'Topic :: Software Development :: Libraries'
      ],
      license='TBD',
      keywords='tfplus')


try:
  install()
finally:
  print(generated_files)
  for garbage in set(generated_files):
    os.remove(garbage)
