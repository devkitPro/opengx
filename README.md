OpenGX
======

OpenGX is an OpenGL-like wrapper which sits on GX subsystem. It implements the most common OpenGL calls such as immediate mode, texturing, array drawing (indexed and not indexed), matrix operations and basic lighting. It features some advanced stuff such as automatic texture compression (ARB extension) and mipmapping. Beware! Some parts are currently NOT opengl compilant (speaking strictly).

Current status
--------------

OpenGX should be able to cover the needs of most OpenGL 1.5 applications. Features are implemented on a per need basis: if a feature is not implemented (or not implemented correctly) the most likely reason is that the OpenGX developers haven't yet encountered an application that they wanted to port that needed it. So, if a missing feature is blocking your adoption of OpenGX, please file an issue; for a faster resolution, providing some code through which the feature can be tested will undoubtedly help.

For a list of implemented functions, please refer to the `src/functions.c` file.

TODO list:

  * Implement spotlights
  * Complete glGet calls

There are several limitations on the implementation of many features, mostly
due to the hardware. They are detailed in the PDF documentation, but feel free
to file an issue if unsure.


Design documentation
--------------------

Please see the `opengx.pdf` file which is [automatically generated with each
build](https://github.com/devkitPro/opengx/actions/workflows/docs.yml).


Build instructions
------------------

The project is built with [cmake](https://cmake.org/) and requires the `libogc`
package from [devkitPro](https://devkitpro.org/). Once these dependencies are
installed, build opengx as follows:

    # Use GameCube.cmake to build for the GameCube instead
    cmake -S. -Bbuild -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Wii.cmake"
    cd build
    make
    # Optional, to install it into devkitPro's portslib:
    sudo -E PATH=$PATH make install


Running OpenGX applications in Dolphin
--------------------------------------

OpenGX makes use of some GX features which require changing specific Dolphin settings from their default values:

- Graphics -> Hacks -> Texture Cache -> Accuracy: move it all towards "Safe"
- Graphics -> Hacks -> Embedded Frame Buffer (EFB) -> Uncheck "Store EFB copies to texture only"
- Graphics -> Hacks -> External Frame Buffer (XFB) -> Uncheck "Store XFB copies to texture only"
- Graphics -> Hacks -> Other -> Uncheck "Disable Bounding Box"
