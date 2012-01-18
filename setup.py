from distutils.core import setup, Extension

setup(name="cgrspy",
      version="1.0",
      description="Python interface to the CellML Generics and Reflection Service",
      author="Andrew Miller",
      author_email="ak.miller@auckland.ac.nz",
      url="http://cellml-api.sf.net/",
      packages=['cgrspy'],
      ext_modules=[
        Extension("cgrspy.bootstrap", ["cgrspy/cgrspy_bootstrap.cpp"],
          include_dirs=["../cellml-api/interfaces", "../cellml-api", "../cellml-api/sources", "../cellml-api/CGRS"],
          library_dirs=["../cellml-api"],
          libraries=["cellml", "cgrs"])
        ]
      )
