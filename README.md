# Stromboli
Stromboli is a C Vulkan library. It is based on the [grounded](https://github.com/Pilzschaf/grounded) platform layer. 

## External dependencies
All external dependencies apart from premake5 are shipped as submodules in the *libs* subdirectory. Make sure you have initialized the submodules with the command
```bash
git submodule update --init
```
### External dependencies used by Stromboli
- [grounded](https://github.com/Pilzschaf/grounded)
- [volk](https://github.com/zeux/volk)

## Building
To build the example applications you require premake5. Then you can create the build files for your operating system with premake5
### Linux with Make
```bash
premake5 gmake2
make -j16
```
### Windows with Visual Studio 2022
```bat
premake5 vs2022
```
Then simply open the generated solution file with Visual Studio