#!/usr/bin/env python3

"""
Helper script to generate an ART self-contained "bundle" directory,
with all the required dependencies (linux version)
"""

import os, sys
import shutil
import subprocess
import argparse
from urllib.request import urlopen, Request
import tarfile
import tempfile
import io
import json
import time


def getopts():
    p = argparse.ArgumentParser()
    p.add_argument('-o', '--outdir', required=True,
                   help='output directory for the bundle')
    p.add_argument('-e', '--exiftool', help='path to exiftool dir')
    p.add_argument('-E', '--exiftool-download', action='store_true')
    p.add_argument('-i', '--imageio', help='path to imageio plugins')
    p.add_argument('-b', '--imageio-bin', help='path to imageio binaries')
    p.add_argument('-I', '--imageio-download', action='store_true')
    p.add_argument('-v', '--verbose', action='store_true')
    p.add_argument('-d', '--debug', action='store_true')
    ret = p.parse_args()
    return ret


def get_imageio_releases():
    auth = os.getenv('GITHUB_AUTH')
    req = Request('https://api.github.com/repos/artraweditor/ART-imageio/releases')
    if auth is not None:
        req.add_header('authorization', 'Bearer ' + auth)
    with urlopen(req) as f:
        data = f.read().decode('utf-8')
    rel = json.loads(data)
    def key(r):
        return (r['draft'], r['prerelease'],
                time.strptime(r['published_at'], '%Y-%m-%dT%H:%M:%SZ'))
    class RelInfo:
        def __init__(self, rel):
            self.rels = sorted(rel, key=key, reverse=True)
            
        def asset(self, name):
            for rel in self.rels:
                for asset in rel['assets']:
                    if asset['name'] == name:
                        res = Request(asset['browser_download_url'])
                        if auth is not None:
                            res.add_header('authorization', 'Bearer ' + auth)
                        return res
            return None
    return RelInfo(rel)


def getdlls(opts):
    blacklist = {
	'linux-vdso.so.1',
	'libm.so.6',
	'libpthread.so.0',
	'libc.so.6',
	'ld-linux-x86-64.so.2',
	'libdl.so.2',
	'libsystemd.so.0',
        'librt.so.1',
        'libstdc++.so.6',
        }
    res = []
    d = os.getcwd()
    p = subprocess.Popen(['ldd', os.path.join(d, 'ART')],
                         stdout=subprocess.PIPE)
    out, _ = p.communicate()
    for line in out.decode('utf-8').splitlines():
        if ' => ' in line:
            bits = line.split()
            lib = bits[2]
            bn = os.path.basename(lib)
            if bn not in blacklist:
                res.append(lib)
    return res


def extra_files(opts):
    def D(s): return os.path.expanduser(s)
    if opts.exiftool and os.path.isdir(opts.exiftool):
        extra = [('lib', [(opts.exiftool, 'exiftool')])]
    elif opts.exiftool_download:
        with urlopen('https://exiftool.org/ver.txt') as f:
            ver = f.read().strip().decode('utf-8')
        name = 'Image-ExifTool-%s.tar.gz' % ver
        with urlopen('https://exiftool.org/' + name) as f:
            if opts.verbose:
                print('downloading %s from https://exiftool.org ...' % name)
            tf = tarfile.open(fileobj=io.BytesIO(f.read()))
            if opts.verbose:
                print('unpacking %s ...' % name)
            tf.extractall(opts.tempdir)
        extra = [('lib', [(os.path.join(opts.tempdir, 'Image-ExifTool-' + ver),
                           'exiftool')])]
    else:
        extra = []
    imageio = get_imageio_releases() if opts.imageio_download else None
    if opts.imageio:
        extra.append(('.', [(opts.imageio, 'imageio')]))
    elif opts.imageio_download:
        with urlopen(imageio.asset('ART-imageio.tar.gz')) as f:
            if opts.verbose:
                print('downloading ART-imageio.tar.gz '
                      'from GitHub ...')
            tf = tarfile.open(fileobj=io.BytesIO(f.read()))
            if opts.verbose:
                print('unpacking ART-imageio.tar.gz ...')
            tf.extractall(opts.tempdir)
        extra.append(('.', [(os.path.join(opts.tempdir, 'ART-imageio'),
                             'imageio')]))
    if opts.imageio_bin:
        extra.append(('imageio', [(opts.imageio_bin, 'bin')]))            
    elif opts.imageio_download:
        with urlopen(imageio.asset('ART-imageio-bin-linux64.tar.gz')) as f:
            if opts.verbose:
                print('downloading ART-imageio-bin-linux64.tar.gz '
                      'from GitHub ...')
            tf = tarfile.open(fileobj=io.BytesIO(f.read()))
            if opts.verbose:
                print('unpacking ART-imageio-bin-linux64.tar.gz ...')
            tf.extractall(opts.tempdir)
        extra.append(('imageio',
                      [(os.path.join(opts.tempdir, 'ART-imageio-bin-linux64'),
                        'bin')]))
    return [
        ('share/icons/Adwaita', [
            D('/usr/share/icons/Adwaita/scalable'),
            D('/usr/share/icons/Adwaita/index.theme'), 
            D('/usr/share/icons/Adwaita/cursors'),
        ]),
        ('lib', [
            D('/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0'),
        ]),
        ('lib/gio/modules', [
            D('/usr/lib/x86_64-linux-gnu/gio/modules/libgioremote-volume-monitor.so'),
            D('/usr/lib/x86_64-linux-gnu/gio/modules/libgvfsdbus.so'),
        ]),
        ('lib', [
            D('/usr/lib/x86_64-linux-gnu/gtk-3.0/3.0.0/immodules')
        ]),
        ('lib', [
            D('/usr/lib/x86_64-linux-gnu/libgtk-3-0/gtk-query-immodules-3.0')
        ]),
        ('share/glib-2.0/schemas', [
            D('/usr/share/glib-2.0/schemas/gschemas.compiled'),
        ]),
        ('share', [
            (D('~/.local/share/lensfun/updates/version_1'), 'lensfun'),
        ]),
        ('lib', [
            D('/usr/lib/x86_64-linux-gnu/gvfs/libgvfscommon.so'),
            D('/usr/lib/x86_64-linux-gnu/gvfs/libgvfsdaemon.so'),
        ]),
    ] + extra


def main():
    opts = getopts()
    d = os.getcwd()
    if not os.path.exists('ART'):
        sys.stderr.write('ERROR: ART not found! Please run this script '
                         'from the build directory of ART\n')
        sys.exit(1)
    if opts.verbose:
        print('copying %s to %s' % (os.getcwd(), opts.outdir))
    shutil.copytree(d, opts.outdir)
    if not os.path.exists(os.path.join(opts.outdir, 'lib')):
        os.mkdir(os.path.join(opts.outdir, 'lib'))
    for lib in getdlls(opts):
        if opts.verbose:
            print('copying: %s' % lib)
        shutil.copy2(lib,
                     os.path.join(opts.outdir, 'lib', os.path.basename(lib)))
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
    os.makedirs(os.path.join(opts.outdir, 'share/gtk-3.0'))
    with open(os.path.join(opts.outdir, 'share/gtk-3.0/settings.ini'), 'w') \
         as out:
        out.write('[Settings]\ngtk-button-images=1\n')
    with open(os.path.join(opts.outdir, 'options'), 'a') as out:
        out.write('\n[Lensfun]\nDBDirectory=share/lensfun\n')
        if opts.exiftool:
            out.write('\n[Metadata]\nExiftoolPath=exiftool\n')
    with open(os.path.join(opts.outdir,
                           'lib/gio/modules/giomodule.cache'), 'w') as out:
        out.write("""\
libgioremote-volume-monitor.so: gio-native-volume-monitor,gio-volume-monitor
libgvfsdbus.so: gio-vfs,gio-volume-monitor
""")        
    for name in ('ART', 'ART-cli'):
        shutil.move(os.path.join(opts.outdir, name),
                    os.path.join(opts.outdir, '.' + name + '.bin'))
    with open(os.path.join(opts.outdir, 'fonts.conf'), 'w') as out:
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
    with open(os.path.join(opts.outdir, 'ART'), 'w') as out:
        out.write("""#!/bin/bash
d=$(dirname $(readlink -f "$0"))

function mkdesktop() {
    if [ -f "$HOME/.config/ART/no-desktop" ]; then
        return
    fi
    fn="$HOME/.config/ART/ART.desktop"
    cat <<EOF > ${fn}
[Desktop Entry]
Version=1.0
Type=Application
Name=ART
Comment=raw image processor
Icon=${d}/share/icons/hicolor/256x256/apps/ART.png
Exec=${d}/ART %f
Actions=
Categories=Graphics;
StartupWMClass=ART
EOF
    lnk="$HOME/.local/share/applications/us.pixls.ART.desktop"
    if [ ! -f "$(readlink -f "${lnk}")" ]; then
        if zenity --question --text="Create a .desktop entry for ART?" --no-wrap; then
            rm -f "${lnk}"
            ln -s "${fn}" "${lnk}"
        else
            touch "$HOME/.config/ART/no-desktop"
        fi
    fi
}
mkdesktop
        
export ART_restore_GTK_CSD=$GTK_CSD
export ART_restore_GDK_PIXBUF_MODULE_FILE=$GDK_PIXBUF_MODULE_FILE
export ART_restore_GDK_PIXBUF_MODULEDIR=$GDK_PIXBUF_MODULEDIR
export ART_restore_GIO_MODULE_DIR=$GIO_MODULE_DIR
export ART_restore_LD_LIBRARY_PATH=$LD_LIBRARY_PATH
export ART_restore_FONTCONFIG_FILE=$FONTCONFIG_FILE
export ART_restore_GDK_BACKEND=$GDK_BACKEND     
export ART_restore_GTK_IM_MODULE_FILE=$GTK_IM_MODULE_FILE
export GTK_CSD=0

t=$(mktemp -d --suffix=-ART)
ln -s "$d" "$t/ART"
d="$t/ART"

export LD_LIBRARY_PATH="$d/lib"
"$d/lib/gdk-pixbuf-2.0/gdk-pixbuf-query-loaders" "$d/lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-png.so" "$d/lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-svg.so" > "$t/loader.cache"
"$d/lib/gtk-query-immodules-3.0" "$d"/lib/immodules/im-*.so > "$t/gtk.immodules"
export GDK_PIXBUF_MODULE_FILE="$t/loader.cache"
export GDK_PIXBUF_MODULEDIR="$d/lib/gdk-pixbuf-2.0"
export GTK_IM_MODULE_FILE="$t/gtk.immodules"
export GIO_MODULE_DIR="$d/lib/gio/modules"
export FONTCONFIG_FILE="$d/fonts.conf"
export ART_EXIFTOOL_BASE_DIR="$d/lib/exiftool"
GDK_BACKEND="${GDK_BACKEND:-wayland,x11}"
""")
        if not opts.debug:
            out.write('"$d/.ART.bin" "$@"\n')
        else:
            out.write("""\
gdb=$(which gdb)
if [ -x "$gdb" ]; then
    echo "set logging file /tmp/ART.log" > "$t/gdb"
    echo "set logging redirect on" >> "$t/gdb"
    echo "set logging on" >> "$t/gdb"
    echo "run" >> "$t/gdb"
    echo "thread apply all bt full" >> "$t/gdb"
    echo "quit" >> "$t/gdb"
    ${gdb} -batch -x "$t/gdb" "$d/.ART.bin" "$@"
else            
    "$d/.ART.bin" "$@"
fi
""")
        out.write('rm -rf "$t"\n')
            
    with open(os.path.join(opts.outdir, 'ART-cli'), 'w') as out:
        out.write("""#!/bin/bash
export ART_restore_GIO_MODULE_DIR=$GIO_MODULE_DIR
export ART_restore_LD_LIBRARY_PATH=$LD_LIBRARY_PATH
d=$(dirname $(readlink -f "$0"))
export GIO_MODULE_DIR="$d/lib/gio/modules"
export LD_LIBRARY_PATH="$d/lib"
export ART_EXIFTOOL_BASE_DIR="$d/lib/exiftool"
exec "$d/.ART-cli.bin" "$@"
""")
    for name in ('ART', 'ART-cli'):
        os.chmod(os.path.join(opts.outdir, name), 0o755)

if __name__ == '__main__':
    main()
