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
    p.add_argument('-V', '--version', required=True)
    p.add_argument('-n', '--no-dmg', action='store_true')
    p.add_argument('-f', '--fix-paths', action='store_true')
    ret = p.parse_args()
    ret.outdir = os.path.join(ret.outdir, 'ART.app')
    return ret


def getplist(opts):
    return f"""\
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0">
    <dict>
        <key>CFBundleExecutable</key>
        <string>ART</string>
        <key>CFBundleGetInfoString</key>
        <string>{opts.version}, Copyright © 2004-2010 Gábor Horváth, 2010-2019 RawTherapee Development Team, 2019-2024 Alberto Griggio</string>
        <key>CFBundleIconFile</key>
        <string>ART.icns</string>
        <key>CFBundleIdentifier</key>
        <string>us.pixls.art.ART</string>
        <key>CFBundleInfoDictionaryVersion</key>
        <string>6.0</string>
        <key>CFBundleName</key>
        <string>ART</string>
        <key>CFBundlePackageType</key>
        <string>APPL</string>
        <key>CFBundleShortVersionString</key>
        <string>{opts.version}</string>
        <key>CFBundleSignature</key>
        <string>????</string>
        <key>CFBundleVersion</key>
        <string>{opts.version}</string>
        <key>NSHighResolutionCapable</key>
        <true />
        <key>NSHumanReadableCopyright</key>
        <string>Copyright © 2004-2010 Gábor Horváth, 2010-2019 RawTherapee Development Team, 2019-2024 Alberto Griggio</string>
        <key>LSMultipleInstancesProhibited</key>
        <true />
    </dict>
</plist>
"""


def getdlls(opts, name='ART', include_rpath=True):
    blacklist = {
	'libc++.1.dylib',
        'libSystem.B.dylib',
        }
    res = []
    d = os.path.join(os.getcwd(), 'Contents/MacOS')
    p = subprocess.Popen(['otool', '-L', os.path.join(d, name)],
                         stdout=subprocess.PIPE)
    out, _ = p.communicate()
    for line in out.decode('utf-8').splitlines()[1:]:
        line = line.strip()
        bits = line.split('(compatibility ')
        lib = bits[0].strip()
        if lib.startswith('@rpath/') and include_rpath:
            bn = lib[7:]
            for p in opts.rpath:
                plib = os.path.join(p, bn)
                if os.path.exists(plib):
                    lib = plib
                    break
        else:
            bn = os.path.basename(lib)
        if bn not in blacklist:
            res.append(lib)
    return res


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
    return [
        ('Contents/Resources/share/icons/Adwaita', [
             P('share/icons/Adwaita/scalable'),
             P('share/icons/Adwaita/index.theme'), 
             P('share/icons/Adwaita/cursors'),
        ]),
        # ('Contents', [
        #     (P('lib/gdk-pixbuf-2.0/2.10.0/loaders'), 'Frameworks'),
        # ]),
        ('Contents/Resources', [
            P('lib/gdk-pixbuf-2.0/2.10.0/loaders.cache'),
        ]),
        # ('lib', [
        #     D('/usr/lib/x86_64-linux-gnu/gio'),
        # ]),
        ('Contents/Resources/share/glib-2.0/schemas', [
            P('share/glib-2.0/schemas/gschemas.compiled'),
        ]),
        ('Contents/Resources', [
            (D('~/.local/share/lensfun/updates/version_1'), 'lensfun'),
        ]),
        # ('lib', [
        #     D('/usr/lib/x86_64-linux-gnu/gvfs/libgvfscommon.so'),
        #     D('/usr/lib/x86_64-linux-gnu/gvfs/libgvfsdaemon.so'),
        # ]),
        ('Contents/Resources/etc/gtk-3.0', [
            P('etc/gtk-3.0'),
        ]),
    ] + extra


def fix_dlls(opts, name):
    if not opts.fix_paths:
        return
    executable = os.path.join(opts.outdir, 'Contents/MacOS', name)
    if opts.verbose:
        print(f'setting @rpath for shared libraries in {executable}')
    for lib in getdlls(opts, name, False):
        libname = os.path.basename(lib)
        outlib = os.path.join(opts.outdir, 'Contents/Frameworks', libname)
        subprocess.run(['install_name_tool', '-id',
                        '/Applications/ART.app/Contents/Frameworks/' + libname,
                        outlib], check=True)
        subprocess.run(['install_name_tool', '-change',
                        lib, '/Applications/ART.app/Contents/Frameworks/'
                        + libname, executable], check=True)


def make_dmg(opts):
    if opts.verbose:
        print(f'Creating dmg in {opts.outdir}/ART-{opts.version}.dmg ...')
    subprocess.run(['hdiutil', 'create', '-format', 'UDBZ',
                    '-fs', 'HFS+', '-srcdir', 'ART.app',
                    '-volname', f'ART-{opts.version}',
                    f'ART-{opts.version}.dmg'],
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
        out.write('[Settings]\ngtk-button-images=1\n')
    with open(os.path.join(opts.outdir, 'Contents/Info.plist'), 'w') as out:
        out.write(getplist(opts))
    shutil.copy2(os.path.join(os.path.dirname(__file__), 'art.icns'),
                 os.path.join(opts.outdir, 'Contents/Resources/ART.icns'))
    with open(os.path.join(opts.outdir, 'Contents/Resources/options'),
              'a') as out:
        out.write('\n[Lensfun]\nDBDirectory=../Contents/Resources/lensfun\n')
        # if opts.exiftool:
        #     out.write('\n[Metadata]\nExiftoolPath=exiftool\n')
    for name in ('ART', 'ART-cli'):
        fix_dlls(opts, name)
        shutil.move(os.path.join(opts.outdir, 'Contents/MacOS', name),
                    os.path.join(opts.outdir, 'Contents/MacOS', name + '.bin'))
    with open(os.path.join(opts.outdir, 'Contents/Resources/fonts.conf'),
              'w') as out:
        out.write("""\
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">

<fontconfig>
  <dir>/usr/share/fonts</dir>
  <dir>/usr/local/share/fonts</dir>
  <dir prefix="xdg">fonts</dir>

  <cachedir>/var/cache/fontconfig</cachedir>
  <cachedir prefix="xdg">fontconfig</cachedir>

  <match target="pattern">
    <test qual="any" name="family"><string>mono</string></test>
    <edit name="family" mode="assign" binding="same"><string>monospace</string></edit>
  </match>
  <match target="pattern">
    <test qual="any" name="family"><string>sans serif</string></test>
    <edit name="family" mode="assign" binding="same"><string>sans-serif</string></edit>
  </match>

  <match target="pattern">
    <test qual="any" name="family"><string>sans</string></test>
    <edit name="family" mode="assign" binding="same"><string>sans-serif</string></edit>
  </match>

  <alias>
    <family>DejaVu Sans</family>
    <default><family>sans-serif</family></default>
  </alias>
  <alias>
    <family>sans-serif</family>
    <prefer><family>DejaVu Sans</family></prefer>
  </alias>
  <alias>
    <family>DejaVu Serif</family>
    <default><family>serif</family></default>
  </alias>
  <alias>
    <family>serif</family>
    <prefer><family>DejaVu Serif</family></prefer>
  </alias>
  <alias>
    <family>DejaVu Sans Mono</family>
    <default><family>monospace</family></default>
  </alias>
  <alias>
    <family>monospace</family>
    <prefer><family>DejaVu Sans Mono</family></prefer>
  </alias>

  <config><rescan><int>30</int></rescan></config>
</fontconfig>
""")        
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
export GTK_CSD=0
d=$(dirname "$0")/..
export GDK_PIXBUF_MODULE_FILE="$d/Resources/loaders.cache"
export GDK_PIXBUF_MODULEDIR="$d/Frameworks"
#export GIO_MODULE_DIR="$d/Frameworks/gio/modules"
export DYLD_LIBRARY_PATH="$d/Frameworks"
export FONTCONFIG_FILE="$d/Resources/fonts.conf"
#export ART_EXIFTOOL_BASE_DIR="$d/Resources/exiftool"
export GTK_PATH="$d/Resources/etc/gtk-3.0"
export GTK_IM_MODULE_FILE="$d/Resources/etc/gtk-3.0/gtk.immodules"
export GSETTINGS_SCHEMA_DIR="$d/Resources/share/glib-2.0/schemas"
"$d/MacOS/ART.bin" "$@"
""")
    with open(os.path.join(opts.outdir, 'Contents/MacOS/ART-cli'), 'w') as out:
        out.write("""#!/bin/bash
export ART_restore_GIO_MODULE_DIR=$GIO_MODULE_DIR
export ART_restore_DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH
d=$(dirname "$0")/..
#export GIO_MODULE_DIR="$d/Frameworks/gio/modules"
export DYLD_LIBRARY_PATH="$d/Frameworks"
#export ART_EXIFTOOL_BASE_DIR="$d/lib/exiftool"
exec "$d/MacOS/ART-cli.bin" "$@"
""")
    for name in ('ART', 'ART-cli'):
        os.chmod(os.path.join(opts.outdir, 'Contents/MacOS', name), 0o755)
    if not opts.no_dmg:
        make_dmg(opts)

if __name__ == '__main__':
    main()
