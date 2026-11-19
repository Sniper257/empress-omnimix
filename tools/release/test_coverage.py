"""Maintainer static cadence matcher: BPM-only changes versus genuine variants."""
import unittest
from coverage_match import same


class Coverage(unittest.TestCase):
    def chart(self,bpm,scale=1.0):
        return dict(notes=[(round((i*i+3*i)*scale),0,i%7)for i in range(100)],bpms=[(0,bpm)],windows=[(0,0,1)])
    def test_one_bpm_on_slow_song(self):
        self.assertIsNotNone(same(self.chart(83),self.chart(84,83/84)))
    def test_one_bpm_metadata_only(self):
        self.assertIsNotNone(same(self.chart(179),self.chart(180)))
    def test_slower_safari_is_retained(self):
        self.assertIsNone(same(self.chart(150),self.chart(170,150/170)))
    def test_genuine_note_and_window_changes_retained(self):
        a=self.chart(150);b=self.chart(150);b['notes'][40]=(b['notes'][40][0],0,8)
        self.assertIsNone(same(a,b));b=self.chart(150);b['windows']=[(0,0,2)]
        self.assertIsNone(same(a,b))


if __name__=='__main__':unittest.main()
