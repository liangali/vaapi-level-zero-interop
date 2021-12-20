import os
import cv2
import numpy as np

def run_cmd(cmd):
    print('#'*16, cmd)
    os.system(cmd)

def dump_surface(w, h, infile):
    filename = infile.split('/')[-1]
    rgbp = np.fromfile(infile, dtype=np.uint8, count=w*h*3).reshape((3, h, w))
    bgr = cv2.cvtColor(rgbp.transpose((1, 2, 0)), cv2.COLOR_RGB2BGR)
    cv2.imwrite('%s.bmp'%infile, bgr)
    print(rgbp.shape, bgr.shape)

run_cmd('cd ../build && cmake ../valz')
run_cmd('cd ../build && make')
run_cmd('rm copy_surface_XE_HPG_COREdg2.*')
run_cmd('ocloc -file copy_surface.cl -device dg2')
run_cmd('cd ../build && ./valz')
dump_surface(224, 224, '../build/lz_img_out.yuv')