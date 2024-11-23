#!/usr/bin/env python3

"""
Helper script to generate an ART self-contained "bundle" directory,
with all the required dependencies (MacOS version)
"""

import os, sys
import shutil
import subprocess
import argparse
from urllib.request import urlopen
import tarfile
import tempfile
import io
import glob


def getopts():
    p = argparse.ArgumentParser()
    p.add_argument('-o', '--outdir', required=True,
                   help='output directory for the bundle')
    # p.add_argument('-e', '--exiftool', help='path to exiftool dir')
    # p.add_argument('-E', '--exiftool-download', action='store_true')
    # p.add_argument('-i', '--imageio', help='path to imageio plugins')
    # p.add_argument('-b', '--imageio-bin', help='path to imageio binaries')
    # p.add_argument('-I', '--imageio-download', action='store_true')
    p.add_argument('-v', '--verbose', action='store_true')
    p.add_argument('-r', '--rpath', action='append')
    p.add_argument('-p', '--prefix')
    p.add_argument('-n', '--no-dmg', action='store_true')
    p.add_argument('-d', '--dmg-name', default='ART')
    ret = p.parse_args()
    ret.outdir = os.path.join(ret.outdir, 'ART.app')
    return ret


def getdlls(opts):
    blacklist = ['/System/', '/usr/lib/']
    res = set()
    d = os.path.join(os.getcwd(), 'Contents/MacOS')
    to_process = [os.path.join(d, 'ART')]
    seen = set()
    while to_process:
        name = to_process[-1]
        to_process.pop()
        if name in seen:
            continue
        seen.add(name)
        r = subprocess.run(['otool', '-L', name], capture_output=True,
                           encoding='utf-8')
        out = r.stdout
        for line in out.splitlines()[1:]:
            line = line.strip()
            bits = line.split('(compatibility ')
            lib = bits[0].strip()
            if lib.startswith('@rpath/'):
                bn = lib[7:]
                for p in opts.rpath:
                    plib = os.path.join(p, bn)
                    if os.path.exists(plib):
                        lib = plib
                        break
            if not any(lib.startswith(p) for p in blacklist):
                res.add(lib)
                to_process.append(lib)
    return sorted(res)


def getprefix(opts):
    if opts.prefix:
        return opts.prefix
    d = os.path.join(os.getcwd(), 'Contents/MacOS')
    p = subprocess.Popen(['otool', '-L', os.path.join(d, 'ART')],
                         stdout=subprocess.PIPE)
    out, _ = p.communicate()
    for line in out.decode('utf-8').splitlines()[1:]:
        line = line.strip()
        bits = line.split('(compatibility ')
        lib = bits[0].strip()
        if 'libgtk-3.0' in lib:
            return os.path.dirname(os.path.dirname(lib))
    assert False, "can't determine prefix"


def extra_files(opts):
    pref = getprefix(opts)
    def D(s): return os.path.expanduser(s)
    def P(s): return os.path.join(pref, s)
    # if opts.exiftool and os.path.isdir(opts.exiftool):
    #     extra = [('lib', [(opts.exiftool, 'exiftool')])]
    # elif opts.exiftool_download:
    #     with urlopen('https://exiftool.org/ver.txt') as f:
    #         ver = f.read().strip().decode('utf-8')
    #     name = 'Image-ExifTool-%s.tar.gz' % ver
    #     with urlopen('https://exiftool.org/' + name) as f:
    #         if opts.verbose:
    #             print('downloading %s from https://exiftool.org ...' % name)
    #         tf = tarfile.open(fileobj=io.BytesIO(f.read()))
    #         if opts.verbose:
    #             print('unpacking %s ...' % name)
    #         tf.extractall(opts.tempdir)
    #     extra = [('lib', [(os.path.join(opts.tempdir, 'Image-ExifTool-' + ver),
    #                        'exiftool')])]
    # else:
    #     extra = []
    extra = []
    # if opts.imageio:
    #     extra.append(('.', [(opts.imageio, 'imageio')]))
    # elif opts.imageio_download:
    #     with urlopen('https://bitbucket.org/agriggio/art-imageio/'
    #                  'downloads/ART-imageio.tar.gz') as f:
    #         if opts.verbose:
    #             print('downloading ART-imageio.tar.gz '
    #                   'from https://bitbucket.org ...')
    #         tf = tarfile.open(fileobj=io.BytesIO(f.read()))
    #         if opts.verbose:
    #             print('unpacking ART-imageio.tar.gz ...')
    #         tf.extractall(opts.tempdir)
    #     extra.append(('.', [(os.path.join(opts.tempdir, 'ART-imageio'),
    #                          'imageio')]))
    # if opts.imageio_bin:
    #     extra.append(('imageio', [(opts.imageio_bin, 'bin')]))            
    # elif opts.imageio_download:
    #     with urlopen('https://bitbucket.org/agriggio/art-imageio/'
    #                  'downloads/ART-imageio-bin-linux64.tar.gz') as f:
    #         if opts.verbose:
    #             print('downloading ART-imageio-bin-linux64.tar.gz '
    #                   'from http://bitbucket.org ...')
    #         tf = tarfile.open(fileobj=io.BytesIO(f.read()))
    #         if opts.verbose:
    #             print('unpacking ART-imageio-bin-linux64.tar.gz ...')
    #         tf.extractall(opts.tempdir)
    #     extra.append(('imageio',
    #                   [(os.path.join(opts.tempdir, 'ART-imageio-bin-linux64'),
    #                     'bin')]))
    extra.append(('Contents/Frameworks',
                  glob.glob(os.path.join(pref,
                                         'lib/gdk-pixbuf-2.0/2.10.0/'
                                         'loaders/*.so'))))
    extra.append(('Contents/Frameworks',
                  glob.glob(os.path.join(pref,
                                         'lib/gtk-3.0/3*/immodules/*.so'))))
    extra.append(('Contents/Resources',
                  [os.path.join(pref, 'bin/gtk-query-immodules-3.0'),
                   os.path.join(pref, 'bin/gdk-pixbuf-query-loaders')]))
    return [
        ('Contents/Resources/share/icons/Adwaita', [
             P('share/icons/Adwaita/scalable'),
             P('share/icons/Adwaita/index.theme'), 
             P('share/icons/Adwaita/cursors'),
        ]),
        ('Contents/Resources/share/icons', [
             P('share/icons/hicolor'),
        ]),
        ('Contents/Resources/share/glib-2.0/schemas', [
            P('share/glib-2.0/schemas/gschemas.compiled'),
        ]),
        ('Contents/Resources', [
            (D('~/.local/share/lensfun/updates/version_1'), 'lensfun'),
        ]),
        ('Contents/Resources/etc', [
            P('etc/gtk-3.0'),
        ]),
        ('Contents/Resources', [
            P('etc/fonts/fonts.conf'),
        ]),
    ] + extra


def make_dmg(opts):
    if opts.verbose:
        print(f'Creating dmg in {opts.outdir}/{opts.dmg_name}.dmg ...')
    subprocess.run(['hdiutil', 'create', '-format', 'UDBZ',
                    '-fs', 'HFS+', '-srcdir', 'ART.app',
                    '-volname', opts.dmg_name,
                    f'{opts.dmg_name}.dmg'],
                    cwd=os.path.join(opts.outdir, '..'),
                   check=True)


def main():
    opts = getopts()
    d = os.getcwd()
    if not os.path.exists('Contents/MacOS/ART'):
        sys.stderr.write('ERROR: ART not found! Please run this script '
                         'from the build directory of ART\n')
        sys.exit(1)
    if opts.verbose:
        print('copying %s to %s' % (os.getcwd(), opts.outdir))
    shutil.copytree(d, opts.outdir)
    if not os.path.exists(os.path.join(opts.outdir, 'Contents/Frameworks')):
        os.mkdir(os.path.join(opts.outdir, 'Contents/Frameworks'))
    for lib in getdlls(opts):
        if opts.verbose:
            print('copying: %s' % lib)
        shutil.copy2(lib,
                     os.path.join(opts.outdir, 'Contents/Frameworks',
                                  os.path.basename(lib)))
    with tempfile.TemporaryDirectory() as d:
        opts.tempdir = d
        for key, elems in extra_files(opts):
            for elem in elems:
                name = None
                if isinstance(elem, tuple):
                    elem, name = elem
                else:
                    name = os.path.basename(elem)
                if opts.verbose:
                    print('copying: %s' % elem)
                if not os.path.exists(elem):
                    print('SKIPPING non-existing: %s' % elem)
                elif os.path.isdir(elem):
                    shutil.copytree(elem, os.path.join(opts.outdir, key, name))
                else:
                    dest = os.path.join(opts.outdir, key, name)
                    destdir = os.path.dirname(dest)
                    if not os.path.exists(destdir):
                        os.makedirs(destdir)
                    shutil.copy2(elem, dest)
    os.makedirs(os.path.join(opts.outdir, 'Contents/Resources/share/gtk-3.0'))
    with open(os.path.join(opts.outdir,
                           'Contents/Resources/share/gtk-3.0/settings.ini'),
              'w') as out:
        #out.write('[Settings]\ngtk-button-images=1\n')
        out.write('[Settings]\ngtk-primary-button-warps-slider = true\n'
                  'gtk-overlay-scrolling = true\n')
    with open(os.path.join(opts.outdir, 'Contents/Resources/options'),
              'a') as out:
        out.write('\n[Lensfun]\nDBDirectory=../Contents/Resources/lensfun\n')
        # if opts.exiftool:
        #     out.write('\n[Metadata]\nExiftoolPath=exiftool\n')
    for name in ('ART', 'ART-cli'):
        #fix_dlls(opts, name)
        shutil.move(os.path.join(opts.outdir, 'Contents/MacOS', name),
                    os.path.join(opts.outdir, 'Contents/MacOS',
                                 '.' + name + '.bin'))
    with open(os.path.join(opts.outdir, 'Contents/MacOS/ART'), 'w') as out:
        out.write("""#!/bin/bash
export ART_restore_GTK_CSD=$GTK_CSD
export ART_restore_GDK_PIXBUF_MODULE_FILE=$GDK_PIXBUF_MODULE_FILE
export ART_restore_GDK_PIXBUF_MODULEDIR=$GDK_PIXBUF_MODULEDIR
export ART_restore_GIO_MODULE_DIR=$GIO_MODULE_DIR
export ART_restore_DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH
export ART_restore_FONTCONFIG_FILE=$FONTCONFIG_FILE
export ART_restore_GTK_PATH=$GTK_PATH
export ART_restore_GTK_IM_MODULE_FILE=$GTK_IM_MODULE_FILE
export ART_restore_GSETTINGS_SCHEMA_DIR=$GSETTINGS_SCHEMA_DIR
export ART_restore_XDG_DATA_DIRS=$XDG_DATA_DIRS
d=$(dirname "$0")/..
t=$(/usr/bin/mktemp -d)
export DYLD_LIBRARY_PATH="$d/Frameworks"
export GTK_CSD=0
"$d/Resources/gdk-pixbuf-query-loaders" "$d/Frameworks/"libpixbufloader-svg.so > "$t/loader.cache"
"$d/Resources/gtk-query-immodules-3.0" "$d"/Frameworks/im-*.so > "$t/gtk.immodules"
export GDK_PIXBUF_MODULE_FILE="$t/loader.cache"
export GTK_IM_MODULE_FILE="$t/gtk.immodules"
export GDK_PIXBUF_MODULEDIR="$d/Frameworks"
export FONTCONFIG_FILE="$d/Resources/fonts.conf"
export GTK_PATH="$d/Resources/etc/gtk-3.0"
export GSETTINGS_SCHEMA_DIR="$d/Resources/share/glib-2.0/schemas"
export XDG_DATA_DIRS="$d/Resources/share"
export GDK_RENDERING=similar
export GTK_OVERLAY_SCROLLING=0
"$d/MacOS/.ART.bin" "$@"
/bin/rm -rf "$t"
""")
    with open(os.path.join(opts.outdir, 'Contents/MacOS/ART-cli'), 'w') as out:
        out.write("""#!/bin/bash
export ART_restore_GIO_MODULE_DIR=$GIO_MODULE_DIR
export ART_restore_DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH
d=$(dirname "$0")/..
export DYLD_LIBRARY_PATH="$d/Frameworks"
exec "$d/MacOS/.ART-cli.bin" "$@"
""")
    for name in ('ART', 'ART-cli'):
        os.chmod(os.path.join(opts.outdir, 'Contents/MacOS', name), 0o755)
    if not opts.no_dmg:
        make_dmg(opts)

if __name__ == '__main__':
    main()
