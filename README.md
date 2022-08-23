# CPPQuake
A Linux port of Quake using SDL2 and C++20.
####Requirements:
* `libfmt`
* `SDL2`
* `A compiler that supports most c++20 features (e.g. gcc 10.2 and up)`

### Before building
1. Copy `progs.dat` found in the `game` directory to `<your quake directory>/id1`

2. Since filenames are not case-insensitive in Linux, you might need to convert every quake file and directory to lowercase.

3. Specify your Quake directory in `CMakeLists.txt`;
The default directory to where the executable will be moved after the build, is `~/quake`.
### Building
Just run `cmake . && make`.
