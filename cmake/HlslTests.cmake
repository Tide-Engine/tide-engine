# What the tests check the HLSL tidec writes for Unity (compiler/src/hlsl.c)
# with: the Windows SDK's fxc, which compiles each program's vertex and
# fragment stages for Direct3D 11 as Unity's compiler does there. Where
# there's none, the tests don't compile the HLSL.
if(WIN32)
    set(_tide_kits "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin")
    file(GLOB _tide_fxc_found "${_tide_kits}/10.*/x64/fxc.exe")
    list(SORT _tide_fxc_found COMPARE NATURAL ORDER DESCENDING)
    set(_tide_fxc_hint "")
    if(_tide_fxc_found)
        list(GET _tide_fxc_found 0 _tide_fxc_newest)
        get_filename_component(_tide_fxc_hint "${_tide_fxc_newest}" DIRECTORY)
    endif()
    find_program(TIDE_FXC fxc HINTS "${_tide_fxc_hint}")
endif()
