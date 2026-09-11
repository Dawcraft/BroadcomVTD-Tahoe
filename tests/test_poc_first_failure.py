import importlib.util
import pathlib
import struct
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('decoder',ROOT/'tools/decode_poc.py')
d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d)

class FirstFailureDecoder(unittest.TestCase):
    def blob(self, flags=18, typ=61, frames=2):
        b=bytearray(d.HEADER.pack(0x3154434152545642,10,200,32768,6,0,0,0,0,1,2,bytes(16)))
        b+=bytes((32768+65+1025)*200)
        p=[typ,0x1000,0x2000,9000,frames,0x1234,0x5000]+[0]*11
        b+=d.EVENT.pack(808,12345,42,0,0,0,90,flags,*p)
        return b
    def test_footer_and_offsets(self):
        h,e=d.decode(self.blob());f=h['first_failure']
        self.assertEqual(len(self.blob()),6771888)
        self.assertEqual(f['trigger_type'],'RxMapperHalted')
        self.assertEqual(f['target_return_offsets'],['0x234',None])
        self.assertTrue(f['captured']);self.assertFalse(f['unstable']);self.assertFalse(e)
    def test_explicit_unavailable_states(self):
        for flags,key in [(0x80000000,'unstable'),(0x40000000,'disabled')]:
            h,e=d.decode(self.blob(flags,0,0));self.assertTrue(h['first_failure'][key])
            self.assertFalse(h['first_failure']['captured'])
    def test_rejected_formats_not_imported(self):
        for version in [8,9]:
            b=self.blob();struct.pack_into('<I',b,8,version)
            with self.assertRaises(ValueError): d.decode(b)
    def test_malformed_or_truncated_footer(self):
        with self.assertRaises(ValueError): d.decode(self.blob(frames=14))
        with self.assertRaises(ValueError): d.decode(self.blob()[:-1])

if __name__=='__main__': unittest.main()
