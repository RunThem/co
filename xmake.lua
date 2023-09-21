--- Project name
set_project('co')

--- Project version
set_version('0.0.1')

--- xmake configure
set_xmakever('2.6.1')

--- Build mode
add_rules('mode.debug', 'mode.valgrind', 'mode.release')

--- Macro definition
add_defines('_GNU_SOURCE=1')

--- No warning
set_warnings('all', 'error')

--- Language standard
set_languages('gnu17')

--- Unused variables and functions
add_cflags('-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-but-set-variable')

--- DWARF v4
add_cflags('-gdwarf-4')

--- Toolchain
add_toolchains('clang')

--- Task(lsp) generate the project file
task('lsp', function()
  set_category('plugin')

  on_run(function()
    os.exec('xmake project -k cmake build/lsp')
    os.exec('xmake project -k compile_commands build/lsp')
  end)

  set_menu({
    usage = 'xmake lsp',
    description = 'Generate the project file.',
  })
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
add_requires('mimalloc')

--- Project common header file path
add_includedirs('$(projectdir)/src')

--- main target
target('co', function()
  set_kind('static')
  add_files('src/co.c')
  add_headerfiles('src/co.h')

  add_packages('mimalloc')
end)

target('demo', function()
  set_kind('binary')
  add_files('src/main.c')

  add_deps('co')
end)
