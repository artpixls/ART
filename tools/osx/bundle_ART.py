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
    p.add_argument('-e', '--exiftool', action='store_true')
    # p.add_argument('-i', '--imageio', help='path to imageio plugins')
    # p.add_argument('-b', '--imageio-bin', help='path to imageio binaries')
    # p.add_argument('-I', '--imageio-download', action='store_true')
    p.add_argument('-v', '--verbose', action='store_true')
    p.add_argument('-r', '--rpath', action='append')
    p.add_argument('-p', '--prefix')
    p.add_argument('-n', '--no-dmg', action='store_true')
    p.add_argument('-d', '--dmg-name', default='ART')
    p.add_argument('-s', '--shell', default='/bin/sh')
    ret = p.parse_args()
    ret.outdir = os.path.join(ret.outdir, 'ART.app')
    return ret


def getdlls(opts):
    blacklist = ['/System/', '/usr/lib/']
    res = set()
    d = os.path.join(os.getcwd(), 'Contents/MacOS')
    to_process = [os.path.join(d, 'ART'),
                  os.path.join(getprefix(opts), 'bin/dbus-daemon')]
    if opts.verbose:
        print('========== getdlls ==========')
    seen = set()
    while to_process:
        name = to_process[-1]
        to_process.pop()
        if name in seen:
            continue
        seen.add(name)
        if opts.verbose:
            print(f'computing dependencies for: {name}')
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
                if opts.verbose:
                    print(f'   {lib}')
                res.add(lib)
                to_process.append(lib)
    if opts.verbose:
        print('=============================')
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
    if opts.exiftool and os.path.exists('/usr/local/bin/exiftool'):
        extra = [('Contents/Resources/exiftool',
                  [('/usr/local/bin/exiftool', 'exiftool'),
                   ('/usr/local/bin/lib', 'lib')])]
    else:
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
    return [
        ('Contents/Frameworks',
         glob.glob(os.path.join(pref,
                                'lib/gdk-pixbuf-2.0/2.10.0/'
                                'loaders/*.so'))),
        ('Contents/Frameworks',
         glob.glob(os.path.join(pref,
                                'lib/gtk-3.0/3*/immodules/*.so'))),
        ('Contents/Resources', [
            os.path.join(pref, 'bin/gtk-query-immodules-3.0'),
            os.path.join(pref, 'bin/gdk-pixbuf-query-loaders'),
            os.path.join(pref, 'bin/dbus-daemon')
        ]),
        ('Contents/Resources/dbus-1', [
            os.path.join(pref, 'share/dbus-1/session.conf')
        ]),
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


def get_version(opts):
    with open('Contents/Resources/AboutThisBuild.txt') as f:
        for line in f:
            if line.startswith('Version: '):
                return line.split()[-1]
    return 'UNKNOWN'


def make_info_plist(opts):
    version = get_version(opts)
    with open(os.path.join(opts.outdir, 'Contents', 'Info.plist'), 'w') as out:
        out.write(f"""\
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0">
    <dict>
        <key>CFBundleExecutable</key>
        <string>ART</string>
        <key>CFBundleGetInfoString</key>
        <string>{version}, Copyright © 2004-2010 Gábor Horváth, 2010-2019 RawTherapee Development Team, 2019-2024 Alberto Griggio</string>
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
        <string>{version}</string>
        <key>CFBundleSignature</key>
        <string>????</string>
        <key>CFBundleVersion</key>
        <string>{version}</string>
        <key>NSHighResolutionCapable</key>
        <true />
        <key>NSHumanReadableCopyright</key>
        <string>Copyright © 2004-2010 Gábor Horváth, 2010-2019 RawTherapee Development Team, 2019-2024 Alberto Griggio</string>
        <key>LSMultipleInstancesProhibited</key>
        <true />
        <key>NSDesktopFolderUsageDescription</key>
        <string>ART requires permission to access the Desktop folder.</string>
        <key>NSDocumentsFolderUsageDescription</key>
        <string>ART requires permission to access the Documents folder.</string>
        <key>NSDownloadsFolderUsageDescription</key>
        <string>ART requires permission to access the Downloads folder.</string>
        <key>NSRemovableVolumesUsageDescription</key>
        <string>ART requires permission to access files on Removable Volumes.</string>        
    </dict>
</plist>
""")


def make_icns(opts):
    icondir = os.path.join(opts.tempdir, 'ART.iconset')
    os.mkdir(icondir)
    for i, sz in enumerate([16, 32, 64, 128, 256, 512]):
        shutil.copy2(os.path.join('Contents/Resources/images',
                                  f'ART-logo-{sz}.png'),
                     os.path.join(icondir, f'icon_{sz}x{sz}.png'))
        if i > 0:
            sz2 = sz / 2
            shutil.copy2(os.path.join('Contents/Resources/images',
                                      f'ART-logo-{sz}.png'),
                         os.path.join(icondir, f'icon_{sz2}x{sz2}@2x.png'))
    shutil.copy2(os.path.join('Contents/Resources/images',
                              'ART-logo-1024.png'),
                 os.path.join(icondir, 'icon_512x512@2x.png'))
    subprocess.run(['iconutil', '-c', 'icns', 'ART.iconset'], check=True,
                   cwd=opts.tempdir)
    shutil.copy2(os.path.join(opts.tempdir, 'ART.icns'),
                 os.path.join(opts.outdir, 'Contents/Resources/ART.icns'))
    

def make_dmg(opts):
    if opts.verbose:
        print(f'Creating dmg in {opts.outdir}/{opts.dmg_name}.dmg ...')
    subprocess.run(['hdiutil', 'create', '-format', 'UDBZ',
                    '-fs', 'HFS+', '-srcdir', 'ART.app',
                    '-volname', opts.dmg_name,
                    f'{opts.dmg_name}.dmg'],
                    cwd=os.path.join(opts.outdir, '..'),
                   check=True)

def build_launcher(opts, prog):
    cname = os.path.join(opts.tempdir, f'{prog}_launcher.c')
    with open(cname, 'w') as out:
        out.write("""
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <stdio.h>


int main(int argc, char *const argv[])
{
    char buf[4096];
    strlcpy(buf, argv[0], 4096);
    char *d0 = dirname(buf);

    char buf2[4096];
    strlcpy(buf2, d0, 4096);
    """)
        out.write(f'strlcat(buf2, "/.{prog}.sh", 4096);\n')
        out.write("""
    strlcpy(buf, d0, 4096);
    strlcat(buf, "/.zsh", 4096);
    
    char **newargs = (char **)malloc(sizeof(char *) * (argc + 1));
    newargs[0] = buf;
    newargs[1] = buf2;
    for (int i = 1; i < argc; ++i) {
        newargs[i+1] = argv[i];
    }
    return execv(buf, newargs);
}
""")
    if opts.verbose:
        print(f'building launcher for {prog}...')
    subprocess.run(['clang', cname, '-o',
                    os.path.join(opts.outdir, f'Contents/MacOS/{prog}_launch')],
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
        try:
            shutil.copy2(lib,
                         os.path.join(opts.outdir, 'Contents/Frameworks',
                                      os.path.basename(lib)))
        except FileNotFoundError as e:
            sys.stderr.write(f'WARNING: {lib} not found, skipping\n')
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
        make_info_plist(opts)
        make_icns(opts)
        
        build_launcher(opts, 'ART')
        build_launcher(opts, 'ART-cli')

    os.makedirs(os.path.join(opts.outdir, 'Contents/Resources/share/gtk-3.0'))
    with open(os.path.join(opts.outdir,
                           'Contents/Resources/share/gtk-3.0/settings.ini'),
              'w') as out:
        out.write('[Settings]\ngtk-primary-button-warps-slider = true\n'
                  'gtk-overlay-scrolling = true\n')
    with open(os.path.join(opts.outdir, 'Contents/Resources/options'),
              'a') as out:
        out.write('\n[Lensfun]\nDBDirectory=lensfun\n')
    for name in ('ART', 'ART-cli'):
        shutil.move(os.path.join(opts.outdir, 'Contents/MacOS', name),
                    os.path.join(opts.outdir, 'Contents/MacOS',
                                 '.' + name + '.bin'))
        shutil.move(os.path.join(opts.outdir, 'Contents/MacOS',
                                 name + '_launch'),
                    os.path.join(opts.outdir, 'Contents/MacOS', name))
    with open(os.path.join(opts.outdir, 'Contents/MacOS/.ART.sh'), 'w') as out:
        out.write("""#!/bin/zsh
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
export ART_restore_DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS

d=$(dirname "$0")/..

export DYLD_LIBRARY_PATH="$d/Frameworks"
export GTK_CSD=0
export GDK_PIXBUF_MODULEDIR="$d/Frameworks"
export FONTCONFIG_FILE="$d/Resources/fonts.conf"
export GTK_PATH="$d/Resources/etc/gtk-3.0"
export GSETTINGS_SCHEMA_DIR="$d/Resources/share/glib-2.0/schemas"
export XDG_DATA_DIRS="$d/Resources/share"
export GDK_RENDERING=similar
export ART_EXIFTOOL_BASE_DIR="$d/Resources/exiftool"

t="$TMPDIR/ART-$USER"
/bin/mkdir -p "$t"

DBUS_SOCK_FILE="$t/dbus.sock"
export DBUS_SESSION_BUS_ADDRESS=unix:path=$DBUS_SOCK_FILE

/usr/sbin/netstat -an | /usr/bin/grep -q $DBUS_SOCK_FILE
if [ $? -ne 0 ]; then
    /bin/rm -f $DBUS_SOCK_FILE
    DBUS_PID=$("$d/Resources/dbus-daemon" --fork --print-pid --config-file="$d/Resources/dbus-1/session.conf" --address "$DBUS_SESSION_BUS_ADDRESS")

    "$d/Resources/gdk-pixbuf-query-loaders" "$d/Frameworks/"libpixbufloader-svg.so > "$t/loader.cache"
    "$d/Resources/gtk-query-immodules-3.0" "$d"/Frameworks/im-*.so > "$t/gtk.immodules"
    export GDK_PIXBUF_MODULE_FILE="$t/loader.cache"
    export GTK_IM_MODULE_FILE="$t/gtk.immodules"
fi

"$d/MacOS/.ART.bin" "$@"

if [ "$DBUS_PID" != "" ]; then
    /bin/rm -rf "$t"
    kill $DBUS_PID
    /bin/rm -f $DBUS_SOCK_FILE
fi
""")
    with open(os.path.join(opts.outdir,
                           'Contents/MacOS/.ART-cli.sh'), 'w') as out:
        out.write("""#!/bin/zsh
export ART_restore_GIO_MODULE_DIR=$GIO_MODULE_DIR
export ART_restore_DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH
d=$(dirname "$0")/..
export DYLD_LIBRARY_PATH="$d/Frameworks"
export ART_EXIFTOOL_BASE_DIR="$d/Resources/exiftool"
exec "$d/MacOS/.ART-cli.bin" "$@"
""")
    shutil.copy(opts.shell, os.path.join(opts.outdir,
                                         'Contents/MacOS/.zsh'))
    if not opts.no_dmg:
        make_dmg(opts)

if __name__ == '__main__':
    main()
