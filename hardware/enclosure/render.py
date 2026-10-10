"""Headless orthographic PNG renders with a software depth buffer (no GPU)."""
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw


def read_stl(path):
    raw = Path(path).read_bytes()
    count = int.from_bytes(raw[80:84], 'little')
    dtype = np.dtype([('normal', '<f4', (3,)), ('vertices', '<f4', (3, 3)), ('attribute', '<u2')])
    return np.frombuffer(raw, dtype=dtype, count=count, offset=84)['vertices'].astype(float)


def rectangle(x0, x1, y0, y1, z):
    return np.array([[[x0,y0,z],[x1,y0,z],[x1,y1,z]], [[x0,y0,z],[x1,y1,z],[x0,y1,z]]])


def rasterize(items, eye, size=(1000, 850)):
    """Triangles share one depth buffer; even coplanar STL faces render correctly."""
    eye = np.array(eye, dtype=float); eye /= np.linalg.norm(eye)
    right = np.cross([0, 0, 1], eye); right /= np.linalg.norm(right)
    up = np.cross(eye, right)
    matrix = np.array([right, up, eye]).T
    meshes = [(vertices @ matrix, colour) for vertices, colour in items]
    bounds = np.concatenate([v.reshape(-1, 3) for v, _ in meshes])
    lo, hi = bounds.min(0), bounds.max(0)
    width, height = size
    scale = min((width-100)/(hi[0]-lo[0]), (height-100)/(hi[1]-lo[1]))
    centre = (hi+lo)/2
    pixels = np.full((height,width,3), [238,240,243], dtype=np.uint8)
    depth = np.full((height,width), -np.inf)
    light = np.array([-.3,.6,.74]); light /= np.linalg.norm(light)
    for vertices, colour in meshes:
        coords = vertices.copy()
        coords[:,:,0] = (coords[:,:,0]-centre[0])*scale + width/2
        coords[:,:,1] = height/2 - (coords[:,:,1]-centre[1])*scale
        normal = np.cross(vertices[:,1]-vertices[:,0], vertices[:,2]-vertices[:,0])
        normal /= np.maximum(np.linalg.norm(normal,axis=1)[:,None],1e-12)
        shades = .55+.45*np.maximum(normal @ light, 0)
        for tri, shade in zip(coords, shades):
            x0=max(0,int(np.floor(tri[:,0].min()))); x1=min(width-1,int(np.ceil(tri[:,0].max())))
            y0=max(0,int(np.floor(tri[:,1].min()))); y1=min(height-1,int(np.ceil(tri[:,1].max())))
            if x1<x0 or y1<y0: continue
            x,y=np.meshgrid(np.arange(x0,x1+1)+.5,np.arange(y0,y1+1)+.5)
            a,b,c=tri
            den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(den)<1e-10: continue
            u=((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/den
            v=((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/den
            w=1-u-v; z=u*a[2]+v*b[2]+w*c[2]
            section=depth[y0:y1+1,x0:x1+1]
            mask=(u>=-1e-8)&(v>=-1e-8)&(w>=-1e-8)&(z>section)
            section[mask]=z[mask]
            pixels[y0:y1+1,x0:x1+1][mask]=np.clip(np.array(colour)*shade,0,255).astype(np.uint8)
    return Image.fromarray(pixels)


def render_all(output):
    output=Path(output); body=[]
    for name in ('01_front','02_rear_extension','03_back'):
        a=read_stl(output/(name+'.stl'))
        if name=='02_rear_extension': a[:,:,2]+=23
        if name=='03_back': a[:,:,1]*=-1; a[:,:,2]=39-a[:,:,2]
        body.append((a, [105,133,153]))
    body.append((rectangle(-53.05,53.05,-33.9,33.9,2.3), [18,25,33]))
    body.append((rectangle(-47.7,47.7,-24.78,29.58,2.29), [24,38,49]))
    views=[]
    for docked in (False,True):
        items=[]
        for vertices,colour in body:
            a=vertices.copy()
            if docked: a=np.stack((a[:,:,0],a[:,:,2]-19.5,a[:,:,1]+46),axis=2)
            else: a[:,:,2]=39-a[:,:,2]
            items.append((a,colour))
        if docked: items.append((read_stl(output/'08_dock.stl'), [105,133,153]))
        img=rasterize(items, [.6,-1,.45] if docked else [.65,-1,1.25])
        ImageDraw.Draw(img).text((35,20),'Controller in dock' if docked else 'Controller case',fill='#303a44')
        img.save(output/('docked.png' if docked else 'case.png'))
        views.append(img)
    canvas=Image.new('RGB',(2000,900),'#eef0f3')
    for i,img in enumerate(views):canvas.paste(img,(i*1000,25))
    ImageDraw.Draw(canvas).text((35,875),'Actual CAD geometry; colour/finish indicative. Screen switched off.',fill='#53616b')
    canvas.save(output/'assembled.png')
    items=[(read_stl(output/(name+'.stl')), [105,133,153]) for name in ('01_front','02_rear_extension','03_back')]
    # Explode along the case depth, with correct assembly orientation.
    items[1][0][:,:,2]+=32
    items[2][0][:,:,1]*=-1; items[2][0][:,:,2]=60-items[2][0][:,:,2]
    for vertices,_ in items:vertices[:,:,2]*=-1
    rasterize(items,[.65,-1,1.2]).save(output/'exploded.png')
