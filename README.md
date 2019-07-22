# MagicTXD_Automator

Edited version of MagicTXD revision26 cloned 7/22/2019, for bulk actions 

## Planned:
* Batch format edit for all files in archive (ex: resize operation with percentage and definable exclusion exceptions for every texture within a TXD)
* Multiple TXD contraint bulk edit (ex: replace fileA with file_name with fileB for all .TXD selected)

## How to build
0. Install Visual Studio 2019 Community (or other ver) with "Desktop development with C++" enabled and under Individual components enable C++ CMake tools for Windows
1. Open `vendor\Qt5.12` folder and edit `_userconf.bat`
* change `_TMPOUTPATH` and `_TMPOUTPATH_GIT` to a drive letter you have mapped for the HDD/SSD path
2. Install [Strawberry Perl](perl.org) and ensure perl is added to PATH
3. Run bldscript_vs2019_x64.bat (replace x64 with x86 if you want 32-bit for all succeeding steps)
* Say `y` to the license agreement, this is the only prompt that will occur for this script
4. Buildscript takes a long time, once complete `qt512_static_x64_vs2019` will be in the folder you ran the script. Open `qt512_static_x64_vs2019\lib` and copy all files within `\lib` to `vendor\Qt5.12\lib\vs2019\x64`
5. You also need to copy the `plugins` folder to `vendor\Qt5.12\lib\vs2019\x64`. Your final folder should have two subfolders: `cmake` and `plugins`, with the qt lib files sitting directly in `\x64`
6. Open `build/Magic.sln` with Visual Studio
7. Specify build as x64 Debug within Visual Studio
8. Copy/Move `resources` and `languages` folder to `output/` for proper program appearance while running