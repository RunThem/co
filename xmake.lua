--- Project
set_project('co')

--- Version
set_version('0.0.1')

--- Xmake configure
set_xmakever('2.6.1')

--- Build mode
add_rules('mode.debug', 'mode.valgrind', 'mode.release')

--- Macro definition
add_defines('_GNU_SOURCE=1')

--- No warning, no error
set_warnings('all', 'error')

--- Language standard
set_languages('clatest', 'cxxlatest')

--- Unused variables and functions
add_cflags(
  '-Wno-unused-label',
  '-Wno-unused-function',
  '-Wno-unused-variable',
  '-Wno-unused-but-set-variable',
  '-Wno-address-of-packed-member'
)

--- Use reserved identifier
add_cflags('-Wno-reserved-macro-identifier', '-Wno-reserved-identifier')

--- Disable VLA extensons
add_cflags('-Werror=vla')

--- Toolchain
set_toolchains('clang')

--- Task(lsp) generate the project file
task('lsp', function()
  set_category('plugin')

  set_menu({
    usage = 'xmake lsp',
    description = 'Generate the project file.',
  })

  on_run(function()
    os.exec('xmake project -k cmake build')
    os.exec('xmake project -k compile_commands build')
  end)
end)

--- Lambda expressions
local lambda = false
if lambda then
  add_cflags('-fblocks')
  if is_plat('linux') then
    add_ldflags('-lBlocksRuntime')
    add_defines('__linux__=1')
  end
end

--- Private repositories
add_repositories('RunThem https://github.com/RunThem/My-xmake-repo')

--- Project common header file path
add_includedirs('$(projectdir)/src')

--- Third party library
add_requires('libsock')

--- main target
target('co', function()
  set_kind('static')
  add_files('src/co.c')
  add_headerfiles('src/*.h')

  add_packages('libsock')
end)

target('demo', function()
  set_kind('binary')
  add_files('src/main.c')

  add_deps('co')

  add_packages('libsock')
end)
