from setuptools import Extension, setup
import os

if os.name != 'posix':
    raise NotImplementedError(
        "This library relies on POSIX threads to operate and is not " +
        "supported by your OS. If you would like help extend support " +
        "to other platforms, consider making a pull request to " + 
        "https://github.com/trbabb/nativethread")


native_module = Extension('nativethread',
                    define_macros = [('MAJOR_VERSION', '1'),
                                     ('MINOR_VERSION', '0')],
                    extra_compile_args = ['-pthread'],
                    sources = ['src/nativethread.c'])


setup (name = 'nativethread',
       version = '1.0',
       description = 'Run interruptible native code on non-python threads.',
       author = 'Tim Babb',
       author_email = 'trbabb@gmail.com',
       long_description = "Interruptable threads for native code.",
       ext_modules = [native_module])
