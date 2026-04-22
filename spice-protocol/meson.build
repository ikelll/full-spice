#
# project definition
#
project('spice-protocol',
        'c',
        version : '0.14.5',
        license : 'BSD',
        meson_version : '>= 0.41.0')

subdir('spice')

spice_protocol_dep = declare_dependency(include_directories : include_directories('.'))

#
# spice-protocol.pc generation
#
datadir = get_option('datadir')
pkgconfig_dir = join_paths(get_option('prefix'), datadir, 'pkgconfig')

pkgconfig = import('pkgconfig')
pkgconfig.generate(name : meson.project_name(),
                   description : 'SPICE protocol headers',
                   version : meson.project_version(),
                   subdirs : 'spice-1',
                   install_dir : pkgconfig_dir,
                   variables : 'datadir=@0@'.format(join_paths('${prefix}', datadir)))
