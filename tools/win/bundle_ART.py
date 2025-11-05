#!/usr/bin/env python3

"""
Helper script to generate an ART self-contained "bundle" directory,
with all the required dependencies (windows version)
"""

import os, sys
import shutil
import subprocess
import argparse
import tarfile, zipfile
import tempfile
import io
from urllib.request import urlopen, Request
import json
import time


def getopts():
    p = argparse.ArgumentParser()
    p.add_argument('-o', '--outdir', required=True,
                   help='output directory for the bundle')
    p.add_argument('-m', '--msys', default='c:/msys64',
                   help='msys installation directory (default: c:/msys64)')
    p.add_argument('-e', '--exiftool',
                   help='path to exiftool.exe (default: search in PATH)')
    p.add_argument('-v', '--verbose', action='store_true')
    p.add_argument('-E', '--exiftool-download', action='store_true')
    p.add_argument('--exiftool-version')
    p.add_argument('-i', '--imageio', help='path to imageio plugins')
    p.add_argument('-b', '--imageio-bin', help='path to imageio binaries')
    p.add_argument('-I', '--imageio-download', action='store_true')
    ret = p.parse_args()
    if ret.exiftool is None and not ret.exiftool_download:
        ret.exiftool = shutil.which('exiftool')
    if ret.msys.endswith('/'):
        ret.msys = ret.msys[:-1]
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
    res = []
    d = os.getcwd()
    p = subprocess.Popen(['ldd', os.path.join(d, 'ART.exe')],
                         stdout=subprocess.PIPE)
    out, _ = p.communicate()
    for line in out.decode('utf-8').splitlines():
        if ' => ' in line:
            bits = line.split()
            lib = bits[2]
            if not lib.lower().startswith('/c/windows/'):
                res.append(opts.msys + lib)
    return res


def extra_files(opts, msys_env, tempdir):
    assert msys_env is not None
    assert tempdir is not None
    def D(s): return opts.msys + '/' + msys_env + '/' + s
    exiftool = []
    extra = []
    if not opts.exiftool and opts.exiftool_download:
        if opts.exiftool_version:
            ver = opts.exiftool_version
        else:
            with urlopen('https://exiftool.org/ver.txt') as f:
                ver = f.read().strip().decode('utf-8')
        name = 'exiftool-%s_64' % ver
        with urlopen('https://exiftool.org/' + name + '.zip') as f:
            if opts.verbose:
                print('downloading %s.zip from https://exiftool.org ...' % name)
            zf = zipfile.ZipFile(io.BytesIO(f.read()))
            if opts.verbose:
                print('unpacking %s.zip ...' % name)
            zf.extractall(tempdir)
            opts.exiftool = os.path.join(tempdir, name, 'exiftool(-k).exe')
    if opts.exiftool:
        exiftool.append((opts.exiftool, 'exiftool.exe'))
        exiftool_files = os.path.join(os.path.dirname(opts.exiftool),
                                      'exiftool_files')
        if os.path.isdir(exiftool_files):
            exiftool.append((exiftool_files, 'exiftool_files'))
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
            tf.extractall(tempdir)
        extra.append(('.', [(os.path.join(tempdir, 'ART-imageio'), 'imageio')]))
    if opts.imageio_bin:
        extra.append(('imageio', [(opts.imageio_bin, 'bin')]))            
    elif opts.imageio_download:
        with urlopen(imageio.asset('ART-imageio-bin-win64.tar.gz')) as f:
            if opts.verbose:
                print('downloading ART-imageio-bin-win64.tar.gz '
                      'from GitHub ...')
            tf = tarfile.open(fileobj=io.BytesIO(f.read()))
            if opts.verbose:
                print('unpacking ART-imageio-bin-win64.tar.gz ...')
            tf.extractall(tempdir)
        extra.append(('imageio',
                      [(os.path.join(tempdir, 'ART-imageio-bin-win64'), 'bin')]))
    return [
        ('.', [
            D('bin/gdbus.exe'),
            D('bin/gspawn-win64-helper.exe'),
            D('bin/gspawn-win64-helper-console.exe')
        ] + exiftool),
        ('share/icons/Adwaita', [
            D('share/icons/Adwaita/scalable'),
            D('share/icons/Adwaita/index.theme'), 
            D('share/icons/Adwaita/cursors'),
        ]),
        ('lib', [
            D('lib/gdk-pixbuf-2.0'),
        ]),
        ('share/glib-2.0/schemas', [
            D('share/glib-2.0/schemas/gschemas.compiled'),
        ]),
        ('share', [
            (D('var/lib/lensfun-updates/version_1'), 'lensfun'),
        ]),
    ] + extra


def main():
    opts = getopts()
    d = os.getcwd()
    if not os.path.exists('ART.exe'):
        sys.stderr.write('ERROR: ART.exe not found! Please run this script '
                         'from the build directory of ART\n')
        sys.exit(1)
    if opts.verbose:
        print('copying %s to %s' % (os.getcwd(), opts.outdir))
    shutil.copytree(d, opts.outdir)
    msys_env = None
    for lib in getdlls(opts):
        if opts.verbose:
            print('copying: %s' % lib)
        bn = os.path.basename(lib)
        if msys_env is None and lib.startswith(opts.msys) \
           and bn.startswith('libgtk'):
            msys_env = lib[len(opts.msys) + 1:].split('/')[0]
        shutil.copy2(lib, os.path.join(opts.outdir, bn))
    if opts.verbose:
        print('detected msys environment: ' + str(msys_env))
    with tempfile.TemporaryDirectory() as d:
        for key, elems in extra_files(opts, msys_env, d):
            for elem in elems:
                name = None
                if isinstance(elem, tuple):
                    elem, name = elem
                else:
                    name = os.path.basename(elem)
                if opts.verbose:
                    print('copying: %s' % elem)
                if os.path.isdir(elem):
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


if __name__ == '__main__':
    main()
