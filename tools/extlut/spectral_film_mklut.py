import sys
import io
import warnings
import argparse
import json
from contextlib import redirect_stdout, redirect_stderr

with warnings.catch_warnings(action='ignore'):
    from spectral_film_lut.utils import create_lut
    from spectral_film_lut import FILMSTOCKS

    films = sorted(f for f in FILMSTOCKS if FILMSTOCKS[f]().stage == 'camera')
    papers = sorted(f for f in FILMSTOCKS
                    if FILMSTOCKS[f]().stage == 'print') + ['None']
    FILMSTOCKS['None'] = lambda : None

def getopts():
    p = argparse.ArgumentParser()
    p.add_argument('--server', action='store_true')
    p.add_argument('--film', choices=films, default=films[0])
    p.add_argument('--paper', choices=papers, default=papers[0])
    p.add_argument('--exposure', type=float, default=0)
    p.add_argument('--wb', type=int, default=6500)
    p.add_argument('--tint', type=float, default=0)
    p.add_argument('--red-light', type=float, default=0)
    p.add_argument('--green-light', type=float, default=0)
    p.add_argument('--blue-light', type=float, default=0)
    p.add_argument('--projector-wb', type=int, default=6500)
    p.add_argument('--white-point', type=float, default=1)
    p.add_argument('--sat', type=float, default=1)
    p.add_argument('--black-offset', type=float, default=0)
    p.add_argument('params', nargs='?')
    p.add_argument('output', nargs='?')
    return p.parse_args()


def update_params(params, fname):
    with open(fname) as f:
        params.update(json.load(f))
        if isinstance(params['film'], int):
            params['film'] = films[params['film']]
        if isinstance(params['paper'], int):
            params['paper'] = papers[params['paper']]
    return params


def get_params(opts):
    params = {
        'film' : opts.film,
        'paper' : opts.paper,
        'exposure' : opts.exposure,
        'wb' : opts.wb,
        'tint' : opts.tint,
        'red_light' : opts.red_light,
        'green_light' : opts.green_light,
        'blue_light' : opts.blue_light,
        'projector_wb' : opts.projector_wb,
        'white_point' : opts.white_point,
        'sat' : opts.sat,
        'black_offset' : opts.black_offset,
    }
    if opts.params:
        params = update_params(params, opts.params)
    return params


def mklut(params, output):
    lut = create_lut(FILMSTOCKS[params['film']](),
                     FILMSTOCKS[params['paper']](),
                     lut_size=33,
                     cube=False,
                     input_colourspace='ACEScct',
                     output_colourspace='ACES2065-1',
                     projector_kelvin=params['projector_wb'],
                     exp_comp=params['exposure'],
                     white_point=params['white_point'],
                     exposure_kelvin=params['wb'],
                     mode='full',
                     red_light=params['red_light'],
                     green_light=params['green_light'],
                     blue_light=params['blue_light'],
                     black_offset=params['black_offset'],
                     color_masking=1,
                     tint=params['tint'],
                     sat_adjust=params['sat'],
                     matrix_method=False,
                     )
    table = lut.reshape((-1, 3))
    with open(output, 'w') as out:
        out.write("""\
<?xml version="1.0" encoding="UTF-8"?>
<ProcessList compCLFversion="3" id="1">
    <Matrix inBitDepth="32f" outBitDepth="32f">
        <Array dim="3 3">
   1.45143931614567   -0.23651074689374  -0.214928569251925
-0.0765537733960206    1.17622969983357 -0.0996759264375522
0.00831614842569772 -0.00603244979102103    0.997716301365323
        </Array>
    </Matrix>
    <Log inBitDepth="32f" outBitDepth="32f" style="cameraLinToLog">
        <LogParams base="2" linSideSlope="1" linSideOffset="0" logSideSlope="0.0570776255707763" logSideOffset="0.554794520547945" linSideBreak="0.0078125" />
    </Log>
    <LUT3D inBitDepth="32f" outBitDepth="32f" interpolation="tetrahedral">
        <Array dim="33 33 33 3">
""")
        for row in table:
            out.write(f'{row[0]:.7f} {row[1]:.7f} {row[2]:.7f}\n')
        out.write("""\
        </Array>
    </LUT3D>
</ProcessList>
""")


def main():
    opts = getopts()
    params = get_params(opts)
    if opts.server:
        while True:
            p = sys.stdin.readline().strip()
            o = sys.stdin.readline().strip()
            params = update_params(params, p)
            buf = io.StringIO()
            with redirect_stdout(buf):
                with redirect_stderr(buf):
                    mklut(params, o)
                    print(f'lut for {params} created in {o}')
            data = buf.getvalue().splitlines()
            sys.stdout.write(f'Y {len(data)}\n')
            for line in data:
                sys.stdout.write(f'{line}\n')
            sys.stdout.flush()
    else:
        mklut(params, opts.output)
        

if __name__ == '__main__':
    with warnings.catch_warnings(action='ignore'):
        main()
