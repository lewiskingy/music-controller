"""Physical relationships that must hold when the enclosure geometry changes."""
import unittest
import cadquery as cq
from geometry import (parts, assembled_parts, case_to_dock, manufacturer_to_case,
                      dock, CONTACT_X, CONTACT_Z, H, DEPTH, DOCK_FLOOR, MAX_DEPTH, BATTERY_X, BATTERY_Y, box, SCREWS)


class PortraitGeometryTests(unittest.TestCase):
    def test_portrait_bounds_and_mounting_transform(self):
        bounds=parts['01_front'].val().BoundingBox()
        self.assertAlmostEqual(bounds.xlen,80,places=5)
        self.assertAlmostEqual(bounds.ylen,138,places=5)
        # Four official metal boss rear centres map onto portrait support tips.
        for x in (-48.95,49.05):
            for y in (-32.47,27.53):
                source=cq.Workplane('XY').sphere(.1).translate((x,y,-7.7)).val()
                actual=manufacturer_to_case(source).Center()
                self.assertAlmostEqual(abs(actual.x),30)
                self.assertTrue(any(abs(actual.y-v)<1e-6 for v in (-39,59)))
                self.assertAlmostEqual(actual.z,14.8)

    def test_battery_pouch_and_separate_parts_have_clearance(self):
        assembled=assembled_parts(include_insert=True)
        # Full assumed battery including allowance: no shell/tray/dock interference.
        battery=box(40,34,10,MAX_DEPTH-14,x=BATTERY_X,y=BATTERY_Y).val()
        for name,part in assembled.items():
            self.assertLess(battery.intersect(part.val()).Volume(),1e-4,name)
        self.assertLess(case_to_dock(battery).intersect(dock.val()).Volume(),1e-4)
        names=list(assembled)
        for i,name in enumerate(names):
            for other in names[i+1:]:
                self.assertLess(assembled[name].val().intersect(assembled[other].val()).Volume(),1e-4,
                                f'{name} vs {other}')
        self.assertAlmostEqual(assembled['back'].val().BoundingBox().zmax,MAX_DEPTH,places=5)

    def test_lower_screw_heads_clear_rear_cradle_wall(self):
        for x in (-36,36):
            head=(cq.Workplane('XZ').center(x,H/2+DOCK_FLOOR+min(y for _,y in SCREWS)).circle(3).extrude(-.7)
                  .translate((0,DEPTH/2,0)).val())
            self.assertLess(head.intersect(dock.val()).Volume(),1e-4)

    def test_contacts_align_with_dock_and_reversed_insertion_is_blocked(self):
        for x in CONTACT_X:
            source=cq.Workplane('XY').sphere(.1).translate((x,-H/2,CONTACT_Z))
            actual=case_to_dock(source).val().Center()
            self.assertAlmostEqual(actual.x,-x)
            self.assertAlmostEqual(actual.y,CONTACT_Z-DEPTH/2)
            self.assertAlmostEqual(actual.z,DOCK_FLOOR)
        normal=0; reversed_volume=0
        for part in assembled_parts(include_insert=True).values():
            pose=case_to_dock(part).val()
            normal+=pose.intersect(dock.val()).Volume()
            reversed_volume+=pose.rotate((0,0,0),(0,0,1),180).intersect(dock.val()).Volume()
        self.assertLess(normal,1e-4)
        self.assertGreater(reversed_volume,1)


if __name__=='__main__':unittest.main()
