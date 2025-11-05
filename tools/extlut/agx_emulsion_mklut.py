#!/usr/bin/env python3
#
# script to generate an ASC-CLF LUT by running the agx-emulsion film
# simulation pipeline (see https://github.com/andreavolpato/agx-emulsion)
#
# the script can be run standalone or integrated with ART
# (https://artraweditor.github.io) using its "external 3dLUT" interface

import os
os.environ['KMP_WARNINGS'] = 'off'

import numpy
import argparse
import gzip
import struct
import math
import sys
import json
import time
import io
import warnings
import copy
from scipy.optimize import least_squares
from contextlib import redirect_stdout, redirect_stderr
from agx_emulsion.model.process import photo_params, AgXPhoto
from agx_emulsion.model.stocks import FilmStocks, PrintPapers


def _enum(cls, *vals):
    res = []
    for v in vals:
        try:
            res.append(cls[v])
        except KeyError:
            pass
    return res

film_stocks = _enum(FilmStocks,
                    'kodak_portra_400',
                    'kodak_ultramax_400',
                    'kodak_gold_200',
                    'kodak_vision3_50d',
                    'fujifilm_pro_400h',
                    'fujifilm_xtra_400',
                    'fujifilm_c200',
                    'kodak_ektar_100',
                    'kodak_portra_160',
                    'kodak_portra_800',
                    'kodak_portra_800_push1',
                    'kodak_portra_800_push2',
                    'kodak_vision3_250d',
                    'kodak_vision3_200t',
                    'kodak_vision3_500t')
                    

print_papers = _enum(PrintPapers,
                     'kodak_endura_premier',
                     'kodak_ektacolor_edge',
                     'kodak_supra_endura',
                     'kodak_portra_endura',
                     'fujifilm_crystal_archive_typeii',
                     'kodak_2393',
                     'kodak_2383')


def getopts():
    p = argparse.ArgumentParser()
    p.add_argument('-o', '--output')
    p.add_argument('-O', '--outdir', default='.')
    p.add_argument('-s', '--size', choices=['small', 'medium', 'large', 'huge'],
                   default='medium')
    p.add_argument('-z', '--compressed', action='store_true')
    film_avail = f"Film stock to use. Options: " + \
        ", ".join(f'{i} : {s.name}' for (i, s) in enumerate(film_stocks))
    p.add_argument('-f', '--film', type=int, choices=range(len(film_stocks)),
                   help=film_avail, default=0)
    paper_avail = f"Print paper to use. Options: " + \
        ", ".join(f'{i} : {s.name}' for (i, s) in enumerate(print_papers))
    p.add_argument('-p', '--paper', type=int, choices=range(len(print_papers)),
                   default=0, help=paper_avail)
    p.add_argument('-e', '--camera-expcomp', type=float, default=0)
    p.add_argument('-E', '--print-exposure', type=float, default=1)
    p.add_argument('-g', '--input-gain', type=float, default=0)
    p.add_argument('--y-shift', type=float, default=0)
    p.add_argument('--m-shift', type=float, default=0)
    p.add_argument('--film-gamma', type=float, default=1)
    p.add_argument('--print-gamma', type=float, default=1)
    p.add_argument('--dir-couplers-amount', type=float, default=1)
    p.add_argument('--output-black-offset', type=float, default=0)
    p.add_argument('--gamut', choices=['srgb', 'rec2020'], default='rec2020')
    p.add_argument('--json', nargs=2)
    p.add_argument('--server', action='store_true')
    p.add_argument('--auto-ym-shifts', action='store_true')
    opts = p.parse_args()
    if opts.json:
        with open(opts.json[0]) as f:
            params = json.load(f)
        update_opts(opts, params, opts.json[1])
    if not opts.output:
        film = film_stocks[opts.film].name
        paper = print_papers[opts.paper].name
        name = f'{film}@{paper}.clf{"z" if opts.compressed else ""}'
        opts.output = os.path.join(opts.outdir, name)
    return opts


def update_opts(opts, params, output):
    opts.film = params.get("film", opts.film)
    opts.paper = params.get("paper", opts.paper)
    opts.camera_expcomp = params.get("camera_expcomp", opts.camera_expcomp)
    opts.print_exposure = params.get("print_exposure", opts.print_exposure)
    opts.input_gain = params.get("input_gain", opts.input_gain)
    opts.y_shift = params.get("y_shift", opts.y_shift)
    opts.m_shift = params.get("m_shift", opts.m_shift)
    opts.auto_ym_shifts = params.get("auto_ym_shifts", opts.auto_ym_shifts)
    opts.film_gamma = params.get("film_gamma", opts.film_gamma)
    opts.print_gamma = params.get("print_gamma", opts.print_gamma)
    opts.dir_couplers_amount = params.get("dir_couplers_amount",
                                          opts.dir_couplers_amount)
    opts.output_black_offset = params.get(
        "output_black_offset", opts.output_black_offset)
    opts.output = output


def srgb(a, inv):
    if not inv:
        a = numpy.fmax(numpy.fmin(a, 1.0), 0.0)
        return numpy.where(a <= 0.0031308,
                           12.92 * a,
                           1.055 * numpy.power(a, 1.0/2.4)-0.055)
    else:
        return numpy.where(a <= 0.04045, a / 12.92,
                           numpy.power((a + 0.055) / 1.055, 2.4))

def pq(a, inv):
    m1 = 2610.0 / 16384.0
    m2 = 2523.0 / 32.0
    c1 = 107.0 / 128.0
    c2 = 2413.0 / 128.0
    c3 = 2392.0 / 128.0
    scale = 100.0
    if not inv:
        # assume 1.0 is 100 nits, normalise so that 1.0 is 10000 nits
        a /= scale
        # apply the PQ curve
        aa = numpy.power(a, m1)
        res = numpy.power((c1 + c2 * aa)/(1.0 + c3 * aa), m2)
    else:
        p = numpy.power(a, 1.0/m2)
        aa = numpy.fmax(p-c1, 0.0) / (c2 - c3 * p)
        res = numpy.power(aa, 1.0/m1)
        res *= scale
    return res    



class LUTCreator:
    lutsize = {
        'small' : 16,
        'medium' : 36,
        'large' : 64,
        'huge' : 121
        }

    ap0_to_rec709 = """\
        <Matrix inBitDepth="32f" outBitDepth="32f" >
          <!-- ACES AP0 to Linear Rec.709 -->
          <Array dim="3 3">
            2.55128702 -1.11947013 -0.4318176
            -0.27586285  1.36601602 -0.09015301
            -0.01729251 -0.14852912  1.16582168
          </Array>
        </Matrix>   
    """.encode('utf-8')


    rec709_to_ap0 = """\
        <Matrix inBitDepth="32f" outBitDepth="32f" >
          <!-- Linear Rec.709 to ACES AP0-->
          <Array dim="3 3">
            0.43392843 0.3762503  0.18982151
            0.088802   0.81526168 0.09593625
            0.01775005 0.10944762 0.87280228
          </Array>
        </Matrix>   
    """.encode('utf-8')

    ap0_to_rec2020 = """\
        <Matrix inBitDepth="32f" outBitDepth="32f" >
          <!-- ACES AP0 to Rec2020-->
          <Array dim="3 3">
            1.50910172 -0.2589874  -0.2501146
            -0.07757638  1.17706684 -0.09949036
            0.0020526  -0.03114411  1.02909153
          </Array>
        </Matrix>   
    """.encode('utf-8')

    rec2020_to_ap0 = """\
        <Matrix inBitDepth="32f" outBitDepth="32f" >
          <!-- Linear Rec.2020 to ACES AP0-->
          <Array dim="3 3">
            0.67022657 0.15216775 0.17760585
            0.0441723  0.86177705 0.09405057
            0.0        0.02577705 0.97422293
          </Array>
        </Matrix>   
    """.encode('utf-8')

    def get_base_image(self, opts):
        dim = self.lutsize[opts.size]
        sz = complex(0, float(dim))
        table = numpy.mgrid[0.0:1.0:sz, 0.0:1.0:sz, 0.0:1.0:sz].reshape(3,-1).T
        n = int(math.sqrt(dim**3))
        data = table.reshape(-1)
        shaper = lambda a: pq(a, True)
        data = numpy.fromiter(map(shaper, data), dtype=numpy.float32)
        data = data.reshape(n, n, -1)
        return data

    def get_params(self, opts):
        params = photo_params(film_stocks[opts.film].value,
                              print_papers[opts.paper].value)
        params.camera.exposure_compensation_ev = opts.camera_expcomp
        params.enlarger.print_exposure = opts.print_exposure
        params.enlarger.lens_blur = 0
        params.scanner.lens_blur = 0
        if opts.gamut == 'srgb':
            params.io.input_color_space = 'sRGB'
            params.settings.rgb_to_raw_method = 'mallett2019'
        else:
            params.io.input_color_space = 'ITU-R BT.2020'
            params.settings.rgb_to_raw_method = 'hanatos2025'
        params.io.input_cctf_decoding = False
        params.io.output_color_space = 'ACES2065-1'
        params.io.output_cctf_encoding = False
        params.io.crop = False
        params.io.preview_resize_factor = 1.0
        params.io.upscale_factor = 1.0
        params.io.full_image = True
        params.io.compute_negative = False
        params.negative.grain.active = False
        params.negative.halation.active = False
        params.print_paper.glare.active = False
        params.negative.parametric.density_curves.active = False
        params.camera.auto_exposure = False
        params.camera.auto_exposure_method = 'median'
        params.enlarger.print_exposure_compensation = True
        params.debug.deactivate_spatial_effects = True
        params.negative.data.tune.gamma_factor = opts.film_gamma
        params.print_paper.data.tune.gamma_factor = opts.print_gamma
        params.enlarger.y_filter_shift = opts.y_shift
        params.enlarger.m_filter_shift = opts.m_shift
        params.negative.dir_couplers.amount = opts.dir_couplers_amount
        params.negative.dir_couplers.active = opts.dir_couplers_amount > 0
        params.settings.use_scanner_lut = False
        params.settings.use_enlarger_lut = False
        params.settings.use_camera_lut = False
        
        if opts.auto_ym_shifts:
            key = self._key('autoshifts', opts)
            res = self.cache.get(key)
            if res is not None:
                y_shift, m_shift = res
            else:
                image = numpy.array([[
                    [0.184, 0.184, 0.184],
                ]])

                par = copy.copy(params)
                par.debug.return_negative_density_cmy = True

                photo = AgXPhoto(par)
                density_cmy = photo.process(image)

                def sqr(x): return x*x

                def func(x):
                    y_shift, m_shift = x
                    photo.enlarger.y_filter_shift = y_shift
                    photo.enlarger.m_filter_shift = m_shift
                    log_raw = photo._expose_print(density_cmy)
                    print_cmy = photo._develop_print(log_raw)
                    out = photo._scan(print_cmy)
                    r, g, b = out.flatten()
                    return (abs(b-g), abs(r-g), abs(r-b))

                start = time.time()
                res = least_squares(func, [0.0, 0.0],
                                    method='dogbox',
                                    bounds=[(-10, -10), (10, 10)],
                                    max_nfev=20)
                y_shift, m_shift = round(res.x[0], 3), round(res.x[1], 3)
                end = time.time()
                print(f'least_squares: {round(end - start, 2)}, '
                      f'y_shift: {y_shift}, m_shift: {m_shift}')

                self.cache[key] = (y_shift, m_shift)
                
            params.enlarger.y_filter_shift = y_shift + opts.y_shift
            params.enlarger.m_filter_shift = m_shift + opts.m_shift
        
        return params


    def __init__(self, opts):
        with warnings.catch_warnings():
            warnings.simplefilter('ignore')
            self.image = self.get_base_image(opts)
            self.shaper = self.get_shaper()
        self.cache = {}

    def _key(self, step, opts):
        keys = {
            'film' : ['film',
                      'camera_expcomp',
                      'film_gamma',
                      'dir_couplers_amount'],
            'full' : ['film',
                      'camera_expcomp',
                      'film_gamma',
                      'dir_couplers_amount',
                      'paper',
                      'print_exposure',
                      'y_shift',
                      'm_shift',
                      'print_gamma',
                      'auto_ym_shifts'],
            'autoshifts' : ['film',
                            'camera_expcomp',
                            'film_gamma',
                            'dir_couplers_amount',
                            'paper',
                            'print_exposure',
                            'print_gamma'],
        }
        d = {'step' : step}
        for k in keys[step]:
            d[k] = getattr(opts, k)
        return json.dumps(d)

    def _get(self, step, opts, photo, image):
        k = self._key(step, opts)
        res = self.cache.get(k)
        if res is None and step == 'film':
            photo.debug.return_negative_density_cmy = True
            res = photo.process(image)
            self.cache[k] = res
        return res

    def __call__(self, opts):
        start = time.time()
        params = self.get_params(opts)
        photo = AgXPhoto(params)
        def identity(rgb, *args, **kwds): return rgb
        photo.print_paper._apply_cctf_encoding_and_clip = identity
        image = self._get('full', opts, photo, self.image)
        if image is None:
            image = self._get('film', opts, photo, self.image)
            log_raw = photo._expose_print(image)
            density_cmy = photo._develop_print(log_raw)
            image = photo._scan(density_cmy)
            self.cache[self._key('full', opts)] = image
        #image = photo.process(self.image)
        self.make_lut(opts, image)
        end = time.time()
        sys.stderr.write('total time: %.3f\n' % (end - start))

    def get_shaper(self):
        f = io.BytesIO()
        f.write(b'<LUT1D inBitDepth="32f" outBitDepth="32f" '
                b'halfDomain="true" rawHalfs="true">\n')
        f.write(b'  <Array dim="65536 1">\n')
        for i in range(65536):
            v = struct.unpack('e', struct.pack('H', i))[0]
            if math.isfinite(v) and v >= 0:
                o = pq(v, False)
            else:
                o = 0.0
            j = struct.unpack('H', struct.pack('e', o))[0]
            f.write(f'    {j}\n'.encode('utf-8'))
        f.write(b'  </Array>\n')
        f.write(b'</LUT1D>\n')            
        return f.getvalue()

    def make_lut(self, opts, data):
        data = data.reshape(-1, 3)
        dim = int(round(math.pow(data.shape[0], 1.0/3.0)))
        fopen = open if not opts.compressed else gzip.open
        with fopen(opts.output, 'wb') as f:
            f.write(b'<?xml version="1.0" encoding="UTF-8"?>\n')
            f.write(b'<ProcessList compCLFversion="3" id="1">\n')
            if opts.input_gain:
                f.write(b'<ASC_CDL inBitDepth="32f" outBitDepth="32f" '
                        b'style="FwdNoClamp">\n')
                f.write(b' <SOPNode>\n')
                g = math.pow(2, opts.input_gain)
                f.write(f'  <Slope>{g} {g} {g}</Slope>\n'.encode('utf-8'))
                f.write(b'  <Offset>0.0 0.0 0.0</Offset>\n')
                f.write(b'  <Power>1.0 1.0 1.0</Power>\n')
                f.write(b' </SOPNode>\n')
                f.write(b'</ASC_CDL>\n')
            if opts.gamut == 'srgb':
                f.write(self.ap0_to_rec709)
            else:
                f.write(self.ap0_to_rec2020)                
            f.write(self.shaper)
            f.write(b'<LUT3D inBitDepth="32f" outBitDepth="32f" '
                    b'interpolation="tetrahedral">\n')
            f.write(f'  <Array dim="{dim} {dim} {dim} 3">\n'.encode('utf-8'))
            for rgb in data:
                f.write(('    %.8f  %.8f  %.8f\n' %
                         tuple(rgb)).encode('utf-8'))
            f.write(b'  </Array>\n')
            f.write(b'</LUT3D>\n')
            if opts.output_black_offset:
                f.write(b'<ASC_CDL inBitDepth="32f" outBitDepth="32f" '
                        b'style="FwdNoClamp">\n')
                f.write(b' <SOPNode>\n')
                bl = opts.output_black_offset * 2000.0 / 65535.0
                f.write(b'  <Slope>1.0 1.0 1.0</Slope>\n')
                f.write(f'  <Offset>{bl} {bl} {bl}</Offset>\n'.encode('utf-8'))
                f.write(b'  <Power>1.0 1.0 1.0</Power>\n')
                f.write(b' </SOPNode>\n')
                f.write(b'</ASC_CDL>\n')
            f.write(b'</ProcessList>\n')

# end of class LUTCreator


def main():
    opts = getopts()
    process = LUTCreator(opts)
    if opts.server:
        while True:
            p = sys.stdin.readline().strip()
            o = sys.stdin.readline().strip()
            with open(p) as f:
                params = json.load(f)
            update_opts(opts, params, o)
            buf = io.StringIO()
            with redirect_stdout(buf):
                with redirect_stderr(buf):
                    process(opts)
            data = buf.getvalue().splitlines()
            sys.stdout.write(f'Y {len(data)}\n')
            for line in data:
                sys.stdout.write(f'{line}\n')
            sys.stdout.flush()
    else:
        process(opts)


if __name__ == '__main__':
    main()
