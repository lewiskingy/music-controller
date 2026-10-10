"""Build printable CAD, validate exported meshes, render and package outputs."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import shutil
import urllib.request
import zipfile
import cadquery as cq
import numpy as np
from geometry import parts, assembled_parts
from render import read_stl, render_all

ROOT=Path(__file__).resolve().parent
URL='https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3/ESP32-S3-Touch-LCD-4in3_3D_Drawing.zip'
DIGEST='8721fac43de47b35771c438829a3251a4c0723227d83696b5517efe7d854de15'


def check_mesh(path):
    triangles=read_stl(path)
    ids={}; edges=Counter()
    for triangle in np.round(triangles,5):
        points=[ids.setdefault(tuple(p),len(ids)) for p in triangle]
        if len(set(points))!=3: raise ValueError(f'{path}: degenerate face')
        for i,j in ((0,1),(1,2),(2,0)): edges[tuple(sorted((points[i],points[j])))]+=1
    invalid=sum(count!=2 for count in edges.values())
    if invalid: raise ValueError(f'{path}: {invalid} non-manifold edges')
    return {'triangles':len(triangles),'non_manifold_edges':invalid,'bounds_mm':[triangles.min((0,1)).tolist(),triangles.max((0,1)).tolist()]}


def check_board(output,archive=None):
    if archive:
        data=Path(archive).read_bytes()
    else:
        with urllib.request.urlopen(URL,timeout=120) as response: data=response.read()
    if hashlib.sha256(data).hexdigest()!=DIGEST: raise ValueError('Manufacturer archive checksum changed; review model before updating hash')
    import io
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        model_path=output/'manufacturer.stp'
        model_path.write_bytes(z.read('esp32-s3-touch-lcd-4_3.stp'))
    print('Importing verified manufacturer STEP model (may take several minutes)',flush=True)
    model=cq.importers.importStep(str(model_path)).val().mirror('XY').translate((-.05,2.47,7.1))
    results={}
    for name,part in assembled_parts().items():
        volume=part.val().intersect(model).Volume()
        results[name]=round(volume,6)
        print(f'{name}: collision volume {volume:.6f} mm3',flush=True)
        if volume>1e-4: raise ValueError(f'{name} collides with board: {volume} mm3')
    model_path.unlink()
    (output/'collision_check.json').write_text(json.dumps(results,indent=2)+'\n')
    return results


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build')
    parser.add_argument('--check-board',action='store_true')
    parser.add_argument('--board-archive',type=Path,help='Use downloaded official ZIP instead of network')
    args=parser.parse_args(); out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    # Remove only generated outputs from previous runs, so stale checks cannot enter a package.
    for name in ('collision_check.json','manufacturer.stp'):
        (out/name).unlink(missing_ok=True)
    checks={}
    for name,part in sorted(parts.items()):
        shape=part.val()
        if not shape.isValid() or len(shape.Solids())!=1: raise ValueError(f'{name}: invalid or disconnected CAD')
        cq.exporters.export(part,str(out/(name+'.stl')),tolerance=.05,angularTolerance=.15)
        cq.exporters.export(part,str(out/(name+'.step')))
        checks[name+'.stl']=check_mesh(out/(name+'.stl'))
        print(f'{name}: solid and mesh checks passed',flush=True)
    assembly=cq.Compound.makeCompound([p.val() for p in assembled_parts().values()])
    cq.exporters.export(assembly,str(out/'case_assembly.step'))
    (out/'mesh_check.json').write_text(json.dumps(checks,indent=2)+'\n')
    if args.check_board or args.board_archive: check_board(out,args.board_archive)
    render_all(out)
    for name in ('ASSEMBLY.md','README.md','geometry.py','build.py','render.py','requirements.txt'):
        shutil.copyfile(ROOT/name,out/name)
    (out/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in sorted(out.iterdir()) if p.is_file() and p.name not in ('SHA256SUMS','print-package.zip','print-package.zip.sha256')))
    with zipfile.ZipFile(out/'print-package.zip','w',zipfile.ZIP_DEFLATED) as archive:
        for p in sorted(out.iterdir()):
            if p.is_file() and p.name not in ('print-package.zip','print-package.zip.sha256'):archive.write(p,'music-controller-enclosure/'+p.name)
    (out/'print-package.zip.sha256').write_text(f"{hashlib.sha256((out/'print-package.zip').read_bytes()).hexdigest()}  print-package.zip\n")
    print(f'Built and validated {len(parts)} parts; renders and print package: {out}',flush=True)

if __name__=='__main__':main()
