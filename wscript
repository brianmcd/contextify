import Options
import os
import sys
import subprocess
import platform
from distutils.version import LooseVersion

VERSION = '0.1.1'

def node_arch():
  if sys.platform != 'darwin':
      return
  cmd = [ 'node', '-e', 'console.log(process.arch)' ]
  p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  out = p.communicate()[0].split('\n')[0]
  return out

def set_options(opt):
  opt.tool_options("compiler_cxx")

def configure(conf):
  conf.check_tool("compiler_cxx")
  conf.check_tool("node_addon")
  conf.env.set_variant("Release")
  flags = ['-O3']
  if platform.system() == 'Darwin' and LooseVersion("10.7") > LooseVersion(platform.mac_ver()[0]):
    arch = node_arch()
    arch_mappings = {'ia32': 'i386', 'x64': 'x86_64'}
    if arch in arch_mappings:
      arch = arch_mappings[arch]
      flags += ['-arch', arch]
    conf.env.append_value('CCFLAGS', flags)
    conf.env.append_value('CXXFLAGS', flags)
    conf.env.append_value('LINKFLAGS', flags)

def build(bld):
  obj = bld.new_task_gen("cxx", "shlib", "node_addon")
  obj.target = "contextify"
  obj.source = "src/contextify.cc"
