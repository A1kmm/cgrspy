from setuptools import setup
from distutils.core import Extension
from distutils.cmd import Command
import distutils.command.build
import sys
import os
from os.path import join

ppath = lambda *p: p and join("..", *p) or ".."
include_dirs = [ppath(i) for i in ["interfaces", "", "sources", "CGRS"]]
library_dirs = [".."]


class test_cgrspy(distutils.command.build.build):
    def run(self):
        sys.path.insert(0, self.build_lib)
        from tests import TestMain
        TestMain.runTests()
    user_options = []


setup(name="cgrspy",
      version="1.1",
      description="Python interface to the CellML Generics and Reflection "
                  "Service",
      author="Andrew Miller",
      author_email="ak.miller@auckland.ac.nz",
      url="http://cellml-api.sf.net/",
      packages=['cgrspy'],
      cmdclass={
          'test': test_cgrspy
      },
      ext_modules=[
          Extension(
              name="cgrspy.bootstrap", 
              sources=[join("cgrspy", "cgrspy_bootstrap.cpp")],
              include_dirs=include_dirs,
              library_dirs=library_dirs,
              libraries=["cellml", "cgrs"])
          ]
      )
