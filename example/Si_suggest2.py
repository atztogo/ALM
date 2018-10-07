#
#  Si_suggest.py
#
#  This is an example to run ALM in the suggest mode.
#

from alm import ALM
import numpy as np

lavec = [[20.406, 0, 0],
         [0, 20.406, 0],
         [0, 0, 20.406]]
xcoord = [[ 0.0000000000000000, 0.0000000000000000, 0.0000000000000000],
          [ 0.0000000000000000, 0.0000000000000000, 0.5000000000000000],
          [ 0.0000000000000000, 0.2500000000000000, 0.2500000000000000],
          [ 0.0000000000000000, 0.2500000000000000, 0.7500000000000000],
          [ 0.0000000000000000, 0.5000000000000000, 0.0000000000000000],
          [ 0.0000000000000000, 0.5000000000000000, 0.5000000000000000],
          [ 0.0000000000000000, 0.7500000000000000, 0.2500000000000000],
          [ 0.0000000000000000, 0.7500000000000000, 0.7500000000000000],
          [ 0.1250000000000000, 0.1250000000000000, 0.1250000000000000],
          [ 0.1250000000000000, 0.1250000000000000, 0.6250000000000000],
          [ 0.1250000000000000, 0.3750000000000000, 0.3750000000000000],
          [ 0.1250000000000000, 0.3750000000000000, 0.8750000000000000],
          [ 0.1250000000000000, 0.6250000000000000, 0.1250000000000000],
          [ 0.1250000000000000, 0.6250000000000000, 0.6250000000000000],
          [ 0.1250000000000000, 0.8750000000000000, 0.3750000000000000],
          [ 0.1250000000000000, 0.8750000000000000, 0.8750000000000000],
          [ 0.2500000000000000, 0.0000000000000000, 0.2500000000000000],
          [ 0.2500000000000000, 0.0000000000000000, 0.7500000000000000],
          [ 0.2500000000000000, 0.2500000000000000, 0.0000000000000000],
          [ 0.2500000000000000, 0.2500000000000000, 0.5000000000000000],
          [ 0.2500000000000000, 0.5000000000000000, 0.2500000000000000],
          [ 0.2500000000000000, 0.5000000000000000, 0.7500000000000000],
          [ 0.2500000000000000, 0.7500000000000000, 0.0000000000000000],
          [ 0.2500000000000000, 0.7500000000000000, 0.5000000000000000],
          [ 0.3750000000000000, 0.1250000000000000, 0.3750000000000000],
          [ 0.3750000000000000, 0.1250000000000000, 0.8750000000000000],
          [ 0.3750000000000000, 0.3750000000000000, 0.1250000000000000],
          [ 0.3750000000000000, 0.3750000000000000, 0.6250000000000000],
          [ 0.3750000000000000, 0.6250000000000000, 0.3750000000000000],
          [ 0.3750000000000000, 0.6250000000000000, 0.8750000000000000],
          [ 0.3750000000000000, 0.8750000000000000, 0.1250000000000000],
          [ 0.3750000000000000, 0.8750000000000000, 0.6250000000000000],
          [ 0.5000000000000000, 0.0000000000000000, 0.0000000000000000],
          [ 0.5000000000000000, 0.0000000000000000, 0.5000000000000000],
          [ 0.5000000000000000, 0.2500000000000000, 0.2500000000000000],
          [ 0.5000000000000000, 0.2500000000000000, 0.7500000000000000],
          [ 0.5000000000000000, 0.5000000000000000, 0.0000000000000000],
          [ 0.5000000000000000, 0.5000000000000000, 0.5000000000000000],
          [ 0.5000000000000000, 0.7500000000000000, 0.2500000000000000],
          [ 0.5000000000000000, 0.7500000000000000, 0.7500000000000000],
          [ 0.6250000000000000, 0.1250000000000000, 0.1250000000000000],
          [ 0.6250000000000000, 0.1250000000000000, 0.6250000000000000],
          [ 0.6250000000000000, 0.3750000000000000, 0.3750000000000000],
          [ 0.6250000000000000, 0.3750000000000000, 0.8750000000000000],
          [ 0.6250000000000000, 0.6250000000000000, 0.1250000000000000],
          [ 0.6250000000000000, 0.6250000000000000, 0.6250000000000000],
          [ 0.6250000000000000, 0.8750000000000000, 0.3750000000000000],
          [ 0.6250000000000000, 0.8750000000000000, 0.8750000000000000],
          [ 0.7500000000000000, 0.0000000000000000, 0.2500000000000000],
          [ 0.7500000000000000, 0.0000000000000000, 0.7500000000000000],
          [ 0.7500000000000000, 0.2500000000000000, 0.0000000000000000],
          [ 0.7500000000000000, 0.2500000000000000, 0.5000000000000000],
          [ 0.7500000000000000, 0.5000000000000000, 0.2500000000000000],
          [ 0.7500000000000000, 0.5000000000000000, 0.7500000000000000],
          [ 0.7500000000000000, 0.7500000000000000, 0.0000000000000000],
          [ 0.7500000000000000, 0.7500000000000000, 0.5000000000000000],
          [ 0.8750000000000000, 0.1250000000000000, 0.3750000000000000],
          [ 0.8750000000000000, 0.1250000000000000, 0.8750000000000000],
          [ 0.8750000000000000, 0.3750000000000000, 0.1250000000000000],
          [ 0.8750000000000000, 0.3750000000000000, 0.6250000000000000],
          [ 0.8750000000000000, 0.6250000000000000, 0.3750000000000000],
          [ 0.8750000000000000, 0.6250000000000000, 0.8750000000000000],
          [ 0.8750000000000000, 0.8750000000000000, 0.1250000000000000],
          [ 0.8750000000000000, 0.8750000000000000, 0.6250000000000000]]

kd = [14] * 64

# alm.alm_new() and alm.alm_delete() are done by 'with' statement
with ALM(lavec, xcoord, kd) as alm:
    alm.define(2, [-1, 7.3])
    #alm.define(2)
    alm.suggest()
    print(alm.getmap_primitive_to_supercell())
    for fc_order in (1, 2):
        print("fc_order: %d" % fc_order)
        for d in alm.get_displacement_patterns(fc_order):
            print(d)
