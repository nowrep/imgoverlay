bindir_client = join_paths(get_option('prefix'), get_option('bindir'))

qt5 = import('qt5')
qt5_dep = dependency('qt5', modules: ['Core', 'Network', 'Widgets', 'WebEngineWidgets', 'QuickWidgets'])

egl_dep = dependency('egl')

client_files = files(
  'main.cpp',
  'groupconfig.cpp',
  'manager.cpp',
  'webview.cpp',
  'utils.cpp',
)
client_headers = files(
  'manager.h',
  'webview.h',
)

moc_files = qt5.preprocess(moc_headers : client_headers,
                           dependencies: qt5_dep)

executable(
  'imgoverlayclient',
  imgoverlay_version,
  client_files,
  moc_files,
  dependencies : [ qt5_dep, egl_dep ],
  install_dir : bindir_client,
  install : true
)
