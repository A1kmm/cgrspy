from distutils.core import setup, Extension
from distutils.cmd import Command
import distutils.command.build
import sys
import os

class test_cgrspy(distutils.command.build.build):
    def run(self):
        sys.path.insert(0, self.build_lib)
        from tests import TestMain
        TestMain.runTests()
    user_options = []

setup(name="cgrspy",
      version="1.1pre",
      description="Python interface to the CellML Generics and Reflection Service",
      author="Andrew Miller",
      author_email="ak.miller@auckland.ac.nz",
      url="http://cellml-api.sf.net/",
      packages=['cgrspy'],
      cmdclass = {'test': test_cgrspy},
      ext_modules=[
        Extension("cgrspy.bootstrap", ["cgrspy/cgrspy_bootstrap.cpp"],
          include_dirs=["../interfaces", "..", "../sources", "../CGRS"],
          library_dirs=[".."],
          libraries=["cellml", "cgrs"])
        ]
      )
