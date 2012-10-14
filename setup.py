try:
    # Only do this with the development
    from ez_setup import use_setuptools
    use_setuptools()
except ImportError:
    # ez_setup unneeded when packaged as an egg.
    pass

from setuptools import setup, find_packages
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
        from cgrspy.tests import test_main
        test_main.runTests()
    user_options = []


setup(name="cgrspy",
      version="1.1.1",
      description="Python interface to the CellML Generics and Reflection "
                  "Service",
      long_description=open("README.rst").read(),
      author="Andrew Miller",
      author_email="ak.miller@auckland.ac.nz",
      url="http://cellml-api.sf.net/",
      license='GPL/LGPL/MPL',
      packages=find_packages(exclude=['ez_setup']),
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
