import hashlib
import importlib.util
import json
import pathlib
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('disp_decoder',ROOT/'tools/decode_poc.py')
d=importlib.util.module_from_spec(s);s.loader.exec_module(d)

class DispositionDecoder(unittest.TestCase):
    def event(self,hint,coverage=127,index=0):
        b=bytearray(d.HEADER.pack(0x3154434152545642,13,200,32768,6,1,0,0,0,1,2,bytes(16)))
        b+=bytes((32768+65+1025+1)*200)
        p=[19,615,0,coverage,4,0,index,123,123,0,2,0,1000,0,4,2|(10<<32),hint,16]
        d.EVENT.pack_into(b,88,1,100,1,2,3,4,100,0,*p)
        d.EVENT.pack_into(b,len(b)-200,0,0,0,0,0,0,90,0,*([0]*18))
        h,e=d.decode(b);return h,e[0]['tx_disposition']
    def test_eligible_is_never_executed_requeue(self):
        h,q=self.event(9)
        self.assertIn('NOT executed',q['branch_hint']);self.assertFalse(q['native_disposition_executed'])
        self.assertFalse(q['ownership_permission']);self.assertFalse(h['first_failure']['captured'])
    def test_disposal_is_never_cleanup_permission(self):
        for hint in (1,2,3,4,8):
            _,q=self.event(hint);self.assertFalse(q['ownership_permission']);self.assertIn('withheld',q['warning'])
    def test_unknown_recovery_not_cached_scb_liveness(self):
        _,q=self.event(6);self.assertEqual(q['branch_hint'],'native SCB recovery still required')
    def test_queue_count_and_signed_index(self):
        _,q=self.event(0,index=(1<<64)-1)
        self.assertEqual(q['bss_index'],-1);self.assertEqual((q['queue_count'],q['queue_limit']),(2,10))
    def test_mode2_not_mode0_queue(self):
        _,q=self.event(5);self.assertIn('filters still required',q['branch_hint'])

if __name__=='__main__':unittest.main()
