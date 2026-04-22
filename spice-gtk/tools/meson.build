#
#  spicy-spice-gtk client
#

spice_cmdline_sources = [
  'spice-cmdline.c',
  'spice-cmdline.h',
]

#
# spicy-stats and spicy-screenshot
#
foreach exe : ['spicy-stats', 'spicy-screenshot']
  executable(exe,
             sources : spice_cmdline_sources + ['@0@.c'.format(exe)],
             c_args : '-Wno-deprecated-declarations',
             install : true,
             dependencies : spice_client_glib_dep)
endforeach

#
# spicy
#
if spice_gtk_has_gtk
  spicy_sources = [
    'spicy.c',
    'spicy-connect.c',
    'spicy-connect.h',
  ]

  #
  # === WINDOWS ICON SUPPORT ===
  #
  if host_machine.system() == 'windows'
    message('Building spicy.exe with embedded icon resource')

    # spicy.rc must be in this directory and contain:
    #   1 ICON "spicy.ico"
    #
    spicy_icon = windows.compile_resources('spicy.rc')
    spicy_sources += spicy_icon
  endif

  executable('spicy',
             sources : spicy_sources + spice_cmdline_sources,
             c_args : '-Wno-deprecated-declarations',
             gui_app : true,          # IMPORTANT!! prevents console window
             install : true,
             dependencies : spice_client_gtk_dep)
endif

