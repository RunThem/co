set_project('co')

set_languages('gnu17')

add_rules('mode.debug', 'mode.release')

add_cflags('-ggdb')

add_repositories('RunThem https://github.com/RunThem/My-xmake-repo')

target('co', function()
  set_kind('static')
  add_files('src/co.c')
  add_headerfiles('src/co.h')
end)

target('demo', function()
  set_kind('binary')
  add_files('src/main.c')
  add_deps('co')
end)
