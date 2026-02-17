import os
import sys
import subprocess
import shutil
from pathlib import Path
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def build_extension(self, ext):
        if not isinstance(ext, CMakeExtension):
            super().build_extension(ext)
            return

        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))

        if not extdir.endswith(os.path.sep):
            extdir += os.path.sep

        if "CMAKE_BUILD_PARALLEL_LEVEL" not in os.environ:
            import multiprocessing

            os.environ["CMAKE_BUILD_PARALLEL_LEVEL"] = str(multiprocessing.cpu_count())

        import sysconfig as _sysconfig

        python_include = _sysconfig.get_path("include")

        cmake_args = [
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extdir}",
            f"-DPython_EXECUTABLE={sys.executable}",
            f"-DPython_ROOT_DIR={sys.prefix}",
            f"-DPython_INCLUDE_DIR={python_include}",
            f"-DPython_FIND_STRATEGY=LOCATION",
            f"-DCMAKE_BUILD_TYPE={'Debug' if self.debug else 'Release'}",
            f"-DSAENGRA_STATIC_DEPS={'ON' if os.environ.get('SAENGRA_STATIC_DEPS') else 'OFF'}",
        ]

        build_args = []

        if sys.platform.startswith("darwin"):
            homebrew_prefix = self.get_homebrew_prefix()
            if homebrew_prefix:
                cmake_args.extend(
                    [
                        f"-DCMAKE_PREFIX_PATH={homebrew_prefix}",
                    ]
                )

            if "MACOSX_DEPLOYMENT_TARGET" not in os.environ:
                cmake_args.append("-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15")

        if not self.debug:
            cmake_args.append("-DCMAKE_BUILD_TYPE=Release")

        build_temp = Path(self.build_temp) / ext.name
        if not build_temp.exists():
            build_temp.mkdir(parents=True)

        subprocess.check_call(["cmake", ext.sourcedir] + cmake_args, cwd=build_temp)

        subprocess.check_call(["cmake", "--build", "."] + build_args, cwd=build_temp)

        import sysconfig

        ext_suffix = sysconfig.get_config_var("EXT_SUFFIX")
        if ext_suffix:
            lib_dir = Path(extdir)
            if lib_dir.exists():
                source_lib = lib_dir / f"{ext.name.split('.')[-1]}.so"
                if not source_lib.exists():
                    source_lib = lib_dir / ext.name.split(".")[-1]

                if source_lib.exists():
                    target_lib = lib_dir / f"{ext.name.split('.')[-1]}{ext_suffix}"
                    if source_lib != target_lib:
                        shutil.copy2(source_lib, target_lib)

    @staticmethod
    def get_homebrew_prefix():
        try:
            result = subprocess.run(
                ["brew", "--prefix"], capture_output=True, text=True, check=True
            )
            return result.stdout.strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            return None


setup(
    ext_modules=[CMakeExtension("saengra.c_extension")],
    cmdclass={"build_ext": CMakeBuild},
    zip_safe=False,
)
