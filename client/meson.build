bindir_client = join_paths(get_option('prefix'), get_option('bindir'))

if get_option('use_qt6')
  qt = import('qt6')
  qt_dep = dependency('qt6', modules: ['Core', 'Gui', 'Network', 'Widgets', 'WebEngineWidgets', 'QuickWidgets'], private_headers: true)
else
  qt = import('qt5')
  qt_dep = dependency('qt5', modules: ['Core', 'Network', 'Widgets', 'WebEngineWidgets', 'QuickWidgets'])
endif

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

moc_files = qt.preprocess(moc_headers : client_headers,
                          dependencies: qt_dep)

executable(
  'imgoverlayclient',
  imgoverlay_version,
  client_files,
  moc_files,
  dependencies : [ qt_dep, egl_dep ],
  install_dir : bindir_client,
  install : true
)
