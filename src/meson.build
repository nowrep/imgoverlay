glslang = find_program('glslangValidator')

# Needs prefix for configure_file()
if get_option('append_libdir_imgoverlay')
  libdir_imgoverlay = join_paths(get_option('prefix'), get_option('libdir'), 'imgoverlay')
else
  libdir_imgoverlay = join_paths(get_option('prefix'), get_option('libdir'))
endif

overlay_shaders = [
  'overlay.frag',
  'overlay.vert',
]
overlay_spv = []
foreach s : ['overlay.frag', 'overlay.vert']
  overlay_spv += custom_target(
    s + '.spv.h', input : s, output : s + '.spv.h',
    command : [glslang, '-V', '-x', '-o', '@OUTPUT@', '@INPUT@'])
endforeach

vklayer_files = files(
  'overlay.cpp',
  'overlay_params.cpp',
  'font_unispace.c',
  'blacklist.cpp',
  'file_utils.cpp',
  'config.cpp',
  'control.cpp',
)

opengl_files = files(
  'gl/glad.c',
  'gl/imgui_impl_opengl3.cpp',
  'gl/imgui_hud.cpp',
  'gl/inject_egl.cpp',
  'elfhacks.cpp',
  'real_dlsym.cpp',
)

#if get_option('with_dlsym').enabled()
#    pre_args += '-DHOOK_DLSYM'
#endif

if get_option('with_x11').enabled()
  pre_args += '-DHAVE_X11'

  vklayer_files += files(
    'loaders/loader_x11.cpp',
    'shared_x11.cpp',
  )

    opengl_files += files(
      'loaders/loader_glx.cpp',
      'gl/inject_glx.cpp',
    )
endif

link_args = cc.get_supported_link_arguments(['-Wl,-Bsymbolic-functions', '-Wl,-z,relro', '-Wl,--exclude-libs,ALL'])
# meson fails to check version-script so just force add
link_args += '-Wl,--version-script,@0@'.format(join_paths(meson.current_source_dir(), 'imgoverlay.version'))

vklayer_mesa_overlay = shared_library(
  'Imgoverlay',
  imgoverlay_version,
  util_files,
  vk_enum_to_str,
  vklayer_files,
  opengl_files,
  overlay_spv,
  c_args : [
    pre_args,
    no_override_init_args,
    vulkan_wsi_args
    ],
  cpp_args : [
    pre_args,
    vulkan_wsi_args
    ],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    vulkan_wsi_deps,
    dearimgui_dep,
    dep_dl,
    dep_rt,
    dep_pthread,
    dep_vulkan],
  include_directories : [inc_common],
  link_args : link_args,
  install_dir : libdir_imgoverlay,
  install : true
)

imgoverlay_dlsym = shared_library(
  'Imgoverlay_dlsym',
  files(
    'elfhacks.cpp',
    'real_dlsym.cpp',
    'hook_dlsym.cpp',
  ),
  c_args : [
    pre_args,
    no_override_init_args,
    ],
  cpp_args : [
    pre_args,
    ],
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_dl],
  include_directories : [inc_common],
  link_args : link_args,
  install_dir : libdir_imgoverlay,
  install : true
)

configure_file(input : 'imgoverlay.json.in',
  output : '@0@.@1@.json'.format(meson.project_name(), target_machine.cpu_family()),
  configuration : {'libdir_imgoverlay' : libdir_imgoverlay + '/',
                  'LAYER_SUFFIX' : target_machine.cpu_family() == 'x86_64' ? '64' : '32'},
  install : true,
  install_dir : join_paths(get_option('datadir'), 'vulkan', 'implicit_layer.d'),
)

if target_machine.cpu_family() == 'x86_64'
  imgoverlay_cpu_family = ''
  imgoverlay_opposite_bin = 'imgoverlay.x86'
else
  imgoverlay_cpu_family = '.@0@'.format(target_machine.cpu_family())
#this is probably dumb but works for now
  imgoverlay_opposite_bin = 'intentionally_borked'
endif

configure_file(input : '../bin/imgoverlay.in',
  output : 'imgoverlay@0@'.format(imgoverlay_cpu_family),
  configuration : {'libdir_imgoverlay' : libdir_imgoverlay, 'imgoverlay_sh' : imgoverlay_opposite_bin},
  install_dir : get_option('bindir'),
)
