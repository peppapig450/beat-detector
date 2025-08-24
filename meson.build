project(
    'beat_detector',
    'cpp',
    version: '0.1.0',
    meson_version: '>=1.3.0',
    default_options: [
        'cpp_std=c++23',
        'werror=false',
        'warning_level=3',
        'b_lto=false',
    ],
)

cpp = meson.get_compiler('cpp')

# Treat external deps' headers as system to reduce noise; keep our own includes non-system
pipewire_dep = dependency(
    'libpipewire-0.3',
    method: 'pkg-config',
    required: true,
    include_type: 'system',
)
aubio_dep = dependency('aubio', method: 'pkg-config', required: true, include_type: 'system')

inc = include_directories('include') # non-system (we want diagnostics here)

extra_warns = []
if cpp.get_id() == 'clang'
    extra_warns += [
        '-Weverything',
        '-Wno-c++98-compat',
        '-Wno-c++98-compat-pedantic',
        '-Wno-padded',
        '-Wno-switch-enum',
        '-Wno-covered-switch-default',
        '-Wno-disabled-macro-expansion',
        '-Wno-weak-vtables',
        '-Wno-global-constructors',
        '-Wno-exit-time-destructors',
        '-Wno-unused-macros',
        '-Wno-gnu-anonymous-struct',
        '-Wno-nested-anon-types',
        '-Wno-reserved-id-macro',
        '-Wno-documentation-unknown-command',
        # Nice-to-have but not in -Weverything
        '-Wshadow-all',
        '-Wshorten-64-to-32',
        '-Wimplicit-fallthrough',
        '-Wfloat-equal',
        '-Wnull-dereference',
        '-Wextra-semi',
        '-Wcomma',
    ]
elif cpp.get_id() == 'gcc'
    extra_warns += [
        '-Wall',
        '-Wextra',
        '-Wpedantic',
        '-Wconversion',
        '-Wsign-conversion',
        '-Wshadow',
        '-Wnon-virtual-dtor',
        '-Wold-style-cast',
        '-Woverloaded-virtual',
        '-Wduplicated-cond',
        '-Wduplicated-branches',
        '-Wlogical-op',
        '-Wuseless-cast',
    ]
elif cpp.get_id() == 'msvc'
    extra_warns += ['/W4']
endif

add_project_arguments(extra_warns, language: 'cpp')

# ---- Sources ----
srcs = files(
    'src/beat_detector.cpp',
    'src/main.cpp',
)

# ---- Executable ----
exe = executable(
    'beat_detector',
    srcs,
    include_directories: inc,
    dependencies: [pipewire_dep, aubio_dep],
    install: true,
)

# ---- Formatting helper (optional) ----
ffinder = find_program('clang-format', required: false)
if ffinder.found()
    fmt = run_command(
        'git',
        'ls-files',
        '*.h',
        '*.hpp',
        '*.hh',
        '*.c',
        '*.cc',
        '*.cpp',
        check: false,
    )
    fmt_list = fmt.stdout().strip().split('\n')

    # Meson does not support Python-style list comprehensions—filter with a loop
    filtered = []
    foreach f : fmt_list
        if (
            f.endswith('.h')
            or f.endswith('.hpp')
            or f.endswith('.hh')
            or f.endswith('.c')
            or f.endswith('.cc')
            or f.endswith('.cpp')
        )
            filtered += f
        endif
    endforeach

    if filtered.length() > 0
        fmt_tgt = custom_target(
            'clang_format_all',
            input: filtered,
            output: 'clang-format.stamp',
            command: [ffinder, '-i', '@INPUT@'],
            build_always_stale: true,
        )
        alias_target('format', fmt_tgt)
    endif
endif